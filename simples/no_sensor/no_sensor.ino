#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <DHT.h>
#include <algorithm>

// ===================== CONFIGURAÇÃO =====================

#define NODE_ID "A"

#define BROKER      "broker.hivemq.com"
#define TOPIC_BASE  "edgeveg-lwf/rodovia"
#define TOPIC_ESTADO TOPIC_BASE "/ponto" NODE_ID "/estado"
#define TOPIC_STATUS TOPIC_BASE "/ponto" NODE_ID "/status"

#define TRIG_PIN 5
#define ECHO_PIN 18
#define DHT_PIN  4
#define LDR_PIN  34
#define LED_PIN  14

const float H_SENSOR = 100.0;
const float DIST_MIN = 5.0;
const float ALTURA_POS_CORTE = 10.0;

const float LIM_ATENCAO = 25.0;
const float LIM_CORTE   = 30.0;
const float HISTERESE   = 1.0;

const int AMOSTRAS     = 5;
const int AMOSTRAS_MIN = 3;
const int FALHAS_MAX   = 3;

const unsigned long LEITURA_MS   = 2000;
const unsigned long HEARTBEAT_MS = 30000;

const float GAMMA = 0.7;
const float RL10  = 50;

// ===================== ESTADO =====================

enum Estado : uint8_t { NORMAL, ATENCAO, CORTE, ERRO };
const char* NOME_ESTADO[] = {"NORMAL", "ATENCAO", "CORTE", "ERRO"};

struct Leitura {
  float altura;
  Estado estado;
  int dias;
  float temp, umid;
  uint32_t seq;
  uint32_t ms;
};

Leitura atual = {NAN, NORMAL, -1, NAN, NAN, 0, 0};

const int FILA_MAX = 20;
Leitura fila[FILA_MAX];
int filaInicio = 0, filaQtd = 0;

DHT dht(DHT_PIN, DHT22);
WiFiClient net;
PubSubClient mqtt(net);
Preferences prefs;

String clientId;
int falhasSeguidas = 0;
unsigned long ultimaLeitura = 0, ultimoEnvio = 0, ultimaTentativa = 0;

// ===================== LEITURA DOS SENSORES =====================

float medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long us = pulseIn(ECHO_PIN, HIGH, 30000);
  return us == 0 ? NAN : us / 58.0;
}

float mediana(float* v, int n) {
  std::sort(v, v + n);
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

float lerAlturaCm() {
  float validas[AMOSTRAS];
  int n = 0;
  for (int i = 0; i < AMOSTRAS; i++) {
    float d = medirDistanciaCm();
    float h = H_SENSOR - d;
    if (!isnan(d) && h >= 0 && d >= DIST_MIN) validas[n++] = h;
    delay(20);
  }
  if (n < AMOSTRAS_MIN) return NAN;
  return mediana(validas, n);
}

float lerLux() {
  int raw = analogRead(LDR_PIN);
  if (raw <= 0) return 120000;
  if (raw >= 4095) return 0;
  float v = raw / 4095.0 * 3.3;
  float r = 2000 * v / (1 - v / 3.3);
  return pow(RL10 * 1e3 * pow(10, GAMMA) / r, 1 / GAMMA);
}

// ===================== PROCESSAMENTO EDGE =====================

Estado classificar(float h, Estado anterior) {
  float limAtencao = LIM_ATENCAO, limCorte = LIM_CORTE;
  if (anterior == ATENCAO || anterior == CORTE) limAtencao -= HISTERESE;
  if (anterior == CORTE) limCorte -= HISTERESE;
  if (h >= limCorte) return CORTE;
  if (h >= limAtencao) return ATENCAO;
  return NORMAL;
}

float avaliarTemperatura(float t) {
  if (t < 0 || t > 45) return 0.0;
  if (t >= 5 && t <= 15) return 0.4;
  if (t >= 24 && t <= 28) return 1.0;
  if (t >= 20 && t <= 30) return 0.8;
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

int preverDiasParaCorte(float altura, float t, float u, float lux) {
  if (isnan(altura) || isnan(t) || isnan(u)) return -1;
  if (altura >= LIM_CORTE) return 0;

  float sT = avaliarTemperatura(t), sU = avaliarUmidade(u), sL = avaliarLuminosidade(lux);
  if (sT == 0 || sU == 0 || sL == 0) return -1;

  float indice = sT * 0.4 + sU * 0.3 + sL * 0.3;
  int diasCiclo = indice >= 0.9 ? 5 : indice >= 0.7 ? 7 : indice >= 0.5 ? 10 : indice >= 0.3 ? 15 : 20;

  float taxa = (LIM_CORTE - ALTURA_POS_CORTE) / diasCiclo;
  return ceil((LIM_CORTE - altura) / taxa);
}

// ===================== COMUNICAÇÃO =====================

bool online() { return mqtt.connected(); }

bool publicar(const Leitura& l, uint32_t idadeS) {
  char msg[96];
  snprintf(msg, sizeof(msg), "%s;%.1f;%s;%d;%.1f;%.0f;%u;%u",
           NODE_ID, isnan(l.altura) ? -1.0 : l.altura, NOME_ESTADO[l.estado],
           l.dias, l.temp, l.umid, (unsigned)l.seq, (unsigned)idadeS);
  bool ok = mqtt.publish(TOPIC_ESTADO, msg, true);
  if (ok) {
    ultimoEnvio = millis();
    Serial.printf("  [TX] %s -> %s\n", TOPIC_ESTADO, msg);
  }
  return ok;
}

void enviarFila() {
  while (filaQtd > 0 && online()) {
    Leitura& e = fila[filaInicio];
    if (!publicar(e, (millis() - e.ms) / 1000)) break;
    filaInicio = (filaInicio + 1) % FILA_MAX;
    filaQtd--;
  }
}

void registrarEvento(const Leitura& l) {
  if (filaQtd == FILA_MAX) {
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
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    if (millis() - ultimaTentativa < 5000) return;
    ultimaTentativa = millis();
    if (!mqtt.connect(clientId.c_str(), TOPIC_STATUS, 1, true, "OFFLINE")) {
      Serial.printf("[REDE] MQTT falhou (rc=%d), tentando de novo em 5 s\n", mqtt.state());
      return;
    }
    mqtt.publish(TOPIC_STATUS, "ONLINE", true);
    Serial.println("[REDE] MQTT conectado");
    enviarFila();
    publicar(atual, 0);
  }
  mqtt.loop();
}

// ===================== AUTOTESTE =====================

void autoteste() {
  float ruido[] = {22, 250, 23, 21, 22};
  bool ok = mediana(ruido, 5) == 22
         && classificar(18, NORMAL) == NORMAL
         && classificar(27, NORMAL) == ATENCAO
         && classificar(34, NORMAL) == CORTE
         && classificar(29.5, NORMAL) == ATENCAO
         && classificar(29.5, CORTE) == CORTE
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
  dht.begin();

  autoteste();

  prefs.begin("edge", false);
  atual.seq = prefs.getUInt("seq", 0);
  atual.estado = (Estado)prefs.getUChar("estado", NORMAL);
  Serial.printf("[BOOT] Ponto %s | estado restaurado: %s | seq=%u\n",
                NODE_ID, NOME_ESTADO[atual.estado], (unsigned)atual.seq);

  clientId = String("edgeveg-") + NODE_ID + "-" + String(random(0xffff), HEX);
  WiFi.begin("Wokwi-GUEST", "", 6);
  mqtt.setServer(BROKER, 1883);
}

void loop() {
  if (Serial.available() && Serial.read() == 'r') ESP.restart();

  cuidarDaRede();

  if (millis() - ultimaLeitura < LEITURA_MS) return;
  ultimaLeitura = millis();

  float h = lerAlturaCm();
  float t = dht.readTemperature();
  float u = dht.readHumidity();
  float lux = lerLux();

  Estado novo = atual.estado;
  if (isnan(h)) {
    falhasSeguidas++;
    Serial.printf("[SENSOR] leitura de altura invalida descartada (%d/%d)\n", falhasSeguidas, FALHAS_MAX);
    if (falhasSeguidas >= FALHAS_MAX) novo = ERRO;
  } else {
    falhasSeguidas = 0;
    atual.altura = h;
    novo = classificar(h, atual.estado);
  }
  if (isnan(t) || isnan(u)) Serial.println("[SENSOR] DHT22 sem leitura: previsao suspensa");
  atual.temp = t;
  atual.umid = u;
  atual.dias = novo == ERRO ? -1 : preverDiasParaCorte(atual.altura, t, u, lux);

  Serial.printf("[%s] h=%.1fcm T=%.1f U=%.0f lux=%.0f -> %s | corte em %d d | %s | fila=%d\n",
                NODE_ID, atual.altura, t, u, lux, NOME_ESTADO[novo], atual.dias,
                online() ? "ONLINE" : "OFFLINE", filaQtd);

  if (novo != atual.estado) {
    Serial.printf("  [EVENTO] %s -> %s\n", NOME_ESTADO[atual.estado], NOME_ESTADO[novo]);
    atual.estado = novo;
    atual.seq++;
    atual.ms = millis();
    prefs.putUInt("seq", atual.seq);
    prefs.putUChar("estado", atual.estado);
    registrarEvento(atual);
  } else if (online() && millis() - ultimoEnvio >= HEARTBEAT_MS) {
    Serial.println("  [HEARTBEAT] sem mudanca, enviando sinal de vida");
    publicar(atual, 0);
  } else {
    Serial.println("  [TX] nada a enviar (estado estavel)");
  }

  digitalWrite(LED_PIN, atual.estado == CORTE ? HIGH : LOW);
}
