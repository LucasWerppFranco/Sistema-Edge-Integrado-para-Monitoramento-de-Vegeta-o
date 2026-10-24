#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
using namespace std;

const int mqtt_port = 1883;
const float H_SENSOR = 100.0;
const float DIST_MIN = 5.0;
const float ALTURA_POS_CORTE = 10.0;
const float LIM_ATENCAO = 30.0;
const float LIM_CORTE   = 35.0; // Estado muda para CORTE ao atingir 35cm
const float HISTERESE   = 1.0;
const int AMOSTRAS     = 5;
const int AMOSTRAS_MIN = 3;
const int FALHAS_MAX   = 3;
const unsigned long LEITURA_MS   = 2000;
const unsigned long HEARTBEAT_MS = 30000;
const unsigned long RETENTAR_MS  = 5000;
const float GAMMA = 0.7;
const float RL10  = 50;
const int FILA_MAX = 20;

enum Estado : uint8_t { NORMAL, ATENCAO, CORTE, ERRO };
const char* NOME_ESTADO[] = {"NORMAL", "ATENCAO", "CORTE", "ERRO"};

float mediana(float* v, int n) {
  std::sort(v, v + n);
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

Estado classificar(float h, Estado anterior) {
  float limAtencao = LIM_ATENCAO, limCorte = LIM_CORTE;
  if (anterior == ATENCAO || anterior == CORTE) limAtencao -= HISTERESE;
  if (anterior == CORTE) limCorte -= HISTERESE;
  if (h >= limCorte) return CORTE;
  if (h >= limAtencao) return ATENCAO;
  return NORMAL;
}

float avaliarLuminosidade(float lux) {
  if (lux < 500 || lux > 120000) return 0.0;
  if (lux >= 1000 && lux <= 10000) return 0.4;
  if (lux >= 40000 && lux <= 70000) return 1.0;
  if (lux >= 20000 && lux <= 60000) return 0.8;
  return 0.6;
}

int preverDiasParaCorte(float altura, float lux) {
  if (isnan(altura)) return -1;
  if (altura >= LIM_CORTE) return 0;

  float sL = avaliarLuminosidade(lux);
  if (sL == 0) return -1;

  int diasCiclo = sL >= 0.9 ? 5 : sL >= 0.7 ? 7 : sL >= 0.5 ? 10 : sL >= 0.3 ? 15 : 20;
  float taxa = (LIM_CORTE - ALTURA_POS_CORTE) / diasCiclo;
  return ceil((LIM_CORTE - altura) / taxa);
}
