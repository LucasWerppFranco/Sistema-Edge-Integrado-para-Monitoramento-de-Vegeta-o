#include <WiFi.h>
#include <PubSubClient.h>

// ===================== CONFIGURAÇÃO =====================

#define BROKER        "broker.hivemq.com"
#define TOPIC_BASE    "edgeveg-lwf/rodovia"
#define TOPIC_RESUMO  TOPIC_BASE "/resumo"
#define TOPIC_GW      TOPIC_BASE "/gateway/status"

#define LED_CORTE  14
#define LED_FALHA  27

struct Ponto {
  char id;
  bool conhecido;
  bool online;
  float altura;
  char estado[8];
  int dias;
  unsigned seq;
};
Ponto pontos[] = {{'A'}, {'B'}};

WiFiClient net;
PubSubClient mqtt(net);

String clientId;
String resumoPublicado;
bool reavaliar = true;
unsigned long ultimaTentativa = 0;

// ===================== RECEPÇÃO =====================

Ponto* buscarPonto(char id) {
  for (auto& p : pontos) if (p.id == id) return &p;
  return nullptr;
}

void aoReceber(char* topic, byte* payload, unsigned int len) {
  char msg[96];
  len = min(len, (unsigned int)sizeof(msg) - 1);
  memcpy(msg, payload, len);
  msg[len] = 0;

  const char* p = strstr(topic, "/ponto");
  Ponto* pt = p ? buscarPonto(p[6]) : nullptr;
  if (!pt) return;

  if (strstr(topic, "/status")) {
    pt->online = strcmp(msg, "ONLINE") == 0;
    Serial.printf("[GW] Ponto %c: %s\n", pt->id, msg);
  } else {
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
  reavaliar = true;
}

// ===================== DECISÃO DO TRECHO =====================

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

void avaliarTrecho() {
  bool algumCorte = false, algumaFalha = false;
  for (auto& p : pontos) {
    algumCorte |= p.conhecido && strcmp(p.estado, "CORTE") == 0;
    algumaFalha |= (p.conhecido && !p.online) || strcmp(p.estado, "ERRO") == 0;
  }
  digitalWrite(LED_CORTE, algumCorte);
  digitalWrite(LED_FALHA, algumaFalha);

  String resumo = calcularResumo();
  Serial.printf("[GW] trecho: %s | MQTT %s\n", resumo.c_str(), mqtt.connected() ? "ON" : "OFF");

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
      return;
    }
    mqtt.publish(TOPIC_GW, "ONLINE", true);
    mqtt.subscribe(TOPIC_BASE "/+/estado", 1);
    mqtt.subscribe(TOPIC_BASE "/+/status", 1);
    resumoPublicado = "";
    Serial.println("[REDE] MQTT conectado, aguardando pontos");
    reavaliar = true;
  }
  mqtt.loop();
}

// ===================== SETUP / LOOP =====================

void setup() {
  Serial.begin(115200);
  pinMode(LED_CORTE, OUTPUT);
  pinMode(LED_FALHA, OUTPUT);
  Serial.println("[GW] Gateway iniciando");

  clientId = String("edgeveg-gw-") + String(random(0xffff), HEX);
  WiFi.begin("Wokwi-GUEST", "", 6);
  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(aoReceber);
}

void loop() {
  cuidarDaRede();

  if (reavaliar) {
    reavaliar = false;
    avaliarTrecho();
  }
}
