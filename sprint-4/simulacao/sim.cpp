// Simulador dos 5 cenarios da Sprint 04.
// As funcoes de decisao vem extraidas de simples/no_sensor/no_sensor.ino (firmware.h).
// O que este arquivo reimplementa e so o encadeamento do loop(): amostragem,
// descarte, contagem de falhas, evento, heartbeat e fila offline.
#include "firmware.h"
#include <cstdio>
#include <cstring>
#include <random>

static std::mt19937 rng(42);  // semente fixa: a simulacao e reproduzivel

// --------- estado equivalente ao do firmware ---------
struct Leitura { float altura; Estado estado; int dias; uint32_t seq; uint32_t ms; };
Leitura atual = {NAN, NORMAL, -1, 0, 0};
Leitura fila[FILA_MAX];
int filaInicio = 0, filaQtd = 0;
int falhasSeguidas = 0;
bool conectado = true;
unsigned long agora_ms = 0, ultimoEnvio = 0;

// --------- sensor simulado ---------
// Uma medicao do HC-SR04: distancia real + ruido gaussiano de 0,5 cm.
// distReal < 0 representa ausencia de eco (nada dentro do alcance).
float medirSimulado(float distReal, float ruidoCm) {
  if (distReal < 0) return NAN;
  std::normal_distribution<float> ruido(0.0f, ruidoCm);
  return distReal + ruido(rng);
}

// Mesmo tratamento de lerAlturaCm(): 5 amostras, descarta invalidas, mediana
float lerAlturaSimulada(float distReal, float ruidoCm, bool picoDeRuido) {
  float validas[AMOSTRAS];
  int n = 0;
  for (int i = 0; i < AMOSTRAS; i++) {
    float d = medirSimulado(distReal, ruidoCm);
    if (picoDeRuido && i == 1) d = 8.0;   // eco espurio em objeto proximo
    float h = H_SENSOR - d;
    if (!isnan(d) && h >= 0 && d >= DIST_MIN) validas[n++] = h;
  }
  if (n < AMOSTRAS_MIN) return NAN;
  return mediana(validas, n);
}

// --------- comunicacao simulada ---------
void publicar(const Leitura& l, uint32_t idadeS) {
  printf("    [TX] A;%.1f;%s;%d;%u;%u\n", isnan(l.altura) ? -1.0 : l.altura,
         NOME_ESTADO[l.estado], l.dias, (unsigned)l.seq, (unsigned)idadeS);
  ultimoEnvio = agora_ms;
}

void enviarFila() {
  while (filaQtd > 0 && conectado) {
    Leitura& e = fila[filaInicio];
    publicar(e, (agora_ms - e.ms) / 1000);
    filaInicio = (filaInicio + 1) % FILA_MAX;
    filaQtd--;
  }
}

void registrarEvento(const Leitura& l) {
  if (filaQtd == FILA_MAX) { filaInicio = (filaInicio + 1) % FILA_MAX; filaQtd--; }
  fila[(filaInicio + filaQtd) % FILA_MAX] = l;
  filaQtd++;
  if (conectado) enviarFila();
}

// Emulacao do Preferences: o firmware grava seq e estado na flash a cada evento
uint32_t flash_seq = 0;
uint8_t flash_estado = NORMAL;
void gravarFlash() { flash_seq = atual.seq; flash_estado = atual.estado; }
void reiniciar() {
  printf("  -- reset do ESP32 (tecla 'r')\n");
  atual = {NAN, (Estado)flash_estado, -1, flash_seq, 0};
  falhasSeguidas = 0;
  printf("  [BOOT] estado restaurado: %s | seq=%u\n", NOME_ESTADO[atual.estado], (unsigned)atual.seq);
}

// --------- um ciclo de 2 s, espelhando loop() ---------
Estado ciclo(float distReal, float lux, float ruidoCm = 0.5, bool picoDeRuido = false) {
  agora_ms += LEITURA_MS;
  float h = lerAlturaSimulada(distReal, ruidoCm, picoDeRuido);

  Estado novo = atual.estado;
  if (isnan(h)) {
    falhasSeguidas++;
    if (falhasSeguidas >= FALHAS_MAX) { novo = ERRO; atual.altura = NAN; }
  } else {
    falhasSeguidas = 0;
    atual.altura = h;
    novo = classificar(h, atual.estado);
  }
  atual.dias = novo == ERRO ? -1 : preverDiasParaCorte(atual.altura, lux);

  printf("  [A] h=%.1fcm lux=%.0f -> %s | corte em %d d | %s | fila=%d\n",
         atual.altura, lux, NOME_ESTADO[novo], atual.dias,
         conectado ? "ONLINE" : "WIFI-OFF", filaQtd);

  if (novo != atual.estado) {
    printf("    [EVENTO] %s -> %s\n", NOME_ESTADO[atual.estado], NOME_ESTADO[novo]);
    atual.estado = novo;
    atual.seq++;
    atual.ms = agora_ms;
    gravarFlash();
    registrarEvento(atual);
  } else if (conectado && agora_ms - ultimoEnvio >= HEARTBEAT_MS) {
    printf("    [HEARTBEAT]\n");
    publicar(atual, 0);
  }
  return atual.estado;
}

void titulo(const char* t) { printf("\n========== %s ==========\n", t); }

int main() {
  printf("SIMULACAO DA LOGICA DO FIRMWARE — entradas injetadas, semente fixa 42\n");
  printf("H_SENSOR=%.0f  LIM_ATENCAO=%.0f  LIM_CORTE=%.0f  HISTERESE=%.0f  FALHAS_MAX=%d\n",
         H_SENSOR, LIM_ATENCAO, LIM_CORTE, HISTERESE, FALHAS_MAX);

  // ---------- S1: condicao normal ----------
  titulo("S1  condicao normal: alvo a 82 cm, luz 48000 lux");
  for (int i = 0; i < 4; i++) ciclo(82, 48000);

  // ---------- S2: proxima ao limite ----------
  titulo("S2  proxima ao limite: alvo a 70 cm (altura 30 = LIM_ATENCAO)");
  int alternancias = 0;
  Estado ant = atual.estado;
  for (int i = 0; i < 10; i++) {
    Estado e = ciclo(70, 48000);
    if (i > 0 && e != ant) alternancias++;
    ant = e;
  }
  printf("  >> alternancias de estado em 10 ciclos: %d\n", alternancias);
  printf("  -- agora 71 cm (altura 29), vindo de ATENCAO: histerese deve manter\n");
  for (int i = 0; i < 3; i++) ciclo(71, 48000);

  // ---------- S3: deve gerar alerta ----------
  titulo("S3  deve gerar alerta: alvo a 63 cm (altura 37 > LIM_CORTE)");
  for (int i = 0; i < 3; i++) ciclo(63, 48000);

  // ---------- S4: repeticao ----------
  titulo("S4a  repeticao: 5x o ciclo 82 cm -> 63 cm");
  for (int r = 1; r <= 5; r++) {
    printf("  -- repeticao %d: afasta para 82 cm\n", r);
    for (int i = 0; i < 3; i++) ciclo(82, 48000);
    printf("  -- repeticao %d: aproxima para 63 cm\n", r);
    ciclo(63, 48000);
    printf("  >> repeticao %d terminou em %s, seq=%u\n", r, NOME_ESTADO[atual.estado], (unsigned)atual.seq);
  }

  titulo("S4b  dispersao: 10 leituras com alvo parado a 82 cm");
  float amostras[10];
  for (int i = 0; i < 10; i++) {
    ciclo(82, 48000);
    amostras[i] = atual.altura;
  }
  float mn = amostras[0], mx = amostras[0];
  for (float a : amostras) { mn = min(mn, a); mx = max(mx, a); }
  float ord[10]; memcpy(ord, amostras, sizeof(ord)); sort(ord, ord + 10);
  printf("  >> minimo %.2f  maximo %.2f  mediana %.2f  AMPLITUDE %.2f cm\n",
         mn, mx, (ord[4] + ord[5]) / 2, mx - mn);
  printf("  >> histerese do firmware: %.1f cm -> %s\n", HISTERESE,
         (mx - mn) < HISTERESE ? "amplitude menor que a histerese (adequada)"
                               : "amplitude MAIOR que a histerese (insuficiente)");

  titulo("S4c  pico de ruido: alvo a 82 cm com um eco espurio a 8 cm por ciclo");
  for (int i = 0; i < 3; i++) ciclo(82, 48000, 0.5, true);
  printf("  >> a mediana de 5 amostras descarta o pico: altura segue em ~18 cm\n");

  // ---------- S5: adversas ----------
  titulo("S5a  adversa: leitura invalida (sensor apontado para o vazio)");
  for (int i = 0; i < 4; i++) {
    ciclo(-1, 48000);
    printf("       falhas consecutivas: %d/%d\n", falhasSeguidas, FALHAS_MAX);
  }
  printf("  -- alvo recolocado a 82 cm\n");
  for (int i = 0; i < 2; i++) ciclo(82, 48000);

  titulo("S5b  adversa: perda de comunicacao");
  printf("  -- rede cai\n");
  conectado = false;
  printf("  -- com a rede caida, alvo vai de 82 -> 70 -> 63 cm\n");
  for (int i = 0; i < 2; i++) ciclo(82, 48000);
  for (int i = 0; i < 2; i++) ciclo(70, 48000);
  for (int i = 0; i < 2; i++) ciclo(63, 48000);
  printf("  >> eventos guardados na fila: %d\n", filaQtd);
  printf("  -- rede volta\n");
  conectado = true;
  enviarFila();
  printf("  >> fila apos reconexao: %d\n", filaQtd);

  titulo("S5c  adversa: luminosidade fora da faixa (LDR coberto)");
  for (int i = 0; i < 2; i++) ciclo(82, 300);
  printf("  >> previsao suspensa (-1), classificacao da altura inalterada\n");

  titulo("S6  persistencia: reinicio do no com estado CORTE gravado");
  printf("  -- alvo a 63 cm ate entrar em CORTE\n");
  for (int i = 0; i < 2; i++) ciclo(63, 48000);
  printf("  >> gravado na flash: estado=%s seq=%u\n", NOME_ESTADO[flash_estado], (unsigned)flash_seq);
  reiniciar();
  printf("  -- primeiros ciclos depois do boot, alvo ainda a 63 cm\n");
  for (int i = 0; i < 3; i++) ciclo(63, 48000);
  printf("  >> seq apos o boot: %u (sem evento falso se nao houve [EVENTO] acima)\n", (unsigned)atual.seq);

  return 0;
}
