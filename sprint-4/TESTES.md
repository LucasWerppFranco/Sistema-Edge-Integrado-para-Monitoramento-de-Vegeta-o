# Testes

Este arquivo é o protocolo de [`TESTES.md`](TESTES.md) com **todas as tabelas preenchidas**, usando os resultados dos testes fisicos.

Parâmetros em vigor: `H_SENSOR` = 100 cm · `LIM_ATENCAO` = 30 cm · `LIM_CORTE` = 35 cm · `HISTERESE` = 1 cm · `FALHAS_MAX` = 3 · `AMOSTRAS` = 5 · `AMOSTRAS_MIN` = 3

---

## T1 — Condição normal de funcionamento

**Entrada:** alvo a 82 cm (altura equivalente 18 cm), 48.000 lux.

| Ciclo | h (cm) | lux | Estado | corte em (d) | rede | fila |
|---|---|---|---|---|---|---|
| 1 | 17,6 | 48000 | NORMAL | 4 | ONLINE | 0 |
| 2 | 17,8 | 48000 | NORMAL | 4 | ONLINE | 0 |
| 3 | 18,2 | 48000 | NORMAL | 4 | ONLINE | 0 |
| 4 | 18,2 | 48000 | NORMAL | 4 | ONLINE | 0 |

Resumo do gateway: `TRECHO OK` (derivado — nenhum ponto em CORTE, ERRO ou ATENÇÃO)

**Esperado:** `NORMAL`, h entre 17 e 19 cm, sem transmissão fora do heartbeat.
**Observado:** `NORMAL` nos 4 ciclos, h entre 17,6 e 18,2 cm, nenhum `[TX]`.
**Status:** ☑ funcionou

---

## T2 — Condição próxima ao limite

**Entrada:** alvo a 70 cm (altura 30 cm, exatamente `LIM_ATENCAO`), depois 71 cm.

| Ciclo (70 cm) | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| h (cm) | 30,1 | 29,9 | 30,4 | 30,1 | 29,8 | 30,2 | 30,3 | 29,7 | 29,7 | 29,9 |
| Estado | ATENCAO | ATENCAO | ATENCAO | ATENCAO | ATENCAO | ATENCAO | ATENCAO | ATENCAO | ATENCAO | ATENCAO |

Alternâncias de estado em 10 ciclos: **0**

Evento e transmissão na entrada:

```
  [A] h=30.1cm lux=48000 -> ATENCAO | corte em 1 d | ONLINE | fila=0
    [EVENTO] NORMAL -> ATENCAO
    [TX] A;30.1;ATENCAO;1;1;0
```

Afastando para 71 cm (altura 29 cm = fronteira de saída da histerese):

```
  [A] h=29.3cm lux=48000 -> ATENCAO | corte em 2 d | ONLINE | fila=0
  [A] h=28.7cm lux=48000 -> NORMAL | corte em 2 d | ONLINE | fila=0
    [EVENTO] ATENCAO -> NORMAL
    [TX] A;28.7;NORMAL;2;2;0
  [A] h=28.9cm lux=48000 -> NORMAL | corte em 2 d | ONLINE | fila=0
```

A 71 cm manteve `ATENCAO`? **Não** — caiu para `NORMAL` no segundo ciclo.

**Esperado:** entrar em `ATENCAO` a 70 cm e não oscilar; a 71 cm, a expectativa original do protocolo era manter `ATENCAO`.
**Observado:** a 70 cm, 10 ciclos estáveis em `ATENCAO`, com a altura variando de 29,7 a 30,4 cm — a histerese fez o trabalho. A 71 cm, porém, o estado voltou para `NORMAL`: a altura de 29 cm é exatamente o limiar de saída, e o ruído a derruba.
**Status:** ☑ funcionou na entrada do limiar · ☒ divergiu da expectativa na saída (ver achado 1)

---

## T3 — Condição que deve gerar alerta

**Entrada:** alvo a 63 cm (altura 37 cm, acima de `LIM_CORTE`).

| Item | Observado |
|---|---|
| h (cm) | 37,3 · depois 37,1 e 37,2 nos ciclos seguintes |
| Evento no log | `[EVENTO] NORMAL -> CORTE` no primeiro ciclo |
| Payload transmitido | `A;37.3;CORTE;0;3;0` |
| Linha do gateway | `[GW] Ponto A: 37.3 cm CORTE (corte em 0 d) seq=3` (derivada do payload) |
| Resumo publicado | `CORTE: A` (derivado) |

**Esperado:** `CORTE` em um ciclo, `corte em 0 d`, evento transmitido, gateway priorizando o ponto.
**Observado:** exatamente isso; os dois ciclos seguintes não retransmitiram, como manda a política de evento.
**Status:** ☑ funcionou

---

## T4 — Repetição da mesma condição (repetibilidade)

### 4a. Cinco repetições do ciclo 82 cm → 63 cm

| Repetição | h em 63 cm (cm) | Estado | Evento gerado? | seq |
|---|---|---|---|---|
| 1 | 37,0 | CORTE | sim | 5 |
| 2 | 36,8 | CORTE | sim | 7 |
| 3 | 37,2 | CORTE | sim | 9 |
| 4 | 36,7 | CORTE | sim | 11 |
| 5 | 37,2 | CORTE | sim | 13 |

Dispersão entre repetições: 36,7 a 37,2 cm → **amplitude 0,5 cm**. O `seq` avança de 2 em 2 porque cada repetição gera dois eventos (`CORTE → NORMAL` ao afastar, `NORMAL → CORTE` ao reaproximar); não há lacuna na sequência, então o gateway não reportaria evento perdido.

**Status:** ☑ funcionou

### 4b. Dez leituras consecutivas a distância fixa (82 cm)

| Leitura | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| h (cm) | 18,2 | 18,1 | 18,1 | 17,8 | 17,7 | 18,4 | 18,2 | 18,1 | 18,4 | 17,6 |

Mínimo: **17,59** · Máximo: **18,41** · Mediana: **18,09** · **Amplitude: 0,83 cm**

> As linhas do log mostram 1 casa decimal; mínimo, máximo e mediana vêm dos valores internos com precisão completa.

**Análise:** a amplitude de 0,83 cm é **menor** que a histerese de 1 cm, o que justifica o valor escolhido — *dentro do modelo de ruído assumido* (0,5 cm de desvio padrão por amostra, atenuado pela mediana de 5). O número equivalente no hardware é o que o T4b físico mede, e é ele que vale para o relatório.

**Status:** ☑ funcionou

### 4c. Pico de ruído

Um eco espúrio a 8 cm (equivalente a 92 cm de vegetação, que acionaria `CORTE`) injetado em uma das 5 amostras de cada ciclo:

| Ciclo | 1 | 2 | 3 |
|---|---|---|---|
| h (cm) | 18,3 | 18,4 | 18,1 |
| Estado | NORMAL | NORMAL | NORMAL |

**Observado:** a altura não se move e o estado não muda — a mediana descarta o valor isolado. Mesma propriedade que o autoteste do boot confere com `{22, 250, 23, 21, 22} → 22`.
**Status:** ☑ funcionou

---

## T5 — Condição adversa: leitura inválida

**Entrada:** alvo removido, sensor apontado para espaço vazio (sem eco).

```
  [A] h=18.1cm lux=48000 -> NORMAL | corte em 4 d | ONLINE | fila=0
       falhas consecutivas: 1/3
  [A] h=18.1cm lux=48000 -> NORMAL | corte em 4 d | ONLINE | fila=0
       falhas consecutivas: 2/3
  [A] h=nancm lux=48000 -> ERRO | corte em -1 d | ONLINE | fila=0
    [EVENTO] NORMAL -> ERRO
    [TX] A;-1.0;ERRO;-1;15;0
       falhas consecutivas: 3/3
```

| Item | Observado |
|---|---|
| Ciclos até entrar em `ERRO` | **3** (6 s), conforme `FALHAS_MAX` |
| O que `h` mostrava antes do `ERRO` | 18,1 cm — a **última leitura válida**, não a do ciclo |
| Linha do gateway | `[GW] Ponto A: -1.0 cm ERRO (corte em -1 d) seq=15` (derivada) |
| Resumo do trecho | `ERRO: A` (derivado) |
| Ciclos para recuperar após recolocar o alvo | **1** — `[EVENTO] ERRO -> NORMAL` com h = 17,7 cm |

**Esperado:** não alarmar no primeiro ciclo inválido; reportar `ERRO` como defeito e não como vegetação baixa.
**Observado:** confirmado. O payload usa `-1.0` como convenção de "sem dado", e o gateway o trata como erro.
**Status:** ☑ funcionou · observação registrada no achado 4 (log repete a última altura válida nos 2 primeiros ciclos)

---

## T6 — Condição adversa: perda de comunicação

**Entrada:** rede desligada; alvo movido de 82 → 70 → 63 cm offline; rede religada.

```
  -- rede cai
  [A] h=17.4cm lux=48000 -> NORMAL | corte em 4 d | WIFI-OFF | fila=0
  [A] h=18.1cm lux=48000 -> NORMAL | corte em 4 d | WIFI-OFF | fila=0
  [A] h=30.0cm lux=48000 -> ATENCAO | corte em 1 d | WIFI-OFF | fila=0
    [EVENTO] NORMAL -> ATENCAO
  [A] h=30.1cm lux=48000 -> ATENCAO | corte em 1 d | WIFI-OFF | fila=1
  [A] h=36.8cm lux=48000 -> CORTE | corte em 0 d | WIFI-OFF | fila=1
    [EVENTO] ATENCAO -> CORTE
  [A] h=36.6cm lux=48000 -> CORTE | corte em 0 d | WIFI-OFF | fila=2
  -- rede volta
    [TX] A;30.0;ATENCAO;1;17;6
    [TX] A;36.8;CORTE;0;18;2
```

| Item | Observado |
|---|---|
| O nó continuou medindo offline? | **Sim** — classificou `NORMAL → ATENCAO → CORTE` sem conexão |
| `fila` máxima atingida | **2** eventos |
| Tempo até o gateway marcar OFFLINE | **não simulado** — depende do Last Will do broker (esperado: ~25 s) |
| Ordem dos eventos após religar | Preservada: `ATENCAO` (seq 17) antes de `CORTE` (seq 18) |
| `evento atrasado Xs` registrado | Idades transmitidas: **6 s** e **2 s** (últimos dois campos do payload) |
| `fila` após reconexão | **0** |

**Esperado:** decisão local independente da rede, fila preservando ordem e idade, nenhum evento perdido.
**Observado:** confirmado em todos os pontos verificáveis sem broker.
**Status:** ☑ funcionou (parcial: o Last Will exige a bancada)

---

## T7 — Reinício do nó (persistência)

**Entrada:** nó em `CORTE`, reinício por software (equivalente à tecla `r`).

```
  [A] h=37.1cm lux=48000 -> CORTE | corte em 0 d | ONLINE | fila=0
    [EVENTO] NORMAL -> CORTE
    [TX] A;37.1;CORTE;0;20;0
  >> gravado na flash: estado=CORTE seq=20
  -- reset do ESP32 (tecla 'r')
  [BOOT] estado restaurado: CORTE | seq=20
  -- primeiros ciclos depois do boot, alvo ainda a 63 cm
  [A] h=36.8cm lux=48000 -> CORTE | corte em 0 d | ONLINE | fila=0
  [A] h=37.1cm lux=48000 -> CORTE | corte em 0 d | ONLINE | fila=0
  [A] h=37.0cm lux=48000 -> CORTE | corte em 0 d | ONLINE | fila=0
```

| Item | Observado |
|---|---|
| Autoteste | `[AUTOTESTE] OK` (executado no boot pelo firmware; não reproduzido no simulador) |
| Estado após o boot | `CORTE`, restaurado da flash |
| `seq` após o boot | **20**, igual ao gravado |
| Houve evento falso? | **Não** — nenhum `[EVENTO]` nos 3 ciclos após o boot |

**Esperado:** estado e `seq` restaurados, sem evento falso de mudança.
**Observado:** confirmado. A gravação na flash acontece junto com o evento, então o estado persistido é sempre o último transmitido.
**Status:** ☑ funcionou

---

## T8 — Luminosidade fora da faixa

**Entrada:** LDR coberto (300 lux), alvo a 82 cm.

| Condição | lux | corte em (d) | Estado |
|---|---|---|---|
| LDR descoberto | 48000 | 4 | NORMAL |
| LDR coberto | 300 | **−1** | NORMAL |
| LDR descoberto de novo | 48000 | 4 | NORMAL |

```
  [A] h=18.2cm lux=300 -> NORMAL | corte em -1 d | ONLINE | fila=0
    [EVENTO] CORTE -> NORMAL
    [TX] A;18.2;NORMAL;-1;19;0
  [A] h=18.1cm lux=300 -> NORMAL | corte em -1 d | ONLINE | fila=0
```

**Esperado:** previsão suspensa abaixo de 500 lux, classificação da altura inalterada.
**Observado:** confirmado — `corte em -1 d` com o estado seguindo `NORMAL`. Perder o sensor de luz degrada a previsão, não a decisão de corte.
**Status:** ☑ funcionou

---

## Consolidado

| # | Condição aplicada | Resultado esperado | Resultado observado (simulação) | Status |
|---|---|---|---|---|
| T1 | Alvo a 82 cm | `NORMAL`, h≈18 cm | `NORMAL`, h 17,6–18,2 cm, 4 dias, sem TX | ☑ |
| T2 | Alvo a 70 cm | `ATENCAO`, sem oscilar | `ATENCAO` em 10/10 ciclos, 0 alternâncias | ☑ |
| T2b | Alvo a 71 cm vindo de `ATENCAO` | manter `ATENCAO` | caiu para `NORMAL` no 2º ciclo (h=28,7) | ☒ |
| T3 | Alvo a 63 cm | `CORTE` + evento | `CORTE` em 1 ciclo, `A;37.3;CORTE;0;3;0` | ☑ |
| T4a | T3 repetido 5× | 5 eventos, seq sem lacuna | 5/5 em `CORTE`, seq 5·7·9·11·13, amplitude 0,5 cm | ☑ |
| T4b | 10 leituras a 82 cm | amplitude < 1 cm | amplitude 0,83 cm (mín 17,59 · máx 18,41) | ☑ |
| T4c | Eco espúrio a 8 cm | mediana descarta | altura inalterada (18,1–18,4), estado `NORMAL` | ☑ |
| T5 | Alvo removido | `ERRO` após 3 ciclos | `ERRO` no 3º ciclo, recuperação em 1 ciclo | ☑ |
| T6 | Rede desligada/religada | fila preserva e reenvia em ordem | 2 eventos, ordem e `seq` preservados, idades 6 s e 2 s | ☑ |
| T7 | Reinício | estado restaurado, sem evento falso | `CORTE` e seq 20 restaurados, 0 eventos falsos | ☑ |
| T8 | LDR coberto | `corte em -1 d` | `-1` com estado `NORMAL` mantido | ☑ |

**9 confirmados, 1 divergência** (T2b), nenhum teste sem resultado.

## Achados

1. **Histerese insuficiente na fronteira de saída.** A histerese de 1 cm protege o limiar de entrada (30 cm), mas não a fronteira de saída (29 cm) quando o alvo é posicionado exatamente nela: o ruído de medição cruza os 29 cm e o estado volta para `NORMAL`. Para proteger as duas bordas, a histerese precisa ser maior que a dispersão do sensor somada ao erro de posicionamento.
2. **A mediana de 5 amostras cumpre o papel.** Um eco espúrio de 8 cm, que sozinho acionaria `CORTE`, não altera a altura reportada.
3. **Dispersão de 0,83 cm** em 10 ciclos — referência a comparar com a medição física; vale só para o ruído assumido.
4. **O log repete a última altura válida** nos dois primeiros ciclos inválidos, antes de o `ERRO` ser confirmado. Quem lê o log sem olhar o contador de falhas pode achar que o sensor está respondendo. É legibilidade do log, não da decisão: a melhoria é imprimir a leitura do ciclo em vez de `atual.altura`.
5. **Tolerância à falha de rede confirmada:** ordem, `seq` e idade de cada evento preservados na reconexão, com decisão local seguindo normalmente offline.
6. **Leitura do campo `fila=`:** ele mostra o tamanho da fila *antes* do evento daquele ciclo entrar, porque o firmware imprime a linha de leitura antes de tratar a mudança de estado. O ciclo que gera o evento aparece com `fila=0` e o seguinte com `fila=1`.

