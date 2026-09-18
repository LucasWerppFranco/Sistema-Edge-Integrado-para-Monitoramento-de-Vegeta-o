/*
 * GATEWAY — Sistema Edge Integrado para Monitoramento de Vegetação
 *
 * Recebe os eventos dos pontos da rodovia, mantém o último estado de cada um,
 * detecta pontos fora do ar e mostra ao operador qual trecho precisa de corte.
 * Publica um resumo do trecho para a nuvem (painel da concessionária).
 *
 *   Ponto A --\                        /--> LCD 20x4 + LEDs (local)
 *              >-- MQTT --> GATEWAY --<
 *   Ponto B --/                        \--> TOPIC_BASE/resumo (nuvem)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ===================== CONFIGURAÇÃO =====================

#define BROKER        "broker.hivemq.com"
#define TOPIC_BASE    "edgeveg-lwf/rodovia"   // igual ao dos nós
#define TOPIC_RESUMO  TOPIC_BASE "/resumo"
#define TOPIC_GW      TOPIC_BASE "/gateway/status"

#define LED_CORTE  14   // vermelho: algum ponto precisa de corte
#define LED_FALHA  27   // amarelo: algum ponto offline ou com sensor em erro

// Pontos monitorados por este gateway (um por linha do LCD)
struct Ponto {
  char id;
  bool conhecido;     // já recebeu algum estado
  bool online;
  float altura;
  char estado[8];
  int dias;
  unsigned seq;
};
Ponto pontos[] = {{'A'}, {'B'}};
const int N_PONTOS = sizeof(pontos) / sizeof(pontos[0]);

LiquidCrystal_I2C lcd(0x27, 20, 4);
WiFiClient net;
PubSubClient mqtt(net);

String clientId;
String resumoPublicado;
bool redesenhar = true;
unsigned long ultimaTentativa = 0;

// ===================== RECEPÇÃO =====================

Ponto* buscarPonto(char id) {
  for (auto& p : pontos) if (p.id == id) return &p;
  return nullptr;
}

// Tópicos: TOPIC_BASE/pontoX/estado  e  TOPIC_BASE/pontoX/status
void aoReceber(char* topic, byte* payload, unsigned int len) {
  char msg[96];
  len = min(len, (unsigned int)sizeof(msg) - 1);
  memcpy(msg, payload, len);
  msg[len] = 0;

  const char* p = strstr(topic, "/ponto");
  Ponto* pt = p ? buscarPonto(p[6]) : nullptr;
  if (!pt) return;  // ponto que não pertence a este trecho

  if (strstr(topic, "/status")) {
    // "OFFLINE" chega pelo Last Will quando o nó cai sem avisar
    pt->online = strcmp(msg, "ONLINE") == 0;
    Serial.printf("[GW] Ponto %c: %s\n", pt->id, msg);
  } else {
    // Payload: id;altura;estado;dias;temp;umid;seq;idade_s  (temp/umid ignorados aqui)
    char id, estado[8];
    float altura;
    int dias;
    unsigned seq, idade;
    if (sscanf(msg, "%c;%f;%7[^;];%d;%*[^;];%*[^;];%u;%u",
               &id, &altura, estado, &dias, &seq, &idade) != 6) {
      Serial.printf("[GW] mensagem invalida descartada: %s\n", msg);
      return;
    }
    if (pt->conhecido && seq > pt->seq + 1)
      Serial.printf("[GW] Ponto %c: %u evento(s) perdido(s)\n", pt->id, seq - pt->seq - 1);
    if (idade > 0)
      Serial.printf("[GW] Ponto %c: evento atrasado %us (guardado enquanto offline)\n", pt->id, idade);

    pt->conhecido = true;
    pt->altura = altura;
    strcpy(pt->estado, estado);
    pt->dias = dias;
    pt->seq = seq;
    Serial.printf("[GW] Ponto %c: %.1f cm %s (corte em %d d) seq=%u\n", pt->id, altura, estado, dias, seq);
  }
  redesenhar = true;
}

// ===================== DECISÃO DO TRECHO =====================

// O que a equipe de campo deve atender primeiro: CORTE > ERRO > ATENCAO > OK.
// Usa o último estado conhecido, mesmo de ponto offline.
String calcularResumo() {
  const char* prioridade[] = {"CORTE", "ERRO", "ATENCAO"};
  for (const char* e : prioridade) {
    String ids;
    for (auto& p : pontos) if (p.conhecido && strcmp(p.estado, e) == 0) ids += p.id;
    if (ids.length()) return String(e) + ": " + ids;
  }
  return "TRECHO OK";
}

// ===================== SAÍDA =====================

void atualizarDisplay() {
  bool algumCorte = false, algumaFalha = false;
  int onlines = 0;

  for (int i = 0; i < N_PONTOS; i++) {
    Ponto& p = pontos[i];
    char linha[21], dias[4];
    if (p.dias < 0) snprintf(dias, sizeof(dias), " --");
    else snprintf(dias, sizeof(dias), "%2dd", p.dias);

    if (!p.conhecido)
      snprintf(linha, sizeof(linha), "%c aguardando...", p.id);
    else if (!p.online)
      snprintf(linha, sizeof(linha), "%c OFFLINE (%s)", p.id, p.estado);
    else if (p.altura < 0)
      snprintf(linha, sizeof(linha), "%c  --.-cm %-7s%s", p.id, p.estado, dias);
    else
      snprintf(linha, sizeof(linha), "%c%6.1fcm %-7s%s", p.id, p.altura, p.estado, dias);

    lcd.setCursor(0, i);
    lcd.printf("%-20s", linha);

    onlines += p.online;
    algumCorte |= p.conhecido && strcmp(p.estado, "CORTE") == 0;
    algumaFalha |= (p.conhecido && !p.online) || strcmp(p.estado, "ERRO") == 0;
  }

  String resumo = calcularResumo();
  lcd.setCursor(0, 2);
  lcd.printf("Online %d/%d  MQTT %-3s", onlines, N_PONTOS, mqtt.connected() ? "ON" : "OFF");
  lcd.setCursor(0, 3);
  lcd.printf("%-20s", resumo.c_str());

  digitalWrite(LED_CORTE, algumCorte);
  digitalWrite(LED_FALHA, algumaFalha);

  // Nuvem: só publica quando o resumo muda (mesma estratégia dos nós)
  if (resumo != resumoPublicado && mqtt.connected() &&
      mqtt.publish(TOPIC_RESUMO, resumo.c_str(), true)) {
    resumoPublicado = resumo;
    Serial.printf("[GW] resumo -> nuvem: %s\n", resumo.c_str());
  }
}

// ===================== REDE =====================

void cuidarDaRede() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (millis() - ultimaTentativa < 5000) return;
    ultimaTentativa = millis();
    if (!mqtt.connect(clientId.c_str(), TOPIC_GW, 1, true, "OFFLINE")) {
      Serial.printf("[REDE] MQTT falhou (rc=%d); mantendo ultimos estados\n", mqtt.state());
      redesenhar = true;
      return;
    }
    mqtt.publish(TOPIC_GW, "ONLINE", true);
    // Os estados são "retained": ao (re)conectar, chega o último de cada ponto
    mqtt.subscribe(TOPIC_BASE "/+/estado", 1);
    mqtt.subscribe(TOPIC_BASE "/+/status", 1);
    resumoPublicado = "";  // republica o resumo na reconexão
    Serial.println("[REDE] MQTT conectado, aguardando pontos");
    redesenhar = true;
  }
  mqtt.loop();
}

// ===================== SETUP / LOOP =====================

void setup() {
  Serial.begin(115200);
  pinMode(LED_CORTE, OUTPUT);
  pinMode(LED_FALHA, OUTPUT);
  lcd.init();
  lcd.backlight();
  lcd.print("Gateway iniciando");

  clientId = String("edgeveg-gw-") + String(random(0xffff), HEX);
  WiFi.begin("Wokwi-GUEST", "", 6);
  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(aoReceber);
}

void loop() {
  bool estavaConectado = mqtt.connected();
  cuidarDaRede();
  if (estavaConectado != mqtt.connected()) redesenhar = true;

  if (redesenhar) {
    redesenhar = false;
    atualizarDisplay();
  }
}
