# Montagem física — versão simplificada

Guia para montar os dois dispositivos desta pasta em hardware real (sem LCD e sem o switch LINK).

- **Ponto A / Ponto B** (`no_sensor/`): o nó que mede a vegetação. São duas placas com o mesmo firmware, mudando só `NODE_ID`.
- **Gateway** (`gateway/`): a central que recebe os pontos e acende os LEDs do trecho.

Ao todo: 3 ESP32 para o sistema completo, ou 2 se você montar só um ponto.

---

## 1. Lista de peças

**Para cada nó (Ponto A e Ponto B):**

| Qtd | Peça | Observação |
|---|---|---|
| 1 | ESP32 DevKit (v1 ou DevKit-C) | Qualquer um com os GPIOs 4, 5, 14, 18 e 34 acessíveis |
| 1 | HC-SR04 | Sensor ultrassônico de distância, o dos dois "olhos" |
| 1 | DHT22 (AM2302) | Temperatura e umidade. A versão em módulo (3 pinos) é mais fácil |
| 1 | Módulo LDR com saída analógica | Precisa ter pino `AO`; o LDR cru sozinho não serve sem divisor |
| 1 | LED vermelho | Indica CORTE NECESSÁRIO |
| 1 | Resistor 220 Ω | Em série com o LED |
| — | Protoboard e jumpers macho-macho | |

**Para o gateway:**

| Qtd | Peça | Observação |
|---|---|---|
| 1 | ESP32 DevKit | |
| 1 | LED vermelho | Algum ponto precisa de corte |
| 1 | LED amarelo | Algum ponto offline ou com sensor em erro |
| 2 | Resistor 220 Ω | Um para cada LED |

Se não tiver 220 Ω, qualquer valor entre 150 Ω e 1 kΩ funciona (quanto maior, mais fraco o brilho).

---

## 2. Antes de começar: duas regras que evitam 90% dos problemas

**O LED tem lado.** A perna **longa** é o positivo (ânodo, vai para o GPIO). A perna **curta** é o negativo (cátodo, vai para o resistor e daí ao GND). Olhando o corpo do LED, o lado do cátodo também tem um chanfro reto. LED invertido não acende e não queima — se não acender, vire.

```
         LED visto de lado
              ___
             /   \
            |     |
             \___/
              | |
    perna     | |     perna
    LONGA ────┘ └──── CURTA (lado chanfrado)
    (+) ânodo        (−) cátodo
    vai no GPIO      vai no resistor → GND
```

**O resistor não tem lado.** Pode ligar em qualquer sentido.

---

## 3. Montagem do nó (Ponto A e Ponto B)

### 3.1 Visão geral dos fios

```
                        ESP32 DevKit
                  ┌───────────────────────┐
                  │                       │
   HC-SR04        │                       │
  ┌──────────┐    │                       │
  │ VCC  ●───┼────┤ 5V                    │   vermelho
  │ TRIG ●───┼────┤ GPIO 5                │   laranja
  │ ECHO ●───┼────┤ GPIO 18               │   roxo
  │ GND  ●───┼──┐ │                       │   preto
  └──────────┘  │ │                       │
                │ │                       │
   DHT22        │ │                       │
  ┌──────────┐  │ │                       │
  │ VCC  ●───┼──┼─┤ 3V3                   │   vermelho
  │ DATA ●───┼──┼─┤ GPIO 4                │   verde
  │ GND  ●───┼──┤ │                       │   preto
  └──────────┘  │ │                       │
                │ │                       │
   LDR (módulo) │ │                       │
  ┌──────────┐  │ │                       │
  │ VCC  ●───┼──┼─┤ 3V3                   │   vermelho
  │ AO   ●───┼──┼─┤ GPIO 34               │   verde
  │ GND  ●───┼──┤ │                       │   preto
  └──────────┘  │ │                       │
                │ │                       │
   LED vermelho │ │                       │
    ┌───┐  220Ω │ │                       │
  ──┤ ● ├──[▭]──┼─┤ GPIO 14  (perna longa)│   verde
    └───┘       │ │                       │
                │ │                       │
                └─┤ GND  (todos juntos)   │   preto
                  └───────────────────────┘
```

Os três `GND` dos sensores mais o do LED vão todos para o mesmo GND do ESP32. Na protoboard, use **uma fileira inteira como trilha de GND** e ligue tudo nela — é mais limpo que empilhar jumpers no mesmo furo da placa.

### 3.2 Tabela fio por fio

| # | De (peça e pino) | Para (ESP32) | Cor sugerida |
|---|---|---|---|
| 1 | HC-SR04 · VCC | **5V** | vermelho |
| 2 | HC-SR04 · TRIG | GPIO 5 | laranja |
| 3 | HC-SR04 · ECHO | GPIO 18 | roxo |
| 4 | HC-SR04 · GND | GND | preto |
| 5 | DHT22 · VCC (ou `+`) | 3V3 | vermelho |
| 6 | DHT22 · DATA (ou `OUT`, `SDA`, `S`) | GPIO 4 | verde |
| 7 | DHT22 · GND (ou `−`) | GND | preto |
| 8 | LDR · VCC | 3V3 | vermelho |
| 9 | LDR · AO (saída analógica) | GPIO 34 | verde |
| 10 | LDR · GND | GND | preto |
| 11 | LED · perna longa (ânodo) | GPIO 14 | verde |
| 12 | LED · perna curta (cátodo) | → resistor 220 Ω → GND | cinza / preto |

### 3.3 Detalhes que importam

- **HC-SR04 em 5V, não em 3V3.** Com 3,3 V ele costuma não emitir pulso nenhum. O pino ECHO devolve 5 V para o GPIO 18; na prática os ESP32 toleram isso e é como quase todo mundo monta, mas se você quiser ficar dentro da especificação, use um divisor de tensão (dois resistores de 1 kΩ e 2 kΩ) no ECHO.
- **DHT22 de 4 pinos** (o sensor cru, sem plaquinha) precisa de um resistor de **10 kΩ entre VCC e DATA**. O de 3 pinos em módulo já tem esse resistor embutido — nesse caso não acrescente nada. No sensor cru, o 4º pino não é usado.
- **GPIO 34 é só entrada.** É de propósito: ele fica no ADC1, o único que continua funcionando com o Wi-Fi ligado. Não troque por 32/33 sem necessidade, e nunca por um pino do ADC2 (0, 2, 4, 12–15, 25–27), que para de ler quando o Wi-Fi sobe.
- **GPIO 4 e 5** são seguros para o DHT22 e o TRIG. Evite 6–11 (ligados à memória flash) e 0/2/12/15 (afetam o boot).

### 3.4 Posição física do sensor — isso define a leitura

O firmware supõe que o HC-SR04 está **a 100 cm do chão, apontado para baixo**, e calcula a altura da vegetação assim:

```
altura da grama = 100 cm − distância medida
```

```
   HC-SR04 ─┐  ← fixado a 100 cm do solo, olhando para baixo
            │
            │  distância medida pelo sensor (ex.: 82 cm)
            │
            ▼
         ▒▒▒▒▒▒▒   ← topo da vegetação  (altura = 100 − 82 = 18 cm)
   ────────────────  solo
```

Essa altura de 100 cm é a constante `H_SENSOR` no firmware. Duas consequências práticas:

1. Se você montar o sensor em outra altura, **ajuste `H_SENSOR`** para a altura real em centímetros, senão todas as leituras saem erradas.
2. Qualquer coisa a **mais de 100 cm** do sensor dá altura negativa, e o firmware descarta como leitura inválida. Testando na bancada, se o sensor estiver apontado para o teto ou para o vazio do quarto, **todas** as leituras serão descartadas e o estado vai para `ERRO` — mesmo com o sensor perfeito. Para testar na mesa, ponha a mão ou uma caixa a uns 20–40 cm na frente dele.

Para referência, com `H_SENSOR` = 100 cm:

| Distância medida | Altura calculada | Estado |
|---|---|---|
| 82 cm | 18 cm | NORMAL |
| 73 cm | 27 cm | ATENCAO |
| 66 cm | 34 cm | CORTE |
| mais de 100 cm | negativa | descartada como inválida |
| menos de 5 cm | — | descartada (perto demais) |

### 3.5 Diferença entre o Ponto A e o Ponto B

A montagem é idêntica. A única diferença está na primeira linha de configuração do firmware:

- Ponto A: `#define NODE_ID "A"`
- Ponto B: `#define NODE_ID "B"`

Grave cada um na sua placa. Dois nós com o mesmo `NODE_ID` publicam no mesmo tópico e o gateway vê os dois como um só ponto.

---

## 4. Montagem do gateway

Só dois LEDs. Nenhum sensor.

```
                        ESP32 DevKit
                  ┌───────────────────────┐
   LED vermelho   │                       │
    ┌───┐  220Ω   │                       │
  ──┤ ● ├──[▭]────┤ GPIO 14               │   "algum ponto precisa de corte"
    └───┘         │                       │
                  │                       │
   LED amarelo    │                       │
    ┌───┐  220Ω   │                       │
  ──┤ ● ├──[▭]────┤ GPIO 27               │   "algum ponto offline ou em erro"
    └───┘         │                       │
                  │                       │
          ────────┤ GND                   │
                  └───────────────────────┘
```

| # | De | Para | Cor sugerida |
|---|---|---|---|
| 1 | LED vermelho · perna longa | GPIO 14 | verde |
| 2 | LED vermelho · perna curta | → 220 Ω → GND | cinza / preto |
| 3 | LED amarelo · perna longa | GPIO 27 | verde |
| 4 | LED amarelo · perna curta | → 220 Ω → GND | cinza / preto |

O resto do trabalho do gateway aparece no Serial Monitor: qual ponto está em que estado, quem caiu, eventos perdidos e o resumo do trecho.

---

## 5. Gravando o firmware

### 5.1 Arduino IDE

1. **Suporte ao ESP32:** em *Arquivo → Preferências → URLs adicionais para gerenciadores de placas*, cole
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   e depois instale "esp32 by Espressif Systems" em *Ferramentas → Placa → Gerenciador de placas*.
2. **Placa:** *Ferramentas → Placa → ESP32 Arduino → ESP32 Dev Module*.
3. **Porta:** a que aparecer ao plugar o cabo (`/dev/ttyUSB0` ou `/dev/ttyACM0` no Linux).
4. **Bibliotecas** (*Ferramentas → Gerenciar bibliotecas*):
   - nó: `PubSubClient` (Nick O'Leary) e `DHT sensor library` (Adafruit, com o `Adafruit Unified Sensor` que ela puxa);
   - gateway: só `PubSubClient`.
5. **Pasta do sketch:** o Arduino IDE exige que o arquivo `.ino` esteja numa pasta com o mesmo nome — `no_sensor/no_sensor.ino` e `gateway/gateway.ino` já estão assim. Abra o `.ino` direto.

No Linux, se a porta não aparecer ou der erro de permissão:

```
sudo usermod -aG uucp,dialout $USER    # depois faça logout e login
```

### 5.2 Preencher a rede antes de gravar

Os dois firmwares vão para o repositório com as credenciais **em branco**, de propósito, para não publicar a senha do Wi-Fi. Preencha na sua cópia local antes de gravar:

```c
const char* ssid = "";       // nome da sua rede
const char* password = "";   // senha da sua rede
```

Nó em `no_sensor/no_sensor.ino`, gateway em `gateway/gateway.ino`, logo no começo do arquivo.

**O ESP32 só funciona em Wi-Fi de 2,4 GHz.** Ele não enxerga redes de 5 GHz. Se o seu roteador usa o mesmo nome para as duas faixas, o jeito mais rápido de testar é o hotspot do celular em 2,4 GHz.

Depois de preencher, os arquivos vão aparecer como modificados no `git status`. Cuidado para não commitar a senha.

### 5.3 Serial Monitor

Abra em **9600 baud** (é o valor usado nos dois firmwares). Em outra velocidade sai texto embaralhado.

---

## 6. Como saber se funcionou

**No boot do nó**, em ordem:

```
[AUTOTESTE] OK
[BOOT] Ponto A | estado restaurado: NORMAL | seq=0
Conectando....
WiFi connected
192.168.0.42
Exito na conexao:
Cliente edgeveg-A-XX:XX:XX:XX:XX:XX conectado ao broker
```

- `[AUTOTESTE] OK` confirma a lógica de mediana, classificação, histerese e previsão.
- Se o Wi-Fi falhar, ele lista as redes de 2,4 GHz que consegue ver, o que mostra na hora se o problema é nome errado, rede 5 GHz ou sinal fraco.

**Durante a operação**, uma linha por leitura:

```
[A] h=18.0cm T=25.0 U=65 lux=48000 -> NORMAL | corte em 4 d | ONLINE | fila=0
```

O penúltimo campo diz onde a rede está:

| Campo | Significado |
|---|---|
| `ONLINE` | Wi-Fi e broker OK |
| `BROKER-OFF` | Wi-Fi OK, mas o broker MQTT não respondeu |
| `WIFI-OFF` | Nem o Wi-Fi conectou |

**No gateway:**

```
[GW] Ponto A: 18.0 cm NORMAL (corte em 4 d) seq=0
[GW] trecho: TRECHO OK | rede ONLINE
```

**Visão de fora** (opcional): com o `mosquitto-clients` instalado, dá para ver o que chega ao broker, de qualquer computador:

```
mosquitto_sub -h broker.hivemq.com -t 'edgeveg-lwf/#' -v
```

---

## 7. Testando sem esperar a grama crescer

| Teste | O que fazer | O que esperar |
|---|---|---|
| Vegetação normal | Objeto a ~82 cm do sensor | `NORMAL`, LED apagado |
| Atenção | Objeto a ~73 cm | `[EVENTO] NORMAL -> ATENCAO`, gateway mostra `ATENCAO: A` |
| Corte | Objeto a ~66 cm | `[EVENTO] ATENCAO -> CORTE`, LED vermelho aceso no nó **e** no gateway |
| Histerese | Com A em CORTE, ir a ~70,5 cm (altura 29,5 cm) | Continua `CORTE`, sem ficar alternando |
| Sensor com defeito | Desligar o fio do ECHO | 3 ciclos de `leitura de altura invalida`, depois `ERRO` e LED amarelo no gateway |
| Nó fora do ar | Desligar o cabo USB do Ponto B | Gateway mostra `B OFFLINE (…)` em até ~25 s, via Last Will |
| Nó reiniciado | Digitar `r` no Serial Monitor do nó | `[BOOT] estado restaurado: CORTE`, sem evento falso |
| Internet caiu | Tirar o roteador do ar, ou desligar o Wi-Fi | O nó continua medindo e decidindo; os eventos ficam em `fila=N` e saem em ordem quando a rede volta |

---

## 8. Se não funcionar

| Sintoma | Causa provável | O que verificar |
|---|---|---|
| `leitura de altura invalida descartada`, sempre | Nada dentro de 100 cm do sensor, ou ECHO/TRIG trocados | Ponha a mão a 30 cm na frente do sensor; confira TRIG no 5 e ECHO no 18 |
| O mesmo, com a fiação certa | HC-SR04 não dispara com os 3,3 V do TRIG (comum em clones) | Teste outro módulo, ou um level shifter no TRIG |
| `T=nan U=nan` | DHT22 sem resistor de pull-up (no sensor cru de 4 pinos), pino trocado ou sensor em 5 V | 10 kΩ entre VCC e DATA; DATA no GPIO 4; alimentar em 3V3 |
| `lux` sempre 0 ou sempre no máximo | LDR no pino errado, ou módulo sem saída analógica | `AO` no GPIO 34, não o pino digital `DO` |
| LED nunca acende | LED invertido | Perna longa no GPIO, curta no resistor |
| `WIFI-OFF` | Rede 5 GHz, nome ou senha errados, sinal fraco | Veja a lista de redes que o nó imprime no boot; teste com hotspot 2,4 GHz |
| `BROKER-OFF` | Porta 1883 bloqueada na rede, ou sem internet | Teste o `mosquitto_sub` no PC na mesma rede; troque para `test.mosquitto.org` |
| Serial embaralhado | Baud errado | 9600 |
| Gateway não mostra nada | `TOPIC_BASE` diferente entre nó e gateway | O texto tem que ser idêntico nos dois firmwares |
| Altura sempre errada por um valor fixo | `H_SENSOR` diferente da altura real de instalação | Meça a altura do sensor até o solo e ajuste a constante |
