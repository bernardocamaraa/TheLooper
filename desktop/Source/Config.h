// Constantes globais do app desktop. Ajuste aqui conforme o hardware do
// usuario (porta COM do Mega, nome do dispositivo do cabo de audio virtual,
// e os limites de memoria dos buffers de loop).
#pragma once

#include <cstddef>
#include <cstdint>

namespace config {

// ---------------------------------------------------------------------------
// AUDIO
// ---------------------------------------------------------------------------
// Sample rate PEDIDA ao driver. A taxa REAL e lida do device em
// AudioEngine::audioDeviceAboutToStart e propagada para a LooperEngine - o
// resto do codigo nunca assume que as duas batem (era uma fonte de bugs:
// buffers dimensionados a 44100 rodando num device a 48000).
constexpr double kPreferredSampleRate = 48000.0;

// Teto usado para DIMENSIONAR os buffers. Se o device abrir acima disso, a
// duracao maxima de loop cai proporcionalmente (ver AudioTrack::setSampleRate).
constexpr double kMaxSupportedSampleRate = 48000.0;

constexpr int kNumChannels = 2;      // ver kInputChannelNames para o que esta em cada canal
constexpr int kNumTracks = 4;

// SAIDA MONO.
//
// O pedal e um instrumento: ele vai para uma entrada de mesa, um amplificador
// ou uma caixa - quase nunca para um par estereo. Qualquer diferenca entre os
// dois canais viraria material perdido no caminho (se so um lado for ligado)
// ou cancelamento (se os dois forem somados fora de fase).
//
// Entao os dois canais de saida carregam o MESMO sinal: a mistura e dobrada em
// mono no fim da cadeia, depois de tudo somado e antes do limitador - assim o
// limitador ve o pico que realmente sai, e o Recorder grava exatamente isso.
constexpr bool kMonoOutput = true;

// Duracao maxima de loop suportada. Dimensiona os buffers "cheios" (a soma
// de cada track e o passe que define o loop, que ainda nao sabe o tamanho):
// kMaxLoopSeconds * kMaxSupportedSampleRate * kNumChannels * 4 bytes = ~23 MB.
constexpr int kMaxLoopSeconds = 60;

// ---------------------------------------------------------------------------
// CAMADAS INFINITAS (ver LayerStore.h e AudioTrack.h)
// ---------------------------------------------------------------------------
// Nao ha limite de camadas por track, e TODAS podem ser desfeitas uma a uma.
// Cada track toca uma SOMA pre-mixada (o custo do callback nao cresce com o
// numero de camadas) e guarda cada camada separada so para o desfazer, com o
// tamanho do loop real. As mais recentes ficam com a thread de audio; as mais
// antigas vao para a thread do LayerStore, que as despeja em disco quando
// passam do orcamento de RAM.

// Quantas camadas recentes cada track guarda na thread de audio (capacidade do
// array fixo), acima de quantas ela manda a mais antiga para o LayerStore, e
// abaixo de quantas pede de volta as que estao guardadas la.
constexpr int kRecentLayerSlots = 16;
constexpr int kRecentLayersKeep = 12;
constexpr int kRecentRefillBelow = 4;

// Desfazer tira a camada da soma AOS POUCOS, um pedaco por bloco de audio
// (enquanto isso a leitura subtrai o resto na hora). Quantos desfazer podem
// estar em andamento ao mesmo tempo por track, e quantas amostras por bloco.
constexpr int kMaxPendingPeels = 8;
constexpr int64_t kPeelSamplesPerBlock = int64_t{1} << 17;

// Buffers ja zerados que o LayerStore deixa prontos para a thread de audio
// (ela nunca aloca nem zera memoria). "Cheios" servem para a soma nova de uma
// track limpa e para o passe que define o loop; "do loop" tem o tamanho do
// loop mestre e servem para as camadas de overdub.
constexpr int kSpareFullBuffers = 6;
constexpr int kSpareLoopBuffers = 4;

// Alem da reserva comum, cada track guarda na mao estes buffers do tamanho do
// loop: se a thread do LayerStore demorar (gravando em disco, por exemplo), o
// overdub ainda tem onde abrir as proximas voltas sem perder nada.
constexpr int kTrackReserveBuffers = 2;

// Acima disto, as camadas guardadas pelo LayerStore mais antigas vao para
// arquivos temporarios em disco (e voltam quando o desfazer chega nelas).
constexpr int64_t kDefaultLayerRamBudgetBytes = int64_t{1536} * 1024 * 1024;

// ---------------------------------------------------------------------------
// COMPENSACAO DE LATENCIA  (a correcao mais importante deste arquivo)
// ---------------------------------------------------------------------------
// Um looper PRECISA compensar a latencia de ida-e-volta: o musico ouve o loop
// com atraso de saida, toca em cima, e o que ele toca chega na entrada com
// mais um atraso. Sem compensar, TODA camada de overdub entra atrasada e as
// camadas vao acumulando erro - e a causa classica de "o loop desanda".
//
// O app pergunta a latencia ao driver ASIO
// (getInputLatencyInSamples + getOutputLatencyInSamples) e usa esse valor
// automaticamente. kLatencyTrimMs e um ajuste fino MANUAL somado por cima,
// para o caso do driver mentir (ASIO4ALL frequentemente reporta menos do que
// a latencia real). Positivo = adianta a gravacao (corrige overdub atrasado).
constexpr double kDefaultLatencyTrimMs = 0.0;

// Teto de seguranca para a latencia reportada pelo driver (drivers bugados
// as vezes devolvem valores absurdos).
constexpr double kMaxLatencyMs = 250.0;

// ---------------------------------------------------------------------------
// MIX
// ---------------------------------------------------------------------------
// Headroom fixo por track, aplicado ANTES do fader do usuario e do limiter
// final. Nao e o volume da mesa - esse e por track, ver kDefaultTrackGain.
constexpr float kTrackGain = 0.8f;

// ---------------------------------------------------------------------------
// MIXER (ver MainComponent/TrackPanel para a interface)
// ---------------------------------------------------------------------------
// Nome de cada entrada fisica, como aparece no seletor de entrada de cada
// track. A ordem segue a FIACAO REAL da interface, conferida no uso: o canal
// 0 e o microfone e o canal 1 e o violao. Se voce remontar a fiacao, e aqui
// que se corrige - nao ha nada no resto do codigo que assuma qual instrumento
// esta em qual canal.
constexpr const char* kInputChannelNames[kNumChannels] = {"Voz", "Violão"};

// Volume inicial de cada track (1.0 = unitario). O fader vai de 0 a
// kMaxTrackGain, com o ponto unitario no meio do curso.
constexpr float kDefaultTrackGain = 1.0f;
constexpr float kMaxTrackGain = 2.0f;

// Ganho de cada ENTRADA fisica (trim de entrada), aplicado antes do
// roteamento e da gravacao. Diferente do fader de track, este e destrutivo:
// entra no que for gravado. E o comportamento normal de um trim de canal.
constexpr float kDefaultInputGain = 1.0f;
constexpr float kMaxInputGain = 2.0f;

// Nome inicial de cada track (editavel na janela de controles e salvo em
// disco - ver Settings).
constexpr const char* kDefaultTrackNames[kNumTracks] = {"TRACK 1", "TRACK 2", "TRACK 3", "TRACK 4"};
constexpr int kMaxTrackNameLength = 18;

// Roteamento inicial: todas as tracks recebem todas as entradas (o
// comportamento que existia antes do mixer). Bitmask de canais - bit n = canal
// n ligado.
constexpr uint32_t kAllInputsMask = (1u << kNumChannels) - 1u;
constexpr uint32_t kDefaultInputMask = kAllInputsMask;

// Limiter de saida: so atua ACIMA deste pico (abaixo dele o sinal passa
// intacto). Substitui o tanh() que era aplicado em todas as amostras e
// distorcia o mix inteiro assim que a soma se aproximava de 1.0.
constexpr float kLimiterThreshold = 0.95f;
constexpr double kLimiterReleaseMs = 200.0;

// Monitoramento do sinal de entrada POR SOFTWARE. Deixe false se a sua
// interface de audio ja faz monitoramento direto por hardware - ligar os dois
// ao mesmo tempo soma o mesmo sinal duas vezes com atraso entre eles
// (comb filtering: som "de lata"/com flanger).
constexpr bool kDefaultSoftwareMonitoring = false;

// Fade aplicado ao parar/retomar o transporte, para nao dar "click".
constexpr double kTransportFadeMs = 8.0;

// Tamanho do ring buffer (em blocos de audio) entre o callback ASIO principal
// e a stream de saida do microfone virtual.
constexpr size_t kVirtualMicRingBlocks = 64;

// ---------------------------------------------------------------------------
// SERIAL (Mega)
// ---------------------------------------------------------------------------
// Nome da porta COM do Mega. Deixe vazio ("") para tentar auto-detectar pelo
// VID:PID do chip USB-serial na enumeracao de portas; defina um valor fixo
// (ex: "COM3") se a auto-deteccao nao funcionar no seu sistema.
constexpr const char* kComPortNameOverride = "";
// VID:PID do chip CH340 (comum em clones de Arduino Mega - confirmado no
// Gerenciador de Dispositivos como "USB-SERIAL CH340"). Se sua placa usar o
// ATmega16U2 original, troque para 0x2341 / 0x0042.
constexpr uint16_t kArduinoMegaVendorId = 0x1A86;
constexpr uint16_t kArduinoMegaProductId = 0x7523;

// ---------------------------------------------------------------------------
// MICROFONE VIRTUAL
// ---------------------------------------------------------------------------
// Nome do dispositivo de saida WASAPI do cabo de audio virtual (ex: VB-CABLE,
// instalado separadamente pelo usuario - ver docs/BUILD.md). O app procura um
// device cujo nome contenha esta substring.
constexpr const char* kVirtualCableDeviceNameContains = "CABLE Input";

} // namespace config
