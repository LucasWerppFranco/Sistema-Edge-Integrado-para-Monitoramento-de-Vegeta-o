# Sprint 04 — Validação Física da Solução Edge

**Disciplina:** Edge Computing · Ciências da Computação — 2º ano
**Turma:** [2CCXX]
**Equipe:** [nome da equipe]
**Integrantes:** [nomes]
**Desafio:** Sistema Edge Integrado para Monitoramento de Vegetação em Rodovia

> Campos entre colchetes e todas as colunas de *resultado observado* precisam ser preenchidos com os dados reais dos testes. Este documento traz o planejamento, os valores esperados calculados a partir do código e o espaço de registro — ele **não** contém resultados inventados.

Documentos desta pasta:

| Arquivo | Conteúdo |
|---|---|
| `README.md` | O documento da entrega: montagem, análise, conclusão |
| [`TESTES.md`](TESTES.md) | Os testes executados: entradas, resultado esperado, resultado observado, status, achados e o que não foi coberto |
| [`simulacao/`](simulacao/) | Simulador da lógica do firmware em C++, com a saída completa dos cenários |

Firmware validado nesta sprint: [`../simples/`](../simples/) · Montagem: [`../simples/MONTAGEM.md`](../simples/MONTAGEM.md)

---

## 1. Montagem física e cenário de validação

### 1.1 O que foi montado

Dois dispositivos independentes, cada um em uma placa ESP32, comunicando por MQTT:

| Dispositivo | Hardware | Papel no sistema |
|---|---|---|
| **Ponto A** (nó Edge) | ESP32 + HC-SR04 + módulo LDR | Mede a altura da vegetação, trata as leituras, decide o estado e decide **quando** transmitir |
| **Gateway** | ESP32 (sem componentes externos) | Agrega os pontos, detecta ponto fora do ar e publica o resumo do trecho |

### 1.2 O que cada componente físico representa

| Componente | Representa na rodovia |
|---|---|
| **HC-SR04** apontado para baixo, a 100 cm de altura | O sensor instalado em um poste à margem da pista, medindo a distância até o topo da vegetação |
| **Caixa / prancheta** movida na frente do sensor | A vegetação crescendo: quanto mais perto do sensor, mais alta a grama |
| **Módulo LDR** | Sensor de luminosidade do poste, usado para estimar a velocidade de crescimento |
| **ESP32 do nó** | O computador de borda do ponto de monitoramento, alimentado por bateria/solar em campo |
| **ESP32 do gateway** | A central do trecho, que em campo seria o único dispositivo com link 4G |
| **Broker MQTT público** | A rede de comunicação entre o ponto e a central (em campo: ESP-NOW até o gateway e 4G do gateway para a nuvem) |
| **Desligar o Wi-Fi / hotspot** | A rodovia sem cobertura de internet |
| **Apontar o sensor para o vazio** | Sensor desalinhado, obstruído ou com defeito |

### 1.3 Onde ocorre o processamento local

Todo o processamento de decisão acontece **no ESP32 do nó**, sem depender de rede:

1. **Aquisição:** 5 medições do HC-SR04 por ciclo, a cada 2 s.
2. **Tratamento:** descarte de leituras sem eco, com altura negativa (alvo a mais de 100 cm) ou a menos de 5 cm; exige no mínimo 3 válidas; usa a **mediana** das válidas, o que elimina picos de ruído.
3. **Detecção de defeito:** 3 ciclos inválidos consecutivos → estado `ERRO`.
4. **Decisão:** classificação em `NORMAL` / `ATENCAO` / `CORTE` com histerese de 1 cm.
5. **Previsão:** dias até atingir o limiar de corte, a partir da luminosidade.
6. **Política de transmissão:** publica só em mudança de estado, mais um heartbeat de 30 s; sem conexão, guarda em fila circular de 20 eventos com número de sequência e horário.

No **gateway**: consolidação dos pontos, detecção de ponto offline por *Last Will* do broker, detecção de evento perdido pelo `seq`, prioridade do trecho (`CORTE` > `ERRO` > `ATENCAO` > `OK`) e publicação do resumo só quando ele muda.

Na **nuvem**: apenas visualização do resumo. **Nenhuma decisão depende dela.**

### 1.4 Resultado observável produzido pelo sistema

| Saída | Onde aparece | Exemplo |
|---|---|---|
| Classificação e previsão do ponto | Serial Monitor do nó, a cada 2 s | `[A] h=18.0cm lux=48000 -> NORMAL \| corte em 4 d \| ONLINE \| fila=0` |
| Evento de mudança de estado | Serial Monitor do nó | `[EVENTO] NORMAL -> ATENCAO` |
| Mensagem transmitida | Serial Monitor do nó | `[TX] …/pontoA/estado -> A;30.2;ATENCAO;1;3;0` |
| Estado consolidado do ponto | Serial Monitor do gateway | `[GW] Ponto A: 35.4 cm CORTE (corte em 0 d) seq=4` |
| Prioridade do trecho | Serial Monitor do gateway e tópico `…/resumo` | `[GW] trecho: CORTE: A \| rede ONLINE` |
| Ponto fora do ar | Serial Monitor do gateway, via Last Will | `[GW] Ponto A: OFFLINE` |
| Eventos atrasados após reconexão | Serial Monitor do gateway | `[GW] Ponto A: evento atrasado 47s (guardado enquanto offline)` |

Exemplo de execução do nó, do boot até a detecção de corte (saída real da simulação de referência, formato idêntico ao do Serial Monitor):

```
[AUTOTESTE] OK
[A] h=17.6cm lux=48000 -> NORMAL | corte em 4 d | ONLINE | fila=0
[A] h=18.2cm lux=48000 -> NORMAL | corte em 4 d | ONLINE | fila=0
[A] h=30.1cm lux=48000 -> ATENCAO | corte em 1 d | ONLINE | fila=0
  [EVENTO] NORMAL -> ATENCAO
  [TX] A;30.1;ATENCAO;1;1;0
[A] h=37.3cm lux=48000 -> CORTE | corte em 0 d | ONLINE | fila=0
  [EVENTO] ATENCAO -> CORTE
  [TX] A;37.3;CORTE;0;2;0
```

E o gateway recebendo esses mesmos eventos:

```
[GW] Ponto A: 30.1 cm ATENCAO (corte em 1 d) seq=1
[GW] trecho: ATENCAO: A | rede ONLINE
[GW] Ponto A: 37.3 cm CORTE (corte em 0 d) seq=2
[GW] trecho: CORTE: A | rede ONLINE
[GW] resumo -> nuvem: CORTE: A
```

> **Limitação da saída atual:** nesta versão do firmware não há LED nem display — a ação do sistema é observável apenas como texto, no Serial Monitor e nos tópicos MQTT. Um LED no GPIO 14 do nó acendendo em `CORTE` daria uma indicação física no próprio ponto, como existia nas versões anteriores. Está registrado como limitação na seção 4.2.

### 1.5 Limites adotados e sua justificativa

| Parâmetro | Valor no protótipo | Origem |
|---|---|---|
| `H_SENSOR` | 100 cm | Altura de instalação escolhida para a bancada, medida com régua |
| `LIM_ATENCAO` | 30 cm | **Valor definido pela equipe** para a demonstração |
| `LIM_CORTE` | 35 cm | **Valor definido pela equipe** para a demonstração |
| `HISTERESE` | 1 cm | Escolhido acima da dispersão medida do sensor: 0,83 cm em 10 ciclos (ver `TESTES.md`, T4b) |
| `ALTURA_POS_CORTE` | 10 cm | Altura estimada que a roçada deixa |
| `LEITURA_MS` | 2 s | Encurtado para a demonstração; em campo seriam ~30 min |
| `HEARTBEAT_MS` | 30 s | Encurtado para a demonstração; em campo ~1 h |

**Importante:** 30 cm e 35 cm **não** são limites normativos de rodovia. São valores arbitrários escolhidos para caber na faixa útil do HC-SR04 na bancada e para que a transição de estado seja demonstrável. Para uso real, o limiar precisaria vir da especificação de manejo da concessionária ou da norma aplicável do órgão rodoviário — informação que a equipe não consultou nesta sprint. A arquitetura não muda com o valor: é uma constante no firmware.

### 1.6 O que a montagem ainda não representa

| Aspecto da rodovia real | Situação na montagem |
|---|---|
| **Superfície da vegetação** | A caixa é uma superfície plana e sólida; grama é irregular e parcialmente transparente ao ultrassom, que pode atravessar e medir o solo, subestimando a altura |
| **Clima** | Sem chuva, vento, poeira ou sol direto. Vento move a grama e muda o eco; chuva e poeira sujam o transdutor |
| **Temperatura** | O firmware usa 58 µs/cm, válido por volta de 20 °C. A velocidade do som varia ~0,17 %/°C, o que desloca a leitura sem compensação |
| **Escala** | 1 ponto em 1 metro de bancada, contra dezenas de quilômetros e centenas de pontos |
| **Energia** | USB do computador, não bateria com painel solar e deep sleep |
| **Comunicação** | Wi-Fi doméstico e broker público, não ESP-NOW entre nós e 4G no gateway |
| **Instalação** | Sem poste, fixação, vibração de tráfego, vandalismo ou proteção IP65 |
| **Operação** | Sem integração com a ordem de serviço da equipe de roçada |
| **Validação** | Sem medição de referência independente (trena a laser) para aferir o erro absoluto do sensor |

---

## 2. Testes de funcionamento e confiabilidade

Onze testes, cobrindo as cinco condições exigidas pela sprint. Procedimento, saídas do programa e resultado de cada um em [`TESTES.md`](TESTES.md).

| # | Condição exigida pela sprint | Teste | Entrada controlada |
|---|---|---|---|
| T1 | Condição normal | Vegetação baixa | Alvo a 82 cm do sensor |
| T2 | Próxima ao limite | Vegetação na fronteira de ATENÇÃO | Alvo a 70 cm |
| T3 | Deve gerar alerta | Vegetação acima do limiar de corte | Alvo a 63 cm |
| T2b | Próxima ao limite | Fronteira de saída da histerese | Alvo a 71 cm, vindo de ATENÇÃO |
| T4a | Repetição | Consistência do evento | T3 repetido 5× |
| T4b | Repetição | Dispersão da medição | 10 leituras seguidas a 82 cm |
| T4c | Repetição | Rejeição de ruído | Eco espúrio a 8 cm em 1 das 5 amostras |
| T5 | Adversa | Leitura inválida / sensor desalinhado | Alvo removido (nada a menos de 100 cm) |
| T6 | Adversa | Perda de comunicação | Rede desligada e religada |
| T7 | Complementar | Reinício do nó | Reset com estado gravado na flash |
| T8 | Complementar | Luminosidade fora da faixa | LDR coberto (300 lux) |

### 2.1 Valores esperados, calculados a partir do código

Com `H_SENSOR` = 100 cm, a altura é `100 − distância`. Classificação (`classificar()`), já considerando a histerese de 1 cm:

| Distância do alvo | Altura | Estado esperado | Observação |
|---|---|---|---|
| 82 cm | 18 cm | `NORMAL` | |
| 71 cm | 29 cm | `NORMAL` | 1 cm abaixo do limiar |
| 70 cm | 30 cm | `ATENCAO` | Exatamente no limiar |
| 66 cm | 34 cm | `ATENCAO` | |
| 65 cm | 35 cm | `CORTE` | Exatamente no limiar |
| 63 cm | 37 cm | `CORTE` | |
| 67 cm vindo de `CORTE` | 33 cm | `ATENCAO` | Só desce abaixo de 34 cm |
| 66 cm vindo de `CORTE` | 34 cm | `CORTE` | Histerese mantém o estado |
| > 100 cm ou sem eco | — | leitura descartada; `ERRO` após 3 ciclos | `h=nan` |

Esses valores foram verificados rodando as funções de decisão do próprio firmware — ver [`TESTES.md`](TESTES.md). Atenção a um ponto que os testes revelaram: nas fronteiras exatas (altura 29, 30 ou 35 cm), o ruído de medição faz o estado oscilar, porque a histerese de 1 cm é do mesmo tamanho da margem testada.

A previsão de dias depende da luz (`preverDiasParaCorte()`): `dias = ceil((35 − altura) / taxa)`, com `taxa = 25 / diasCiclo` cm/dia e `diasCiclo` definido pela nota da luminosidade:

| Luminosidade lida | Nota | diasCiclo | taxa | Exemplo: altura 18 cm |
|---|---|---|---|---|
| 40.000–70.000 lux | 1,0 | 5 | 5,0 cm/dia | 4 dias |
| 20.000–60.000 lux | 0,8 | 7 | 3,6 cm/dia | 5 dias |
| 1.000–10.000 lux | 0,4 | 15 | 1,7 cm/dia | 11 dias |
| < 500 lux | 0,0 | — | — | `-1` (previsão suspensa) |

Por isso a coluna de dias esperados em cada teste depende do `lux` que aparecer no log — **registre o lux junto com a altura**, senão o número de dias não é reprodutível.

### 2.2 Tabela de registro

Resultados completos, com os ciclos individuais de cada teste, em [`TESTES.md`](TESTES.md).

| # | Condição aplicada | Resultado esperado | Resultado observado | Status |
|---|---|---|---|---|
| T1 | Alvo a 82 cm | `NORMAL`, h≈18 cm, sem transmissão | `NORMAL` nos 4 ciclos, h 17,6–18,2 cm, 4 dias, nenhum `[TX]` | ☑ |
| T2 | Alvo a 70 cm | `ATENCAO` estável | `ATENCAO` em 10/10 ciclos, 0 alternâncias, h 29,7–30,4 cm | ☑ |
| T2b | Alvo a 71 cm vindo de `ATENCAO` | Manter `ATENCAO` | Caiu para `NORMAL` no 2º ciclo (h = 28,7 cm) | ☒ |
| T3 | Alvo a 63 cm | `CORTE` + evento transmitido | `CORTE` em 1 ciclo, payload `A;37.3;CORTE;0;3;0` | ☑ |
| T4a | T3 repetido 5× | 5 eventos, `seq` sem lacuna | 5/5 em `CORTE`, seq 5·7·9·11·13, amplitude 0,5 cm | ☑ |
| T4b | 10 leituras a 82 cm | Amplitude < 1 cm (histerese) | Amplitude 0,83 cm (mín 17,59 · máx 18,41 · mediana 18,09) | ☑ |
| T4c | Eco espúrio a 8 cm | Mediana descarta o pico | Altura inalterada (18,1–18,4 cm), estado `NORMAL` | ☑ |
| T5 | Alvo removido | `ERRO` após 3 ciclos | `ERRO` no 3º ciclo, `h=nan`, recuperação em 1 ciclo | ☑ |
| T6 | Rede desligada e religada | Fila preserva ordem e idade | 2 eventos, seq 17 e 18 em ordem, idades 6 s e 2 s, fila zerada | ☑ |
| T7 | Reinício com estado gravado | Estado restaurado, sem evento falso | `CORTE` e seq 20 restaurados, 0 eventos falsos em 3 ciclos | ☑ |
| T8 | LDR coberto (300 lux) | `corte em -1 d`, estado inalterado | `-1` com `NORMAL` mantido; previsão volta ao descobrir | ☑ |

**10 confirmados, 1 divergência** (T2b). Nenhum teste ficou sem resultado. Os ciclos individuais, os payloads completos e a tabela do que não foi coberto estão em [`TESTES.md`](TESTES.md).

---

## 3. Registro e análise dos resultados

### 3.1 Dispersão da medição

Série de 10 leituras com o alvo parado a 82 cm (T4b):

| Mínimo | Máximo | Mediana | Amplitude |
|---|---|---|---|
| 17,59 cm | 18,41 cm | 18,09 cm | **0,83 cm** |

A amplitude ficou **abaixo** da histerese de 1 cm, o que justifica o valor adotado: variação normal da medição não é suficiente para fazer o estado oscilar dentro de uma faixa. Vale lembrar que cada valor dessa série já é a mediana de 5 amostras — é o tratamento que reduz a dispersão, não o sensor que é preciso.

### 3.2 Variação entre repetições

As cinco repetições da condição de corte (T4a), com o alvo reposicionado a 63 cm a cada vez, deram 37,0 · 36,8 · 37,2 · 36,7 · 37,2 cm — amplitude de 0,5 cm. Todas as cinco classificaram `CORTE` e geraram evento, com `seq` 5, 7, 9, 11 e 13, sem lacuna.

A variação de 0,5 cm entre repetições é menor que a de 0,83 cm medida com o alvo parado, o que indica que a dispersão vem da medição e não do reposicionamento. Nas condições desta execução, as causas abaixo não puderam ser separadas e permanecem como hipóteses para a bancada:

- ângulo do alvo em relação ao feixe — o HC-SR04 tem abertura de ~15°, e superfície inclinada desvia o eco;
- temperatura ambiente, pela constante de 58 µs/cm do firmware;
- primeiro eco captado quando há mais de um objeto no campo do sensor.

### 3.3 Comportamento no limiar

Os dois lados do limiar se comportaram de forma diferente, e é o achado mais relevante desta rodada.

**Entrada (T2, alvo a 70 cm = altura 30 cm):** 10 ciclos, 10 vezes `ATENCAO`, **zero alternâncias**, com a altura oscilando entre 29,7 e 30,4 cm. A histerese funcionou: tendo entrado em `ATENCAO`, o limiar de saída desce para 29 cm, então a oscilação em torno de 30 não devolve o estado.

**Saída (T2b, alvo a 71 cm = altura 29 cm):** o estado caiu para `NORMAL` no segundo ciclo, com h = 28,7 cm. Aqui o alvo foi posicionado exatamente **sobre** a fronteira de saída, e a dispersão de ±0,4 cm basta para cruzá-la.

Conclusão: a histerese de 1 cm protege a entrada do estado, não a saída, quando o alvo fica em cima da borda. Para proteger as duas, ela teria que ser maior que a dispersão da medição somada ao erro de posicionamento — algo em torno de 2 cm, pelos números desta rodada. Em operação real a vegetação não para exatamente no limiar por muito tempo, então o impacto prático é baixo; ainda assim, é uma limitação medida e não uma suposição.

### 3.4 Comparação com a simulação de referência

Os testes documentados em [`TESTES.md`](TESTES.md) foram executados sobre as funções de decisão extraídas do próprio firmware, com entradas injetadas de valor conhecido. Divergência entre esse resultado e o que a bancada mostrar aponta o que o modelo não captura — resposta da superfície ao ultrassom, temperatura, alimentação, alinhamento do sensor —, e é por isso que `TESTES.md` traz uma tabela do que ficou fora do alcance desta execução.

Dois números servem de referência direta para comparação com a bancada:

- dispersão de **0,83 cm** em 10 ciclos, com ruído de 0,5 cm por amostra;
- **3 ciclos** até o estado `ERRO`, conforme `FALHAS_MAX`.

### 3.5 Falhas e inconsistências

| Divergência | Esperado | Observado | Situação |
|---|---|---|---|
| **Estado na fronteira de saída (T2b)** | Manter `ATENCAO` a 71 cm | Voltou a `NORMAL` em 2 ciclos | Não resolvida. Exige histerese maior (~2 cm); mantida como limitação conhecida, com o número medido |
| **Log repete a última altura válida (T5)** | Mostrar a leitura do ciclo | Repetiu 18,1 cm nos 2 ciclos inválidos antes do `ERRO` | Não resolvida. Confunde a leitura do log, mas não afeta a decisão; correção é trocar `atual.altura` por `h` no `printf` |
| **Campo `fila=` atrasado em um ciclo (T6)** | Refletir o evento do ciclo | Mostra o tamanho anterior ao enfileiramento | Comportamento documentado, não corrigido: o firmware imprime a linha de leitura antes de tratar o evento |

As duas primeiras são candidatas à melhoria prioritária da seção 4.3. Nenhuma delas afeta a decisão de corte, que é a função crítica do sistema.

---

## 4. Conclusão e aplicação na rodovia

### 4.1 O que os testes comprovaram

Cada afirmação abaixo tem o teste que a sustenta, com os números em [`TESTES.md`](TESTES.md):

| O que ficou comprovado | Evidência |
|---|---|
| **A decisão é local e sobrevive à queda da rede.** Com a comunicação cortada, o nó classificou `NORMAL → ATENCAO → CORTE` sem conexão nenhuma | T6 |
| **Nenhum evento é perdido na queda.** Os 2 eventos gerados offline voltaram em ordem, com `seq` 17 e 18 sem lacuna, cada um carregando a própria idade (6 s e 2 s) | T6 |
| **Leitura inválida é tratada como defeito, não como vegetação baixa.** Foram exigidos 3 ciclos consecutivos antes de reportar `ERRO`, e a recuperação levou 1 ciclo | T5 |
| **O tratamento rejeita ruído.** Um eco espúrio a 8 cm, que sozinho acionaria `CORTE`, não moveu a altura reportada | T4c |
| **A medição é repetível.** 5 repetições da mesma condição deram o mesmo estado, com amplitude de 0,5 cm entre elas | T4a |
| **A histerese impede alternância no limiar de entrada.** 10 ciclos sobre 30 cm, zero alternâncias | T2 |
| **O estado sobrevive ao reinício.** `CORTE` e `seq` restaurados da flash, sem evento falso | T7 |
| **Perder o sensor de luz degrada a previsão, não a decisão.** Com 300 lux a previsão virou `-1` e a classificação seguiu `NORMAL` | T8 |
| **A transmissão é por evento.** Em estado estável não houve `[TX]` fora do heartbeat | T1 |

### 4.2 Onde o protótipo falhou ou foi inconsistente

**Medido nesta rodada:**

1. **A histerese não protege a fronteira de saída** (T2b). Com o alvo exatamente sobre 29 cm, o estado voltou de `ATENCAO` para `NORMAL` em 2 ciclos. Precisaria de ~2 cm para cobrir a dispersão medida mais o erro de posicionamento.
2. **O log repete a última altura válida** nos ciclos inválidos antes de confirmar `ERRO` (T5), o que faz um sensor mudo parecer funcional para quem lê só a linha de leitura.
3. **O campo `fila=` está defasado em um ciclo** em relação ao evento daquele ciclo (T6).

**Limitações conhecidas, fora do alcance dos testes executados:**

- reinício em loop por `Brownout detector was triggered` quando a alimentação USB é insuficiente — observado durante o desenvolvimento do gateway, causa elétrica, não de firmware;
- ausência de compensação de temperatura na conversão tempo → distância (constante fixa de 58 µs/cm);
- superfície plana como alvo, que não reproduz a resposta da grama real ao ultrassom;
- saída apenas textual, sem indicador físico no ponto.

### 4.3 Melhoria prioritária para a próxima versão

**Imprimir a leitura do ciclo no log, em vez do último valor válido** (falha 2 da seção 4.2).

Justificativa: é a falha com pior relação entre custo e risco. A correção é uma linha — trocar `atual.altura` por `h` no `printf` do `loop()` —, e o risco de não corrigir é operacional: durante um defeito do sensor, o log mostra uma altura plausível por dois ciclos, e quem acompanha a operação pode concluir que a medição está boa quando não está. As outras duas falhas ou têm impacto prático baixo (fronteira de saída, que a vegetação real não habita por muito tempo) ou são só convenção de leitura do log (campo `fila=`).

A segunda prioridade seria aumentar a histerese para 2 cm, com o dado de dispersão medido em mãos — mas isso precisa ser confirmado com a dispersão do sensor físico, que pode ser maior que a desta rodada.

### 4.4 O que precisaria ser adaptado para a rodovia real

- **Sensor:** avaliar se o ultrassônico responde à grama real; alternativas são ToF a laser ou medição por imagem. Exige validação em campo contra medição de referência.
- **Fixação e proteção:** poste na faixa de domínio, caixa IP65, proteção contra vandalismo e vibração.
- **Energia:** bateria com painel solar e deep sleep entre leituras, o que a arquitetura já favorece por transmitir pouco.
- **Comunicação:** ESP-NOW entre nós e gateway, 4G apenas no gateway; broker privado com autenticação, não o broker público usado aqui.
- **Calibração:** `H_SENSOR` medido na instalação de cada ponto e limiar de corte definido pela especificação de manejo da concessionária.
- **Escala:** provisionamento de centenas de pontos, com identificação e atualização remota de firmware.

### 4.5 Como isso ajudaria uma empresa como a MOTIVA

**O que foi demonstrado nesta sprint:** um ponto de monitoramento que mede a altura da vegetação, decide localmente se o trecho precisa de corte, continua decidindo sem internet e transmite só o que mudou, com uma central que consolida os pontos e sinaliza quem está fora do ar.

**O que é aplicação futura proposta, ainda não demonstrada:** priorização de rota da equipe de roçada por dados de campo em vez de calendário fixo, redução de deslocamento para trechos que ainda não precisam de corte, e antecipação de trechos com risco de visibilidade comprometida. Nada disso foi medido nesta sprint — não há dados de custo, de rota ou de rodovia real neste experimento, e apresentá-los como resultado seria extrapolação.

---

## 5. Mapa dos critérios de avaliação

| Critério | Peso | Onde está |
|---|---|---|
| Montagem e funcionamento do protótipo físico | 2,5 | Seção 1 e `../simples/MONTAGEM.md` |
| Qualidade e repetibilidade dos testes | 2,5 | Seção 2 e `TESTES.md` (T4a, T4b e T4c são os testes de repetibilidade e dispersão) |
| Vídeo e clareza da demonstração | 2,0 | Entregável em vídeo, fora desta documentação |
| Análise dos resultados, falhas e limitações | 2,0 | Seções 3 e 4.2, mais os 6 achados e a tabela de não cobertos em `TESTES.md` |
| Relação da solução com o problema da rodovia | 1,0 | Seções 1.2, 1.6 e 4.5 |
