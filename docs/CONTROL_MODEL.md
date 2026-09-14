# Modelo de controle e FSM do looper

Este documento descreve o comportamento implementado em `desktop/Source/LooperEngine.cpp`
(a fonte de verdade em código é sempre essa; este arquivo é a versão em prosa
para consulta rápida). O modelo é inspirado no **Multi Mode** do Sheeran
Looper X (todas as 4 tracks travadas no mesmo comprimento de loop), adaptado
e simplificado para os 8 footswitches do nosso hardware.

## Hardware

8 footswitches ligados ao Arduino Mega: `REC_PLAY`, `PAUSE` (= **STOP**),
`UNDO` (fisicamente rotulado **"CLEAR"** no pedal), `MODE`, `TRACK1`,
`TRACK2`, `TRACK3`, `TRACK4`. 4 LEDs bicolores, um por track.

O Mega **não** conhece esta FSM — ele só envia eventos de botão classificados
(short-press na maioria, SHORT/LONG no caso do UNDO) e recebe comandos de
LED. Toda a lógica abaixo roda no app desktop (`LooperEngine`).

## Estado

- **Modo global**: `REC_MODE` ou `PLAY_MODE` (= "Mute Mode" no manual do
  Sheeran), alternado pelo botão `MODE`. O modo só muda o que os botões de
  track fazem — não afeta `REC_PLAY`, `PAUSE`/STOP, `UNDO`/CLEAR nem o
  transporte.
- **Track selecionada**: 0-3, padrão Track1 no boot/reset. Só muda em
  `REC_MODE`, via `TRACK[n]` ou pelo atalho descrito abaixo.
- **Comprimento do loop mestre**: indefinido ("primordial") até a primeira
  track fechar uma gravação — pode ser **qualquer** track, não só a Track1.
- **Transporte tocando/parado**: controlado pelo `PAUSE`/STOP, independente
  do modo.

**Invariante**: sempre que alguma track está capturando (gravando ou
overdubando), ela é necessariamente a track selecionada.

## REC_PLAY — grava/empilha camada, toca (funciona em qualquer modo)

**Estado unificado**: gravar a camada base (definindo o loop mestre) e
empilhar uma camada de overdub **não são estados diferentes** — as duas são
só `RECORDING`. A diferença (sobrescrever vs. acumular a camada) é um
detalhe interno da `AudioTrack`, não algo que o resto do sistema precisa
distinguir. Isso elimina uma classe de bugs que existia quando eram tratadas
como estados separados.

Em `REC_MODE`, age sobre a track selecionada `S`:

- `S` não capturando (`EMPTY` ou `PLAYING`) → começa a capturar
  (`RECORDING`). Se o loop mestre ainda não tem comprimento definido, esta
  captura é quem vai defini-lo ao ser fechada; senão, já nasce alinhada ao
  comprimento existente.
- `S` capturando (`RECORDING`) → fecha a captura (empilha a camada). Se essa
  captura era a que estava definindo o loop mestre, o comprimento é fixado
  neste instante **e a track já continua direto numa nova captura**
  (overdub imediato). Caso contrário, só fecha e vai para `PLAYING`.

Enquanto uma camada nova está sendo capturada, as camadas já commitadas
continuam tocando normalmente (`mixFrameInto` não exclui `RECORDING`, só
`EMPTY`/`MUTED`/track sem camadas) — dá pra ouvir o loop existente enquanto
se adiciona uma camada em cima.

**A camada em andamento também toca**, a partir da volta seguinte do loop:
você grava uma frase e ela já volta repetindo na próxima passada, sem
precisar fechar o passe. Isso vale inclusive para a camada base das tracks
2-4 (não só para overdubs). Não há realimentação porque o ponteiro de escrita
fica `latencySamples` atrás do de leitura — ler a posição atual devolve
sempre o que foi gravado na volta anterior. A única exceção é o passe que
**define** o loop mestre, que ainda não deu a primeira volta e cujo buffer
não é zerado.

Pressionamentos seguintes alternam `PLAYING ⟷ RECORDING` livremente.

Em `PLAY_MODE` (Mute Mode), `REC_PLAY` assume a função de **PLAY**: retoma o
transporte a partir do início (equivalente a apertar `PAUSE` de novo, mas
sem precisar trocar de modo).

## TRACK[1-4]

- **`REC_MODE`**: o botão é **sempre e somente** uma ação de seleção
  (`selectedTrack = n`) — inclusive se a própria track `n` já estiver
  capturando (nesse caso é um no-op). O botão de uma track nunca fecha a
  própria captura sozinho — só `REC_PLAY` faz isso.
  - **Atalho**: se outra track `A` está capturando e o usuário pressiona
    `TRACK[n]` (`n ≠ A`), isso fecha a captura de `A` (equivalente a apertar
    `REC_PLAY` nela) — `A` vai para `PLAYING` — e imediatamente inicia a
    captura em `n`. Serve para agilizar o uso ao vivo (não está no manual do
    Sheeran, é uma conveniência nossa).
- **`PLAY_MODE` (Mute Mode)**: `TRACK[n]` = toggle mute/unmute (não pausa o
  ponteiro de leitura).

## PAUSE = STOP (funciona em qualquer modo, não é toggle)

- **Short press**: para o transporte (se alguma track estiver capturando,
  fecha essa captura primeiro) e volta a posição para o **início (0)**. Para
  retomar, use `REC_PLAY` (em `REC_MODE` continua o ciclo normal; em
  `PLAY_MODE` ele funciona como PLAY).
- Sem função de hold — `PAUSE` só para, nada mais.

## UNDO (fisicamente "CLEAR") — sempre age sobre a track selecionada

- **Short press ("Peel")**: remove a camada mais recente da track,
  **incluindo a camada base** se for a última restante. Pressionar
  repetidamente leva a track de volta a `EMPTY` — é assim que se limpa uma
  track individualmente. Se pressionado com um passe **em andamento** (ainda
  não fechado), cancela esse passe em vez de remover uma camada já
  commitada.
  - Se o peel deixar **as 4 tracks vazias**, o comprimento do loop mestre
    também volta ao estado "primordial". Sem isso o comprimento continuaria
    valendo sem nenhum áudio na tela para justificá-lo, e a única forma de
    mudá-lo seria o hold de 3s — a próxima gravação ficaria presa a um loop
    invisível.
- **Long press (3s)**: **Clear All** — limpa todas as 4 tracks e reseta o
  comprimento do loop mestre (volta ao estado "primordial"). Não muda o modo
  global (`REC_MODE`/`PLAY_MODE`) nem a track selecionada permanece a mesma
  (volta pra Track1). A partir de ~1,5s de hold, os 4 LEDs piscam vermelho
  como aviso (soltar antes dos 3s cancela).

## LEDs

- `REC_MODE`: `LED[i]` = vermelho se `i` é a track selecionada, senão apagado.
- `PLAY_MODE` (Mute Mode): `LED[i]` = apagado se mutada, senão verde.
- **Track gravando**: `LED[i]` = vermelho **piscando**, em qualquer modo,
  sobrepondo as duas regras acima. Antes o pedal não dava nenhum retorno
  visual de que estava gravando (em `REC_MODE` o LED da track selecionada
  ficava vermelho fixo tanto gravando quanto parada).
- Durante o hold do UNDO/CLEAR para Clear All (a partir de ~1,5s): todos os
  LEDs piscam vermelho, sobrepondo o estado normal.

## Entrada de áudio

2 canais (violão + voz) via a interface de áudio ligada ao PC. O seletor de
entrada de cada track escolhe quais deles a alimentam — ver **Mixer** adiante.

## Saída de áudio: MONO

Os dois canais de saída carregam o **mesmo sinal**. O pedal é um instrumento:
vai para uma entrada de mesa, um amplificador ou uma caixa, quase nunca para
um par estéreo. Qualquer diferença entre os canais viraria material perdido
(se só um lado for ligado) ou cancelamento (se os dois forem somados fora de
fase).

A dobra é feita no fim da cadeia, **depois** de somar todas as tracks e o
monitoramento e **antes** do limitador — assim o limitador vê o pico que
realmente sai, e o `Recorder` grava exatamente isso. É a **média** dos dois
canais, não a soma: tudo o que chega ali já é material centrado, então a média
devolve o próprio sinal, sem os +6 dB que a soma daria.

Chave: `config::kMonoOutput`.

**Monitoramento**: assume-se que o monitoramento direto do sinal ao vivo é
feito pela própria interface de áudio (hardware). O monitoramento por
software existe mas vem **desligado** (`config::kSoftwareInputMonitoring`) —
ligar os dois ao mesmo tempo soma o mesmo sinal duas vezes com um atraso
entre eles, o que produz comb filtering (som "de lata"/com flanger).

## Controle sem o pedal

Os 8 footswitches têm equivalente na tela e no teclado, e entram **na mesma
FSM** — o looper não distingue de onde veio o toque. Antes disso o app não
fazia absolutamente nada com o Mega desconectado.

| Tecla | Botão |
|---|---|
| `espaço` | REC / PLAY |
| `S` | STOP |
| `U` | CLEAR (remove a última camada) |
| `Shift+U` | CLEAR All (equivale ao hold de 3s) |
| `M` | MODO |
| `1` a `4` | TRACK 1 a 4 |

Os atalhos são tratados no `keyPressed()` das **duas** janelas, não no
componente: a tecla só sobe até a janela quando o componente com foco não a
consumiu, então renomear uma track continua funcionando normalmente (o
`TextEditor` fica com as teclas enquanto edita). Os botões do mixer têm
`setWantsKeyboardFocus(false)` para o Espaço não acionar o botão focado em
vez de chegar no REC/PLAY.

> **Duas filas de botão, não uma.** `SpscQueue` é estritamente
> single-producer (ver o comentário no topo de `SpscQueue.h`). A thread do
> `SerialLink` e a thread da GUI empurrando na mesma fila seria corrida, então
> cada origem tem a sua e o `AudioEngine` drena as duas — ele continua sendo
> o único consumidor.

### O desenho do pedal

A seção **PEDAL** da janela de controles é um **desenho do equipamento**
(`PedalMap`) — a caixa clara com o logo, a janela dos VUs, os LEDs e os 8
footswitches, na mesma disposição e com a mesma serigrafia do pedal real
(`PLAY+REC`, `STOP`, `CLEAR`, `MODE`, `1`-`4`).

É desenho e não uma fileira de botões de propósito: o ponto é bater o olho e
reconhecer o pedal, achando o switch na tela pelo mesmo lugar em que ele está
no chão. Por isso ele é pintado com as cores do **equipamento** (caixa clara,
serigrafia preta) e não com a paleta escura do app — fica lendo como um objeto
apoiado no painel, que é o que é.

Todo o desenho vive num espaço virtual de 1600×800 escalado uniformemente
(`PedalMap::art()`), então a proporção 2:1 do pedal se mantém em qualquer
tamanho de janela. A janela de controles usa **duas colunas** — desenho à
esquerda, ajustes à direita — porque em coluna única o desenho ficaria pequeno
demais para se reconhecer, ou a janela não caberia numa tela de 1080p.

Faz três coisas ao mesmo tempo:

1. **Clicar** num switch dispara o mesmo evento do footswitch — é o que
   permite usar o looper sem o hardware.
2. **O switch dá um flash amarelo** quando o botão é pressionado, no pedal ou
   na tela. É a forma de conferir que o toque chegou (fiação, porta serial,
   debounce) sem precisar ouvir o resultado. O amarelo é de propósito: não se
   confunde com o vermelho de gravação nem com o verde de reprodução, que já
   têm significado nos LEDs.
3. **Os LEDs e os VUs mostram o que o app está mandando** para o pedal
   (`LooperEngine::ledColor/ledBlink` e os níveis das tracks). Se o desenho e
   o equipamento discordarem, o problema está no hardware ou no firmware, não
   na FSM.

O CLEAR no desenho reproduz os dois gestos: clique curto remove a última
camada, segurar 3s limpa tudo — e por isso ele é o único que decide no soltar,
igual ao firmware.

> **Limitação atual**: o firmware envia o *press*, nunca o *release*, então
> "está pressionado" é aproximado por "foi pressionado nos últimos 180 ms".
> Para o estado real de cada switch seria preciso uma mensagem nova no
> protocolo com o bitmask dos botões, enviada a cada mudança —
> `Button::isPressed()` já existe no firmware.

## Configuração que antes exigia recompilar

Na seção **ÁUDIO** da janela de controles, tudo salvo em disco:

- **Ajuste de latência** (−50 a +150 ms), somado ao que o driver reporta.
  Positivo adianta a gravação. Era `config::kLatencyTrimMs`, uma `constexpr` —
  ajustar o parâmetro mais importante do looper exigia editar C++ e
  recompilar. `LooperEngine` agora guarda a latência crua do driver e o trim
  separados, para os dois mudarem independente.
- **Monitorar entrada** por software. Era `config::kSoftwareInputMonitoring`.
- **Porta COM** do Mega, com "Reconectar pedal". Vazio = auto-detectar por
  VID:PID (o padrão, que funciona). A porta é lida **uma vez** no
  `SerialLink::start()`; trocar exige `stop()` + `start()`, justamente para
  não haver uma string sendo alterada enquanto a thread serial a lê.

## As duas janelas

A interface é dividida em **duas janelas independentes**, para uso com dois
monitores:

- **Controles** (`MainComponent`) — tudo o que se ajusta: trim das entradas,
  canal de mesa de cada track, botão de configuração de áudio, e um rodapé
  discreto com estado do pedal, dispositivo, sample rate, buffer e latência
  compensada. A latência saiu do topo mas continua aqui, porque é o número
  que se olha para calibrar `config::kLatencyTrimMs`.
- **VUs** (`PerformanceComponent`) — só leitura, para ficar virada para quem
  está tocando: nome, estado, número de camadas e o medidor de cada track,
  em tamanho grande.

A posição e o tamanho das duas são salvos (ver Persistência) — a divisão
entre os monitores é feita uma vez só. Fechar a janela de VUs no X apenas a
esconde; o botão "Janela de VUs" na janela de controles a traz de volta. Só a
janela de controles encerra o app.

### Identidade visual

A premissa é que **a tela é o *faceplate* do pedal**, não um painel de
software. O vocabulário é o de equipamento: caixa de alumínio anodizado,
legenda serigrafada, poço rebaixado, medidor de LED segmentado. Tudo está em
`Source/PedalLookAndFeel.{h,cpp}` (namespace `theme` + a `LookAndFeel` do
JUCE).

- **Cor**: grafite quente (`enclosure` `#17181A`) em vez de preto — preto puro
  existe em tela, não em caixa pintada. Profundidade vem de um fio de luz na
  aresta de cima com sombra embaixo (`paintPanel`), do jeito que a luz pega
  numa peça fresada; o inverso (`paintGroove`) faz os rebaixos.
- **Acento**: vermelho e verde **são os LEDs bicolores do hardware**, não uma
  escolha decorativa. Significam na tela exatamente o que significam no pedal,
  e nada mais no app usa essas cores. O âmbar aparece só na zona de atenção do
  medidor.
- **Tipo**: `Bahnschrift` (derivada da DIN, a norma de letreiro de painel
  industrial) para legendas, em caixa alta e bem espaçada; `Cascadia Mono`
  para números — é tabular, então o valor não muda de largura ao passar de
  "100 %" para "98 %" e não treme enquanto se mexe no fader. Caixa alta é só
  para **legendas**; texto de botão é ação e fica em caixa de sentença.
- **Medidor** (`VuMeter`): dois estilos, escolhidos pela largura disponível.
  - `Style::Bar` — **janela de VUs**. Barra contínua, largura cheia. É o
    medidor grande, lido a distância no meio de uma música, onde a altura de
    uma coluna cheia se lê mais rápido do que qualquer outra coisa.
  - `Style::Segments` — **canal do mixer**. Régua de LEDs segmentada. Ali a
    coluna é estreita e serve para dosar o fader de perto, e o segmento ajuda
    a ler valor ("três abaixo do topo") em vez de só tendência.
  - Segmentos numa coluna **larga** não funcionam: cada segmento fica mais
    largo que alto e a régua lê como tecido listrado em vez de medidor. Foi
    por isso que a primeira tentativa de usar segmentos nos dois lugares
    precisou estreitar o medidor da janela de VUs — o que estragava justamente
    o que aquele medidor tem de bom.
  - Nos dois estilos o leito apagado fica visível de leve, e o rebaixo
    (`paintGroove`) dá a profundidade: é o que impede a track sem sinal de
    virar um retângulo preto chapado. O alpha do leito é menor no estilo Bar
    porque a área é muito maior.
  - Os dois têm **retenção de pico**: uma risca no ponto mais alto recente, na
    cor da zona em que ele caiu. É o único aviso de que o sinal está perto do
    teto — a barra sozinha não diz isso.

> **Acentuação**: `juce::String(const char*)` interpreta os bytes como
> Latin-1, não como UTF-8. Todo literal acentuado **tem** de passar por
> `ui::utf8()` (`Source/UiText.h`). Os fontes são UTF-8 (`/utf-8` no MSVC).
> Confusamente, `juce::String::operator+=` faz o contrário e decodifica UTF-8,
> o que deixava metade da interface certa e metade quebrada.

### Moldura de modo

O modo global não é mais mostrado como texto ("MODO REC"/"MODO PLAY"): as
**duas janelas ganham uma moldura em degradê** na borda, mais saturada na
beirada e sumindo para dentro. A informação vira periférica — dá para ver de
longe e de canto de olho tocando, sem ocupar espaço nem exigir leitura.

- `REC_MODE` tocando → **vermelha**
- `PLAY_MODE` (Mute Mode) tocando → **verde**
- Transporte parado (STOP) → **cinza**, em qualquer modo

A moldura vermelha em volta da track selecionada continua existindo, dentro
das duas janelas.

### Progresso do loop

Na **janela de controles**, uma barra fina que enche conforme o ponteiro de
leitura avança e zera quando o loop reinicia. Sem texto, de propósito: de
relance ela responde a única pergunta que importa tocando — o loop está no
começo, no meio ou no fim. Enquanto o loop mestre não estiver definido,
aparece só o trilho vazio.

Na **janela de VUs** o mesmo dado é mostrado como um **playhead que atravessa
os quatro medidores** (`PerformanceComponent::paintOverChildren`). A escolha
não é decorativa: a invariante central deste looper é que as 4 tracks
compartilham um só comprimento de loop, e quatro barras separadas diriam o
contrário. Uma linha única cruzando a régua inteira *é* essa informação.

Tem de ser pintado em `paintOverChildren` e não em `paint`: os painéis das
tracks são componentes filhos e se desenham por cima do pai, então uma linha
pintada antes deles ficaria escondida.

## Mixer (na tela, não no pedal)

Cada track é um canal de mesa, controlado pela GUI (os footswitches não têm
função de mixer). Os nomes das entradas vêm de `config::kInputChannelNames`.

- **Nome da track**: duplo clique no título do canal para renomear. O nome
  aparece nas duas janelas e é salvo em disco. Apagar tudo devolve o nome
  anterior — uma track nunca fica sem nome.
- **Trim de entrada**: um controle por entrada física, no bloco "ENTRADAS" da
  janela de controles. Diferente do fader de track, o trim é **destrutivo**:
  aplicado antes do roteamento, entra no que for gravado (é o comportamento
  normal de um trim de canal).
- Cada canal do mixer tem **VU próprio**, ao lado do fader, além do medidor
  grande na janela de VUs — dosar o volume olhando para o outro monitor não
  funcionaria.

> **Qual instrumento está em qual canal** é definido só por
> `config::kInputChannelNames`. Nada no resto do código assume isso. Se você
> remontar a fiação da interface, é a única coisa a corrigir. Hoje: canal 0 =
> `Voz`, canal 1 = `Violao`.

### O que o VU de cada track mede

- **Track selecionada** (em `REC_MODE`, sem gravar): mede a **entrada já
  roteada**, mesmo com a track vazia ou o transporte parado. É assim que se
  confere se está chegando sinal e se dosa o trim **antes** de apertar REC, em
  vez de gravar no escuro e descobrir depois. Na janela de VUs o rótulo da
  entrada aparece destacado em vermelho (`ENTRADA: Violao`) — sem isso, um
  medidor se mexendo numa track vazia pareceria defeito.
- **Track gravando**: mede a entrada roteada (é o que está sendo escrito).
- **Demais tracks**: medem o que estão reproduzindo, pós-fader.

O critério bate de propósito com o da moldura vermelha de seleção: o medidor
que muda de significado é exatamente o que está destacado.
- Tanto o fader de track quanto o trim de entrada têm **botão "100%"** para
  voltar ao ganho unitário; duplo clique no próprio controle faz o mesmo.

- **Seletor de entrada**: escolhe quais entradas físicas alimentam aquela
  track — `Violao`, `Voz`, `Violao + Voz` ou `Nenhuma`. Internamente é um
  bitmask por track (`AudioTrack::setInputMask`).
  - **Qualquer** seleção — uma entrada ou todas — é somada em **mono e
    mandada para os dois canais** de saída, como um canal mono de mesa com o
    pan no centro. Se mantivesse o canal original, uma track só de violão
    sairia de um lado só.
  - Com **duas** entradas na mesma track, o sinal gravado é a média das duas
    (não a soma), pelo mesmo motivo que uma mesa não soma dois canais em cima
    de um fader só.
  - O roteamento é aplicado na **gravação**. Mudá-lo depois não altera o que
    já foi gravado — é assim que uma mesa se comporta.
- **Fader de volume**: 0 a 200%, com o ponto unitário (100%) no meio do
  curso. Aplicado na **reprodução**, então é não-destrutivo: pode ser mexido
  a qualquer momento sem estragar o áudio gravado. O VU meter é pós-fader
  (acompanha o que se ouve); durante a gravação ele mostra a entrada já
  roteada, que é como se confere se o roteamento está certo.

Ambos são lidos pela thread de áudio via `std::atomic` — a GUI escreve sem
lock, porque travar o callback de áudio seria pior que ler um valor com um
bloco de atraso.

## Persistência das configurações

Salvas automaticamente em `%APPDATA%\PEDAL\Pedal Looper.settings`:

- configuração do dispositivo de áudio (driver, device, sample rate, buffer
  size, canais) — gravada na abertura e a cada mudança feita no diálogo de
  configuração;
- entrada roteada, volume e **nome** de cada track — gravados a cada
  alteração;
- **trim de cada entrada** física;
- **posição e tamanho das duas janelas** — para não ter que rearrumar os dois
  monitores a cada abertura. Uma janela salva num monitor que não existe mais
  é reposicionada em vez de ficar inacessível.

Na primeira execução (arquivo inexistente) o app aplica os padrões do projeto
(ASIO na `kPreferredSampleRate`, 2 canais). A partir daí **o que está salvo
tem prioridade** — antes o app forçava esses padrões toda vez que abria, o
que descartava silenciosamente qualquer escolha feita no diálogo de áudio.

Para voltar tudo ao padrão, basta apagar esse arquivo.

## Compensação de latência

O ponto mais importante do motor de áudio, e o que estava faltando.

Quando o músico ouve o loop e toca em cima, o que ele toca só chega de volta
na entrada depois da latência de **ida e volta** (saída + entrada) da
interface. Se a gravação for escrita na posição atual do transporte, toda
camada de overdub entra atrasada por esse valor — e o erro se acumula a cada
camada. É o clássico "o loop desanda".

O app corrige isso escrevendo na posição
`transportPosition - latencySamples`:

- `latencySamples` vem do próprio driver
  (`getInputLatencyInSamples() + getOutputLatencyInSamples()`), lida quando o
  dispositivo abre.
- `config::kLatencyTrimMs` é um ajuste fino **manual** somado por cima, para
  quando o driver reporta menos que a latência real (o ASIO4ALL costuma
  fazer isso). Positivo adianta a gravação.
- O valor efetivo em milissegundos aparece na barra de status da GUI, para
  conferência.

O passe que **define** o loop mestre é a única exceção: ele grava em
sequência a partir de 0, sem compensação, porque é ele que estabelece a
referência de tempo (não há nada anterior com que se alinhar). Ao fechá-lo, o
transporte é posicionado em `latencySamples` justamente para que o ponteiro
de escrita do passe seguinte caia exatamente em 0.

## Diferenças em relação ao Sheeran Looper X original

- O Sheeran distingue "Peel" (não remove a camada base) de "Clear Track"
  (remove tudo, via hold no botão de track) como duas ações separadas. Nós
  simplificamos para uma coisa só no botão `UNDO`/CLEAR: pressionar
  repetidamente remove camada por camada até `EMPTY`, incluindo a base -
  bate com o rótulo físico "CLEAR" do botão.
- `PAUSE`/STOP não é mais um toggle - só para; retomar é sempre via
  `REC_PLAY` (que assume a função de PLAY em `PLAY_MODE`).
- Não implementamos **solo** (duplo-toque nas tracks no Sheeran original) —
  pode ser adicionado depois se for útil.
- Suposição: monitoramento do sinal ao vivo (antes de qualquer captura) é
  feito pela própria interface de áudio (hardware), não pelo software.
