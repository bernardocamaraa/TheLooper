// Uma track de looper: camadas (layers) estereo + estado.
//
// CAMADAS INFINITAS. A track toca UMA soma pre-mixada (mix_) com todas as
// camadas fechadas e o passe em andamento - o custo por amostra e o mesmo com
// 1 ou 500 camadas. Cada camada tambem fica guardada SEPARADA, so para poder
// ser desfeita: as mais recentes aqui, num array fixo (recent_), e as mais
// antigas com o LayerStore, que as despeja em disco quando passam do
// orcamento de RAM (ver LayerStore.h). A thread de audio nunca aloca, zera ou
// espera: os buffers chegam prontos e zerados do LayerStore.
//
// Gravar escreve a entrada em DOIS lugares: na camada (capture_, para o
// desfazer) e direto na soma (para o overdub ser ouvido ja na volta seguinte).
// Desfazer tira a camada da soma AOS POUCOS, um pedaco por bloco (service());
// enquanto isso a leitura subtrai o que falta na hora, entao o som some
// imediatamente. Desfazer a ULTIMA camada troca a soma por uma zerada, em vez
// de subtrair: a track fica em silencio exato, sem residuo numerico.
//
// Estado unificado: gravar a camada base e gravar uma camada de overdub NAO
// sao estados separados - as duas sao apenas RECORDING.
//
// Todas as tracks compartilham UMA UNICA posicao de transporte (mantida pela
// LooperEngine) para garantir sincronia rigorosa - ver docs/CONTROL_MODEL.md.
//
// A LooperEngine e a UNICA dona da FSM: a track nunca muda de estado por
// conta propria. (A versao anterior deixava peelLastLayer() cancelar uma
// captura em andamento sem avisar a engine, que continuava achando que a
// track estava gravando - isso corrompia o comprimento do loop mestre e
// deixava tracks "tocando" em silencio.)
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "Config.h"
#include "LayerStore.h"

enum class TrackState : uint8_t { EMPTY, RECORDING, PLAYING, MUTED };

class AudioTrack {
public:
    explicit AudioTrack(int trackIndex);
    ~AudioTrack();

    AudioTrack(const AudioTrack&) = delete;
    AudioTrack& operator=(const AudioTrack&) = delete;

    // Chamado uma vez no startup do app (nao-RT): aloca a soma da track e
    // passa a pegar os buffers de camada do LayerStore.
    void prepareBuffers(LayerStore* store);

    // Chamado quando o device de audio abre (nao-RT): ajusta a capacidade
    // util em frames a partir da sample rate REAL do driver. Se o device
    // rodar acima de config::kMaxSupportedSampleRate, a duracao maxima de
    // loop cai proporcionalmente (os buffers ja estao alocados).
    void setSampleRate(double sampleRate);

    // Comprimento do loop mestre (0 = ainda nao definido). A LooperEngine
    // avisa sempre que ele muda; e o trecho que o desfazer percorre.
    void setLoopLength(int64_t frames) { loopLength_ = frames; }

    // --- Transicoes de FSM (RT-safe, chamadas SO pela LooperEngine) ---

    // Pega um buffer zerado para a camada nova e entra em RECORDING.
    // masterLoopLengthSamples == 0 significa "esta captura e a que vai DEFINIR
    // o comprimento do loop mestre" (buffer cheio, ja que o comprimento ainda
    // e desconhecido, e ela nao toca enquanto grava - ver mixFrameInto).
    // Retorna false so se o LayerStore nao tiver buffer pronto.
    bool beginCapture(int64_t masterLoopLengthSamples);

    // Fecha a captura em andamento, commitando-a como camada nova -> PLAYING.
    // Retorna false se nao havia captura em andamento (no-op seguro).
    bool closeCapture();

    // Overdub sem parar: fecha a volta que terminou como camada e continua
    // gravando numa camada nova, sem mudar de estado. Chamado pela LooperEngine
    // quando a escrita passa pelo comeco do loop. Retorna false (e a volta
    // seguinte continua na mesma camada) se nao houver buffer pronto.
    bool splitCapture();

    // Aborta a captura em andamento SEM commitar (tira da soma o que ela ja
    // tinha gravado) - volta para PLAYING (se ja havia camadas) ou EMPTY.
    bool cancelCapture();

    // "Peel": remove a camada commitada mais recente, incluindo a base se for
    // a ultima. Pressionar repetidamente leva a track a EMPTY. Retorna false
    // se nao havia nada para remover. NAO mexe numa captura em andamento -
    // quem decide isso e a LooperEngine (ver comentario no topo).
    bool peelLastLayer();

    void setMuted(bool muted);

    // Limpa a track inteira -> EMPTY.
    void clearTrack();

    // --- Manutencao (RT-safe, uma vez por bloco de audio) ---

    // Avanca os desfazer em andamento e troca camadas com o LayerStore.
    void service();

    // Uma camada pedida de volta ao LayerStore chegou.
    void onRestored(const LayerRestore& restore);

    // --- Processamento por frame (RT-safe) ---

    // Grava um frame de entrada na camada ativa, na posicao dada (que a
    // LooperEngine ja compensou pela latencia do driver).
    void writeFrame(const float* inputFrame, int64_t position);

    // Soma esta track em outputFrame.
    //
    // soloOut (opcional) recebe a contribuicao SO desta track, ja com fader e
    // mute aplicados, para a versao plugin poder mandar cada track para uma
    // saida propria (uma track do mixer do FL). E escrito, nao somado, e vale
    // 0 quando a track esta muda ou vazia. Fica ANTES do limiter, que e um
    // processo de barramento e continua valendo so para o mix.
    //
    // meterScale multiplica o nivel do VU (a LooperEngine passa o ganho do
    // fade de transporte): com o transporte parado o medidor cai a zero.
    void mixFrameInto(float* outputFrame, int64_t position, float* soloOut = nullptr,
                      float meterScale = 1.0f);

    // --- VU meter ---
    //
    // O medidor mostra o que a track REPRODUZ (pos-fader) - inclusive a
    // MUTADA, que continua medida (a interface pinta de cinza) e so nao vai
    // para a saida. Na track SELECIONADA (e na que grava) ele mostra o que toca
    // E o que chega na entrada, o maior dos dois: da para conferir o sinal
    // antes de gravar sem deixar de ver a reproducao. (Ja foi so a entrada, e
    // depois so a reproducao; o usuario quer os dois juntos.)
    //
    // Chamado pela LooperEngine a cada frame, ANTES de mixFrameInto, que e
    // quem fecha o nivel do frame.
    void meterInputFrame(const float* inputFrame);

    // --- Mixer (chamado da thread da GUI; lido da thread de audio) ---
    //
    // Os dois sao atomicos porque a GUI escreve enquanto o callback de audio
    // le. Nao ha lock: um valor "meio antigo" por um bloco de audio e
    // inofensivo, e travar o callback nao seria.

    // Bitmask das entradas fisicas que alimentam esta track (bit n = canal n).
    // Ver routeInput() para o que acontece quando so uma entrada esta ligada.
    void setInputMask(uint32_t mask) { inputMask_.store(mask, std::memory_order_relaxed); }
    uint32_t inputMask() const { return inputMask_.load(std::memory_order_relaxed); }

    // Volume de reproducao da track (fader da mesa). Nao afeta o que foi
    // gravado - so o quanto essa track pesa no mix, entao mexer nele depois
    // nao estraga nada.
    void setGain(float gain) { gain_.store(gain, std::memory_order_relaxed); }
    float gain() const { return gain_.load(std::memory_order_relaxed); }

    // --- Sessao em disco (arquivo .loop - ver LoopFile) ------------------
    //
    // NAO sao RT-safe: quem chama tem de desligar o callback de audio antes
    // (ver MainComponent::saveLoop/openLoop).
    //
    // O que vai para o arquivo e a SOMA das camadas - o que a track toca -, e
    // nao as camadas separadas. Ao abrir, a track volta com UMA camada so
    // (sem buffer proprio: ela e a soma inteira), entao o UNDO nao desfaz
    // camada por camada de uma musica aberta, ele apaga a track. O fader nao
    // entra na soma: o volume e guardado como numero.
    void readMix(float* dest, int64_t frames) const;
    void loadMix(const float* source, int64_t frames, bool muted);

    TrackState state() const { return state_; }
    bool isCapturing() const { return state_ == TrackState::RECORDING; }
    // Camadas que a track tem (para a GUI; lock-free).
    int layerCount() const { return publishedLayers_.load(std::memory_order_relaxed); }
    bool hasAudio() const { return effectiveLayers() > 0; }
    int trackIndex() const { return trackIndex_; }
    int64_t capacitySamples() const { return capacitySamples_; }

    // Nenhum desfazer em andamento nem esperando camada voltar do disco.
    bool layersSettled() const { return peelCount_ == 0 && pendingUndos_ == 0; }

    // Nivel (peak com decay) do ultimo bloco, para o VU meter da GUI.
    float currentLevel() const { return level_.load(std::memory_order_relaxed); }

private:
    struct Peel {
        LayerBuffer* buffer = nullptr;
        int64_t done = 0;    // amostras (nao frames) ja subtraidas da soma
        int64_t frames = 0;  // trecho que a camada ocupa
    };

    int committedLayers() const { return recentCount_ + deepCount_ + (baseOnly_ ? 1 : 0); }
    int effectiveLayers() const { return committedLayers() - pendingUndos_; }
    void publishCount();
    void pushRecent(LayerBuffer* layer);
    void startPeel(LayerBuffer* layer);
    void finishPeel(int index);
    // Buffer zerado para uma camada: da reserva da track, senao do LayerStore.
    LayerBuffer* takeLayerBuffer(bool full);
    void topUpReserve();
    void resetMix();
    void updateLevel(float peak);

    // Aplica o roteamento de entrada: copia o frame de entrada para out,
    // zerando/somando canais conforme inputMask_.
    void routeInput(const float* in, float* out) const;

    int trackIndex_;
    TrackState state_ = TrackState::EMPTY;

    LayerStore* store_ = nullptr;
    int64_t allocatedFrames_ = 0;  // tamanho fisico da soma
    int64_t capacitySamples_ = 0;  // capacidade util (<= allocatedFrames_), depende da sample rate real
    int64_t loopLength_ = 0;

    LayerBuffer* mix_ = nullptr;     // soma das camadas fechadas + o passe atual
    int64_t mixDirtyFrames_ = 0;     // ate onde a soma ja recebeu audio

    LayerBuffer* capture_ = nullptr; // camada em gravacao
    // true quando a captura em andamento e a que esta definindo o loop mestre.
    bool captureDefinesMaster_ = false;

    // Camadas recentes, da mais antiga [0] para a mais nova.
    std::array<LayerBuffer*, config::kRecentLayerSlots> recent_{};
    int recentCount_ = 0;
    int deepCount_ = 0;       // guardadas no LayerStore (RAM ou disco)
    bool baseOnly_ = false;   // camada base sem buffer (sessao aberta de arquivo)

    std::array<Peel, config::kMaxPendingPeels> peels_{};
    int peelCount_ = 0;

    // Buffers do tamanho do loop ja na mao (ver config::kTrackReserveBuffers).
    std::array<LayerBuffer*, config::kTrackReserveBuffers> reserve_{};
    int reserveCount_ = 0;

    // Desfazer pedidos quando a camada a tirar ainda estava no LayerStore:
    // sao aplicados quando ela volta (onRestored).
    int pendingUndos_ = 0;
    bool refillOutstanding_ = false;
    int refillExpected_ = 0;
    // Muda a cada limpeza: camadas pedidas antes dela chegam com a geracao
    // velha e sao descartadas.
    uint32_t generation_ = 0;

    std::atomic<int> publishedLayers_{0};

    std::atomic<float> level_{0.0f};
    // Pico da entrada neste frame (gravando ou selecionada); mixFrameInto soma
    // com o da reproducao e zera. So a thread de audio toca nisto.
    float framePeak_ = 0.0f;
    float levelDecayPerSample_ = 0.0f;

    std::atomic<uint32_t> inputMask_{config::kDefaultInputMask};
    std::atomic<float> gain_{config::kDefaultTrackGain};
};
