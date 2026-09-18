/*
 * NÓ SENSOR — Sistema Edge Integrado para Monitoramento de Vegetação
 *
 * Um nó por ponto da rodovia. O mesmo firmware roda no Ponto A e no Ponto B:
 * só muda NODE_ID.
 *
 * Fluxo: Sensor -> Tratamento local -> Decisão local -> Comunicação (só quando precisa)
 *
 *   HC-SR04 (altura) ---\
 *   DHT22 (temp/umid) ---+--> mediana + descarte --> classificação --> LCD/LED (sempre)
 *   LDR (luz) ----------/                          \-> previsão de corte
 *                                                   \-> MQTT só em mudança de estado
 *                                                       ou heartbeat; fila se offline
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <algorithm>

// ===================== CONFIGURAÇÃO =====================

#define NODE_ID "A"                    // "A" no Ponto A, "B" no Ponto B

// Broker público: TOPIC_BASE precisa ser único para não misturar com outras turmas
#define BROKER      "broker.hivemq.com"
#define TOPIC_BASE  "edgeveg-lwf/rodovia"
#define TOPIC_ESTADO TOPIC_BASE "/ponto" NODE_ID "/estado"
#define TOPIC_STATUS TOPIC_BASE "/ponto" NODE_ID "/status"

// Pinos
#define TRIG_PIN 5
#define ECHO_PIN 18
#define DHT_PIN  4
#define LDR_PIN  34     // ADC1: continua funcionando com o Wi-Fi ligado (ADC2 não)
#define LED_PIN  14     // vermelho: CORTE NECESSÁRIO
#define LINK_PIN 13     // switch LINK: esquerda = conectado, direita = simula queda

// Calibração da instalação (ajustar em campo)
const float H_SENSOR = 100.0;         // altura do sensor em relação ao solo (cm)
const float DIST_MIN = 5.0;           // abaixo disso o ultrassom não é confiável
const float ALTURA_POS_CORTE = 10.0;  // altura que a roçada deixa (cm)

// Limiares de decisão (cm)
const float LIM_ATENCAO = 25.0;
const float LIM_CORTE   = 30.0;
const float HISTERESE   = 1.0;        // evita alternar de estado perto do limiar

// Tratamento de dados
const int AMOSTRAS     = 5;           // leituras por ciclo -> mediana
const int AMOSTRAS_MIN = 3;           // mínimo de leituras válidas para confiar
const int FALHAS_MAX   = 3;           // ciclos inválidos seguidos -> estado ERRO

// Temporização. Na simulação é curto para a demonstração;
// em campo seriam ~30 min por leitura e 1 h de heartbeat (a grama cresce devagar)
const unsigned long LEITURA_MS   = 2000;
const unsigned long HEARTBEAT_MS = 30000;

// LDR do Wokwi (ver docs.wokwi.com/parts/wokwi-photoresistor-sensor)
const float GAMMA = 0.7;
const float RL10  = 50;

// ===================== ESTADO =====================

enum Estado : uint8_t { NORMAL, ATENCAO, CORTE, ERRO };
const char* NOME_ESTADO[] = {"NORMAL", "ATENCAO", "CORTE", "ERRO"};

struct Leitura {
  float altura;      // cm (NAN = sem leitura válida)
  Estado estado;
  int dias;          // previsão de dias até o corte (-1 = sem previsão)
  float temp, umid;
  uint32_t seq;      // número do evento: o gateway percebe se algum se perdeu
  uint32_t ms;       // quando o evento aconteceu (para calcular o atraso)
};

Leitura atual = {NAN, NORMAL, -1, NAN, NAN, 0, 0};

// Fila circular de eventos: guarda o que não pôde ser enviado sem conexão.
// ponytail: fica só na RAM (perde-se num reboot). Para sobreviver a reboot, gravar em NVS/SPIFFS.
const int FILA_MAX = 20;
Leitura fila[FILA_MAX];
int filaInicio = 0, filaQtd = 0;

DHT dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient net;
PubSubClient mqtt(net);
Preferences prefs;

String clientId;
int falhasSeguidas = 0;
unsigned long ultimaLeitura = 0, ultimoEnvio = 0, ultimaTentativa = 0;

// ===================== LEITURA DOS SENSORES =====================

// Uma medição do HC-SR04 em cm (NAN se não houve eco)
float medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long us = pulseIn(ECHO_PIN, HIGH, 30000);  // 30 ms cobre os 400 cm
  return us == 0 ? NAN : us / 58.0;
}

float mediana(float* v, int n) {
  std::sort(v, v + n);
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// TRATAMENTO: várias amostras, descarta inválidas, usa a mediana (resiste a picos)
float lerAlturaCm() {
  float validas[AMOSTRAS];
  int n = 0;
  for (int i = 0; i < AMOSTRAS; i++) {
    float d = medirDistanciaCm();
    float h = H_SENSOR - d;
    // Inválida: sem eco, abaixo do solo (h < 0) ou perto demais do sensor
    if (!isnan(d) && h >= 0 && d >= DIST_MIN) validas[n++] = h;
    delay(20);
  }
  if (n < AMOSTRAS_MIN) return NAN;
  return mediana(validas, n);
}

// Converte o LDR do Wokwi em lux. Tensão maior = MENOS luz.
float lerLux() {
  int raw = analogRead(LDR_PIN);
  if (raw <= 0) return 120000;   // saturado de luz
  if (raw >= 4095) return 0;     // escuro total
  float v = raw / 4095.0 * 3.3;
  float r = 2000 * v / (1 - v / 3.3);
  return pow(RL10 * 1e3 * pow(10, GAMMA) / r, 1 / GAMMA);
}

// ===================== PROCESSAMENTO EDGE =====================

// DECISÃO LOCAL: classifica a altura. A histerese exige que a altura caia
// HISTERESE cm abaixo do limiar para voltar de nível, o que evita
// ATENCAO/NORMAL/ATENCAO/... quando a grama está bem no limite.
Estado classificar(float h, Estado anterior) {
  float limAtencao = LIM_ATENCAO, limCorte = LIM_CORTE;
  if (anterior == ATENCAO || anterior == CORTE) limAtencao -= HISTERESE;
  if (anterior == CORTE) limCorte -= HISTERESE;
  if (h >= limCorte) return CORTE;
  if (h >= limAtencao) return ATENCAO;
  return NORMAL;
}

// Notas por condição ambiental (lógica da Sprint 02, mantida)
float avaliarTemperatura(float t) {
  if (t < 0 || t > 45) return 0.0;     // vida impossível
  if (t >= 5 && t <= 15) return 0.4;   // crescimento lento
  if (t >= 24 && t <= 28) return 1.0;  // muito ideal
  if (t >= 20 && t <= 30) return 0.8;  // ideal
  return 0.6;
}

float avaliarUmidade(float u) {
  if (u < 15 || u > 95) return 0.0;
  if (u >= 20 && u <= 40) return 0.4;
  if (u >= 60 && u <= 70) return 1.0;
  if (u >= 50 && u <= 75) return 0.8;
  return 0.6;
}

float avaliarLuminosidade(float lux) {
  if (lux < 500 || lux > 120000) return 0.0;
  if (lux >= 1000 && lux <= 10000) return 0.4;
  if (lux >= 40000 && lux <= 70000) return 1.0;
  if (lux >= 20000 && lux <= 60000) return 0.8;
  return 0.6;
}

// PREVISÃO: o índice da Sprint 02 dá a duração de um ciclo de crescimento.
// Com a altura real, calcula quantos dias faltam até atingir LIM_CORTE.
int preverDiasParaCorte(float altura, float t, float u, float lux) {
  if (isnan(altura) || isnan(t) || isnan(u)) return -1;  // falta dado: sem previsão
  if (altura >= LIM_CORTE) return 0;

  float sT = avaliarTemperatura(t), sU = avaliarUmidade(u), sL = avaliarLuminosidade(lux);
  if (sT == 0 || sU == 0 || sL == 0) return -1;          // vida impossível: não cresce

  float indice = sT * 0.4 + sU * 0.3 + sL * 0.3;
  int diasCiclo = indice >= 0.9 ? 5 : indice >= 0.7 ? 7 : indice >= 0.5 ? 10 : indice >= 0.3 ? 15 : 20;

  float taxa = (LIM_CORTE - ALTURA_POS_CORTE) / diasCiclo;  // cm por dia
  return ceil((LIM_CORTE - altura) / taxa);
}

// ===================== COMUNICAÇÃO =====================

bool linkAtivo() { return digitalRead(LINK_PIN) == HIGH; }
bool online() { return linkAtivo() && mqtt.connected(); }

// Payload CSV: id;altura;estado;dias;temp;umid;seq;idade_s
// altura -1 = sem leitura; idade_s > 0 = evento que ficou na fila offline
bool publicar(const Leitura& l, uint32_t idadeS) {
  char msg[96];
  snprintf(msg, sizeof(msg), "%s;%.1f;%s;%d;%.1f;%.0f;%u;%u",
           NODE_ID, isnan(l.altura) ? -1.0 : l.altura, NOME_ESTADO[l.estado],
           l.dias, l.temp, l.umid, (unsigned)l.seq, (unsigned)idadeS);
  // retained: um gateway que reinicia recebe na hora o último estado de cada ponto
  bool ok = mqtt.publish(TOPIC_ESTADO, msg, true);
  if (ok) {
    ultimoEnvio = millis();
    Serial.printf("  [TX] %s -> %s\n", TOPIC_ESTADO, msg);
  }
  return ok;
}

// Envia a fila em ordem. Se a conexão cair no meio, o resto espera.
void enviarFila() {
  while (filaQtd > 0 && online()) {
    Leitura& e = fila[filaInicio];
    if (!publicar(e, (millis() - e.ms) / 1000)) break;
    filaInicio = (filaInicio + 1) % FILA_MAX;
    filaQtd--;
  }
}

// Todo evento passa pela fila, online ou não, então há um caminho só
void registrarEvento(const Leitura& l) {
  if (filaQtd == FILA_MAX) {  // fila cheia: descarta o mais antigo
    filaInicio = (filaInicio + 1) % FILA_MAX;
    filaQtd--;
    Serial.println("  [FILA] cheia, evento mais antigo descartado");
  }
  fila[(filaInicio + filaQtd) % FILA_MAX] = l;
  filaQtd++;
  if (online()) enviarFila();
  else Serial.printf("  [FILA] sem conexao, evento guardado (%d na fila)\n", filaQtd);
}

void cuidarDaRede() {
  if (!linkAtivo()) {
    // Simula a queda: fecha o TCP SEM mandar DISCONNECT. O broker trata como
    // queda real e publica o Last Will "OFFLINE" -> o gateway fica sabendo.
    if (net.connected()) {
      net.stop();
      Serial.println("[REDE] LINK desligado: simulando perda de conexao");
    }
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;  // o ESP32 reconecta o Wi-Fi sozinho

  if (!mqtt.connected()) {
    if (millis() - ultimaTentativa < 5000) return;
    ultimaTentativa = millis();
    // Last Will: se este nó sumir sem avisar, o broker publica "OFFLINE" por ele
    if (!mqtt.connect(clientId.c_str(), TOPIC_STATUS, 1, true, "OFFLINE")) {
      Serial.printf("[REDE] MQTT falhou (rc=%d), tentando de novo em 5 s\n", mqtt.state());
      return;
    }
    mqtt.publish(TOPIC_STATUS, "ONLINE", true);
    Serial.println("[REDE] MQTT conectado");
    enviarFila();       // primeiro o que ficou pendente, em ordem
    publicar(atual, 0); // depois o estado atual
  }
  mqtt.loop();
}

// ===================== SAÍDA LOCAL =====================

void atualizarLcdLed() {
  char l1[17], l2[17], dias[4];
  if (isnan(atual.altura) || atual.estado == ERRO)
    snprintf(l1, sizeof(l1), "%s --.-cm %-7s", NODE_ID, NOME_ESTADO[atual.estado]);
  else
    snprintf(l1, sizeof(l1), "%s %4.1fcm %-7s", NODE_ID, atual.altura, NOME_ESTADO[atual.estado]);

  if (atual.dias < 0) snprintf(dias, sizeof(dias), "--");
  else snprintf(dias, sizeof(dias), "%d", atual.dias);
  snprintf(l2, sizeof(l2), "Corte:%sd %s f%d", dias, online() ? "ON " : "OFF", filaQtd);

  lcd.setCursor(0, 0); lcd.printf("%-16s", l1);  // completa com espaços: apaga sobras
  lcd.setCursor(0, 1); lcd.printf("%-16s", l2);

  digitalWrite(LED_PIN, atual.estado == CORTE ? HIGH : LOW);
}

// ===================== AUTOTESTE =====================

// Confere a lógica de decisão a cada boot. Resultado no Serial Monitor.
void autoteste() {
  float ruido[] = {22, 250, 23, 21, 22};  // 250 = pico de ruído do ultrassom
  bool ok = mediana(ruido, 5) == 22
         && classificar(18, NORMAL) == NORMAL
         && classificar(27, NORMAL) == ATENCAO
         && classificar(34, NORMAL) == CORTE
         && classificar(29.5, NORMAL) == ATENCAO
         && classificar(29.5, CORTE) == CORTE      // histerese: não desce no limite
         && classificar(24.5, ATENCAO) == ATENCAO
         && classificar(23.9, ATENCAO) == NORMAL
         && preverDiasParaCorte(34, 25, 65, 50000) == 0
         && preverDiasParaCorte(NAN, 25, 65, 50000) == -1;
  Serial.println(ok ? "[AUTOTESTE] OK" : "[AUTOTESTE] FALHOU - revisar logica de decisao");
}

// ===================== SETUP / LOOP =====================

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LINK_PIN, INPUT_PULLUP);
  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.print("Ponto " NODE_ID " iniciando");

  autoteste();

  // Restaura da flash o último estado e o contador de eventos (sobrevive a reboot)
  prefs.begin("edge", false);
  atual.seq = prefs.getUInt("seq", 0);
  atual.estado = (Estado)prefs.getUChar("estado", NORMAL);
  Serial.printf("[BOOT] Ponto %s | estado restaurado: %s | seq=%u\n",
                NODE_ID, NOME_ESTADO[atual.estado], (unsigned)atual.seq);

  clientId = String("edgeveg-") + NODE_ID + "-" + String(random(0xffff), HEX);
  WiFi.begin("Wokwi-GUEST", "", 6);  // em campo: ESP-NOW até o gateway (ver README)
  mqtt.setServer(BROKER, 1883);
}

void loop() {
  if (Serial.available() && Serial.read() == 'r') ESP.restart();  // teste de reboot (F4)

  cuidarDaRede();

  if (millis() - ultimaLeitura < LEITURA_MS) return;
  ultimaLeitura = millis();

  // 1. LEITURA
  float h = lerAlturaCm();
  float t = dht.readTemperature();
  float u = dht.readHumidity();
  float lux = lerLux();

  // 2. TRATAMENTO: leitura inválida é descartada; só vira ERRO se persistir
  Estado novo = atual.estado;
  if (isnan(h)) {
    falhasSeguidas++;
    Serial.printf("[SENSOR] leitura de altura invalida descartada (%d/%d)\n", falhasSeguidas, FALHAS_MAX);
    if (falhasSeguidas >= FALHAS_MAX) novo = ERRO;
  } else {
    falhasSeguidas = 0;
    atual.altura = h;
    novo = classificar(h, atual.estado);  // 3. DECISÃO LOCAL
  }
  if (isnan(t) || isnan(u)) Serial.println("[SENSOR] DHT22 sem leitura: previsao suspensa");
  atual.temp = t;
  atual.umid = u;
  atual.dias = novo == ERRO ? -1 : preverDiasParaCorte(atual.altura, t, u, lux);

  Serial.printf("[%s] h=%.1fcm T=%.1f U=%.0f lux=%.0f -> %s | corte em %d d | %s | fila=%d\n",
                NODE_ID, atual.altura, t, u, lux, NOME_ESTADO[novo], atual.dias,
                online() ? "ONLINE" : "OFFLINE", filaQtd);

  // 4. ESTRATÉGIA DE COMUNICAÇÃO: transmite só o que importa
  if (novo != atual.estado) {
    Serial.printf("  [EVENTO] %s -> %s\n", NOME_ESTADO[atual.estado], NOME_ESTADO[novo]);
    atual.estado = novo;
    atual.seq++;
    atual.ms = millis();
    prefs.putUInt("seq", atual.seq);  // grava na flash só em evento (poupa a memória)
    prefs.putUChar("estado", atual.estado);
    registrarEvento(atual);
  } else if (online() && millis() - ultimoEnvio >= HEARTBEAT_MS) {
    Serial.println("  [HEARTBEAT] sem mudanca, enviando sinal de vida");
    publicar(atual, 0);
  } else {
    Serial.println("  [TX] nada a enviar (estado estavel)");
  }

  // 5. RESULTADO LOCAL: funciona com ou sem rede
  atualizarLcdLed();
}
