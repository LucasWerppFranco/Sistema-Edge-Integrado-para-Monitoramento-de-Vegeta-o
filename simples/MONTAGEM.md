# Montagem física — versão simplificada

Guia para montar os dois dispositivos desta pasta em hardware real.

- **Ponto A / Ponto B** (`no_sensor/`): o nó que mede a altura da vegetação. O mesmo firmware roda nos dois, mudando só `NODE_ID`.
- **Gateway** (`gateway/`): a central que recebe os pontos, detecta quem caiu e publica o resumo do trecho.

Esta versão não tem LCD, não tem switch, **não tem LED e não usa DHT22**. A saída do sistema é o **Serial Monitor** de cada placa e os **tópicos MQTT**.

---

## 1. Lista de peças

**Para cada nó (Ponto A e Ponto B):**

| Qtd | Peça | Observação |
|---|---|---|
| 1 | ESP32 DevKit (v1 ou DevKit-C) | Precisa dos GPIOs 5, 18 e 34 acessíveis |
| 1 | HC-SR04 | Sensor ultrassônico de distância, o dos dois "olhos" |
| 1 | Módulo LDR com saída analógica | Precisa ter pino `AO`; só afeta a previsão de dias |
| — | Protoboard e jumpers macho-macho | |

**Para o gateway:**

| Qtd | Peça | Observação |
|---|---|---|
| 1 | ESP32 DevKit | Nenhum componente externo |

O gateway é só a placa e o cabo USB. Todo o resultado dele aparece no Serial Monitor.

---

## 2. Montagem do nó

### 2.1 Visão geral dos fios

```
                        ESP32 DevKit
                  ┌───────────────────────┐
   HC-SR04        │                       │
  ┌──────────┐    │                       │
  │ VCC  ●───┼────┤ 5V                    │   vermelho
  │ TRIG ●───┼────┤ GPIO 5                │   laranja
  │ ECHO ●───┼────┤ GPIO 18               │   roxo
  │ GND  ●───┼──┐ │                       │   preto
  └──────────┘  │ │                       │
                │ │                       │
   LDR (módulo) │ │                       │
  ┌──────────┐  │ │                       │
  │ VCC  ●───┼──┼─┤ 3V3                   │   vermelho
  │ AO   ●───┼──┼─┤ GPIO 34               │   verde
  │ GND  ●───┼──┤ │                       │   preto
  └──────────┘  │ │                       │
                │ │                       │
                └─┤ GND  (os dois juntos) │   preto
                  └───────────────────────┘
```

Na protoboard, use **uma fileira inteira como trilha de GND** e ligue os dois GND nela.

### 2.2 Tabela fio por fio

| # | De (peça e pino) | Para (ESP32) | Cor sugerida |
|---|---|---|---|
| 1 | HC-SR04 · VCC | **5V** | vermelho |
| 2 | HC-SR04 · TRIG | GPIO 5 | laranja |
| 3 | HC-SR04 · ECHO | GPIO 18 | roxo |
| 4 | HC-SR04 · GND | GND | preto |
| 5 | LDR · VCC | 3V3 | vermelho |
| 6 | LDR · AO (saída analógica) | GPIO 34 | verde |
| 7 | LDR · GND | GND | preto |

### 2.3 Detalhes que importam

- **HC-SR04 em 5V, não em 3V3.** Com 3,3 V ele costuma não emitir pulso nenhum. O ECHO devolve 5 V ao GPIO 18; é como quase todo mundo monta, e os ESP32 toleram. Para ficar dentro da especificação, use um divisor (1 kΩ + 2 kΩ) no ECHO.
- **TRIG em 3,3 V.** Alguns clones do HC-SR04 só disparam com 5 V no TRIG. O sintoma é não haver eco nenhum, sempre.
- **GPIO 34 é só entrada**, e é de propósito: fica no ADC1, o único que continua lendo com o Wi-Fi ligado. Não use pinos do ADC2 (0, 2, 4, 12–15, 25–27) para o LDR.
- **Alimentação.** O ESP32 puxa picos de 300–500 mA quando o rádio liga. Cabo USB fino ou hub sem alimentação causa `Brownout detector was triggered` e reinício em loop. Use cabo curto e grosso, direto na porta do computador. Se persistir, um eletrolítico de 470–1000 µF entre `5V` e `GND` resolve.

### 2.4 Posição física do sensor — isso define a leitura

O firmware supõe o HC-SR04 **a 100 cm do chão, apontado para baixo**:

```
   HC-SR04 ─┐  ← fixado a 100 cm do solo, olhando para baixo
            │
            │  distância medida (ex.: 82 cm)
            │
            ▼
         ▒▒▒▒▒▒▒   ← topo da vegetação  (altura = 100 − 82 = 18 cm)
   ────────────────  solo
```

```
altura da vegetação = H_SENSOR (100 cm) − distância medida
```

Duas consequências práticas:

1. Montando o sensor em outra altura, **ajuste `H_SENSOR`** para a altura real, senão toda leitura sai deslocada.
2. Qualquer coisa a **mais de 100 cm** dá altura negativa e é descartada como inválida. Na bancada, com o sensor apontado para o vazio, **todas** as leituras são descartadas e o estado vai para `ERRO` — mesmo com o sensor perfeito. Para testar, use uma caixa ou prancheta na distância desejada.

Com `H_SENSOR` = 100 cm e os limiares atuais (`LIM_ATENCAO` = 30 cm, `LIM_CORTE` = 35 cm):

| Distância medida | Altura calculada | Estado |
|---|---|---|
| 82 cm | 18 cm | NORMAL |
| 71 cm | 29 cm | NORMAL (quase no limite) |
| 70 cm | 30 cm | ATENCAO |
| 66 cm | 34 cm | ATENCAO |
| 65 cm | 35 cm | CORTE |
| 60 cm | 40 cm | CORTE |
| mais de 100 cm | negativa | descartada como inválida |
| menos de 5 cm | — | descartada (perto demais) |

A **histerese de 1 cm** significa que, já em `CORTE`, a altura precisa cair abaixo de 34 cm (distância acima de 66 cm) para voltar a `ATENCAO`; e já em `ATENCAO`, abaixo de 29 cm para voltar a `NORMAL`. É o que evita alternar de estado com a grama parada no limiar.

### 2.5 Diferença entre o Ponto A e o Ponto B

Montagem idêntica. Muda uma linha do firmware:

- Ponto A: `#define NODE_ID "A"`
- Ponto B: `#define NODE_ID "B"`

Dois nós com o mesmo `NODE_ID` publicam no mesmo tópico, e o gateway enxerga os dois como um único ponto.

---

## 3. Montagem do gateway

Nenhum componente externo: só o ESP32 e o cabo USB. O trabalho dele aparece no Serial Monitor — qual ponto está em que estado, quem caiu, eventos perdidos e o resumo do trecho — e no tópico `edgeveg-lwf/rodovia/resumo`.

---

## 4. Gravando o firmware

### 4.1 Arduino IDE

1. **Suporte ao ESP32:** em *Arquivo → Preferências → URLs adicionais para gerenciadores de placas*, cole
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   e instale "esp32 by Espressif Systems" em *Ferramentas → Placa → Gerenciador de placas*.
2. **Placa:** *Ferramentas → Placa → ESP32 Arduino → ESP32 Dev Module*.
3. **Porta:** a que aparecer ao plugar o cabo (`/dev/ttyUSB0` ou `/dev/ttyACM0` no Linux).
4. **Biblioteca:** só `PubSubClient` (Nick O'Leary), nos dois projetos.
5. **Pasta do sketch:** o `.ino` precisa estar numa pasta com o mesmo nome — `no_sensor/no_sensor.ino` e `gateway/gateway.ino` já estão assim.

No Linux, se a porta não aparecer ou der erro de permissão:

```
sudo usermod -aG uucp,dialout $USER    # depois faça logout e login
```

### 4.2 Preencher a rede antes de gravar

Os dois firmwares vão para o repositório com as credenciais **em branco**, para não publicar a senha do Wi-Fi. Preencha na sua cópia local antes de gravar:

```c
const char* ssid = "";       // nome da sua rede
const char* password = "";   // senha da sua rede
```

Nó em `no_sensor/no_sensor.ino`, gateway em `gateway/gateway.ino`, no começo do arquivo.

**O ESP32 só funciona em Wi-Fi de 2,4 GHz** — ele não enxerga redes de 5 GHz. Se o roteador usa o mesmo nome nas duas faixas, o hotspot do celular em 2,4 GHz é o teste mais rápido.

Depois de preencher, os arquivos aparecem como modificados no `git status`. Cuidado para não commitar a senha.

### 4.3 Serial Monitor

Abra em **9600 baud** nos dois projetos. Em outra velocidade sai texto embaralhado. Os caracteres estranhos que aparecem no instante do boot são o bootloader da ROM falando em 115200 — normal.

---

## 5. Como saber se funcionou

**No boot do nó:**

```
[AUTOTESTE] OK
```

Confirma a lógica de mediana, classificação e previsão antes de qualquer leitura. Se sair `FALHOU`, a lógica de decisão foi alterada e está inconsistente.

**Durante a operação**, uma linha a cada 2 segundos:

```
[A] h=18.0cm lux=48000 -> NORMAL | corte em 4 d | ONLINE | fila=0
```

| Campo | Significado |
|---|---|
| `h=` | Altura da vegetação em cm (`nan` = sem leitura válida) |
| `lux=` | Luminosidade, usada só para a previsão de dias |
| `-> ESTADO` | `NORMAL`, `ATENCAO`, `CORTE` ou `ERRO` |
| `corte em N d` | Previsão de dias até atingir 35 cm (`-1` = sem previsão) |
| `ONLINE` / `BROKER-OFF` / `WIFI-OFF` | Onde a rede está |
| `fila=N` | Eventos guardados esperando conexão |

Mudança de estado gera duas linhas a mais:

```
  [EVENTO] NORMAL -> ATENCAO
  [TX] edgeveg-lwf/rodovia/pontoA/estado -> A;30.2;ATENCAO;1;3;0
```

Campos do payload: `id;altura;estado;dias;seq;idade_s`.

**No gateway:**

```
[GW] Ponto A: 30.2 cm ATENCAO (corte em 1 d) seq=3
[GW] trecho: ATENCAO: A | rede ONLINE
```

**Visão de fora** (opcional): de qualquer computador, com `mosquitto-clients` instalado,

```
mosquitto_sub -h broker.hivemq.com -t 'edgeveg-lwf/#' -v
```

---

## 6. Se não funcionar

| Sintoma | Causa provável | O que verificar |
|---|---|---|
| `h=nan` sempre | Nada dentro de 100 cm do sensor, ou ECHO/TRIG trocados | Caixa a 60–80 cm na frente do sensor; TRIG no 5 e ECHO no 18 |
| O mesmo, com a fiação certa | Clone do HC-SR04 que não dispara com 3,3 V no TRIG | Testar outro módulo ou um level shifter |
| `lux` sempre 0 ou sempre no máximo | LDR no pino errado, ou módulo sem saída analógica | `AO` no GPIO 34, não o pino digital `DO` |
| `corte em -1 d` | Luz fora da faixa (abaixo de 500 lux), ou altura inválida | Normal no escuro: a previsão fica suspensa, a classificação continua |
| `E BOD: Brownout detector was triggered` em loop | Alimentação insuficiente | Cabo USB melhor, porta direta, capacitor de 470–1000 µF |
| `WIFI-OFF` | Rede 5 GHz, nome ou senha errados, sinal fraco | Testar com hotspot 2,4 GHz |
| `BROKER-OFF` | Porta 1883 bloqueada, ou Wi-Fi sem internet | `mosquitto_sub` no PC na mesma rede; trocar para `test.mosquitto.org` |
| Serial embaralhado | Baud errado | 9600 |
| Gateway não mostra nada | `TOPIC_BASE` diferente entre nó e gateway | O texto tem que ser idêntico nos dois firmwares |
| `mensagem invalida descartada` no gateway | Nó e gateway com formatos de payload diferentes | Os dois precisam ter o mesmo número de campos (hoje: 6) |
| Altura deslocada por um valor fixo | `H_SENSOR` diferente da altura real de instalação | Medir do sensor ao solo e ajustar a constante |
