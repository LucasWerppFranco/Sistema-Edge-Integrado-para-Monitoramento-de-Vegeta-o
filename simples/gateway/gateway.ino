#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ===================== CONFIGURAÇÃO =====================

const char* ssid = "Mkdir";
const char* password = "";   // preencher antes de gravar

// MQTT Broker
const char* mqtt_broker = "broker.hivemq.com";
const char* mqtt_username = "";
const char* mqtt_password = "";
const int mqtt_port = 1883;

#define TOPIC_BASE    "edgeveg-lwf/rodovia"
#define TOPIC_RESUMO  TOPIC_BASE "/resumo"
#define TOPIC_GW      TOPIC_BASE "/gateway/status"

// Tópicos assinados (curinga: serve para qualquer ponto do trecho)
const char* topic[] = {
  TOPIC_BASE "/+/estado",
  TOPIC_BASE "/+/status"
};
const int totalTopicos = sizeof(topic) / sizeof(topic[0]);

#define LED_CORTE  14
#define LED_FALHA  27

const unsigned long RETENTAR_MS = 5000;

// ===================== ESTADO =====================

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

// Objetos
WiFiClient espClient;
PubSubClient client(espClient);

String clientId;
String resumoPublicado;
bool reavaliar = true;
unsigned long ultimaTentativa = 0;

// Prototipos
bool connectMQTT();
void callback(char* topic, byte* payload, unsigned int length);

// ===================== RECEPÇÃO =====================

Ponto* buscarPonto(char id) {
  for (auto& p : pontos) if (p.id == id) return &p;
  return nullptr;
}

void callback(char* topic, byte* payload, unsigned int length) {
  char msg[96];
  length = min(length, (unsigned int)sizeof(msg) - 1);
  memcpy(msg, payload, length);
  msg[length] = 0;

  Serial.print("Message arrived in topic: ");
  Serial.println(topic);

  const char* p = strstr(topic, "/ponto");
  Ponto* pt = p ? buscarPonto(p[6]) : nullptr;
  if (!pt) return;

  if (strstr(topic, "/status")) {
    // "OFFLINE" chega pelo Last Will quando o nó cai sem avisar
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

// Prioridade de atendimento: CORTE > ERRO > ATENCAO > OK
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
  Serial.printf("[GW] trecho: %s | MQTT %s\n", resumo.c_str(), client.connected() ? "ON" : "OFF");

  // Nuvem: só publica quando o resumo muda
  if (resumo != resumoPublicado && client.connected() &&
      client.publish(TOPIC_RESUMO, resumo.c_str(), true)) {
    resumoPublicado = resumo;
    Serial.printf("[GW] resumo -> nuvem: %s\n", resumo.c_str());
  }
}

// ===================== REDE =====================

// Uma tentativa de conexao, com Last Will do proprio gateway
bool connectMQTT() {
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);

  if (!client.connect(clientId.c_str(), mqtt_username, mqtt_password,
                      TOPIC_GW, 1, true, "OFFLINE")) {
    Serial.print("Falha ao conectar: ");
    Serial.println(client.state());
    return 0;
  }
  Serial.println("Exito na conexao:");
  Serial.printf("Cliente %s conectado ao broker\n", clientId.c_str());

  client.publish(TOPIC_GW, "ONLINE", true);
  // Os estados são "retained": ao (re)conectar, chega o último de cada ponto
  for (int i = 0; i < totalTopicos; i++) client.subscribe(topic[i], 1);
  resumoPublicado = "";
  reavaliar = true;
  return 1;
}

void cuidarDaRede() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!client.connected()) {
    if (millis() - ultimaTentativa < RETENTAR_MS) return;
    ultimaTentativa = millis();
    if (!connectMQTT()) return;
  }
  client.loop();
}

// ===================== SETUP / LOOP =====================

void setup() {
  Serial.begin(9600);
  pinMode(LED_CORTE, OUTPUT);
  pinMode(LED_FALHA, OUTPUT);
  Serial.println("[GW] Gateway iniciando");

  // Conectar
  WiFi.begin(ssid, password);

  // Aguardando conexao
  Serial.println();
  Serial.print("Conectando");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi nao conectado: mantendo ultimos estados");
  }

  clientId = String("edgeveg-gw-") + WiFi.macAddress();
  connectMQTT();
}

void loop() {
  cuidarDaRede();

  if (reavaliar) {
    reavaliar = false;
    avaliarTrecho();
  }
}
