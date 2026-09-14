// Uma track de looper: pilha de camadas (layers) estereo + estado.
//
// Cada uma das config::kMaxLayersPerTrack camadas e um buffer PRE-ALOCADO de
// tamanho fixo, alocado uma unica vez no startup (prepareBuffers(), fora da
// thread de audio). Nao ha pool compartilhado, nao ha alocacao em background,
// nao ha handoff entre threads - a track e dona direta de todos os seus
// slots, e reivindicar uma camada nova e so avancar layerCount_ (que tambem
// serve como indice do proximo slot livre, ja que camadas sao sempre
// reivindicadas em ordem e liberadas em ordem LIFO pelo undo).
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
#include <vector>

#include "Config.h"

enum class TrackState : uint8_t { EMPTY, RECORDING, PLAYING, MUTED };

class AudioTrack {
public:
    explicit AudioTrack(int trackIndex);

    // Chamado uma vez no startup do app (nao-RT): reserva as
    // config::kMaxLayersPerTrack camadas.
    void prepareBuffers();

    // Chamado quando o device de audio abre (nao-RT): ajusta a capacidade
    // util em frames a partir da sample rate REAL do driver. Se o device
    // rodar acima de config::kMaxSupportedSampleRate, a duracao maxima de
    // loop cai proporcionalmente (os buffers ja estao alocados).
    void setSampleRate(double sampleRate);

    // --- Transicoes de FSM (RT-safe, chamadas SO pela LooperEngine) ---

    // Reivindica uma camada nova e entra em RECORDING.
    //
    // masterLoopLengthSamples == 0 significa "esta captura e a que vai
    // DEFINIR o comprimento do loop mestre": o buffer nao e limpo (o
    // comprimento final ainda e desconhecido) e writeFrame sobrescreve, ja
    // que cada posicao e visitada exatamente uma vez, em ordem.
    //
    // masterLoopLengthSamples > 0: o trecho relevante do slot e ZERADO antes
    // do uso e writeFrame acumula. Zerar e obrigatorio - o slot pode conter
    // audio de um passe anterior que foi desfeito (peel) ou limpo, e sem
    // isso esse audio antigo voltava a tocar nos trechos que a nova captura
    // ainda nao tinha sobrescrito ("audio fantasma"). Isso acontecia SEMPRE
    // na camada base das tracks 2-4, porque elas comecam a gravar na posicao
    // atual do transporte, nunca em 0.
    //
    // Retorna false se o limite de camadas ja foi atingido.
    bool beginCapture(int64_t masterLoopLengthSamples);

    // Fecha a captura em andamento, commitando-a como camada nova -> PLAYING.
    // Retorna false se nao havia captura em andamento (no-op seguro).
    bool closeCapture();

    // Aborta a captura em andamento SEM commitar - volta para PLAYING (se ja
    // havia camadas) ou EMPTY.
    bool cancelCapture();

    // "Peel": remove a camada commitada mais recente, incluindo a base se for
    // a ultima. Pressionar repetidamente leva a track a EMPTY. Retorna false
    // se nao havia nada para remover. NAO mexe numa captura em andamento -
    // quem decide isso e a LooperEngine (ver comentario no topo).
    bool peelLastLayer();

    void setMuted(bool muted);

    // Limpa a track inteira -> EMPTY.
    void clearTrack();

    // --- Processamento por frame (RT-safe) ---

    // Grava um frame de entrada na camada ativa, na posicao dada (que a
    // LooperEngine ja compensou pela latencia do driver).
    void writeFrame(const float* inputFrame, int64_t position);

    // Soma as camadas desta track em outputFrame - as ja commitadas E a que
    // estiver em andamento (para o overdub ser ouvido ja na volta seguinte do
    // loop, e nao so depois de fechar o passe).
    //
    // soloOut (opcional) recebe a contribuicao SO desta track, ja com fader e
    // mute aplicados, para a versao plugin poder mandar cada track para uma
    // saida propria (uma track do mixer do FL). E escrito, nao somado, e vale
    // 0 quando a track esta muda ou vazia. Fica ANTES do limiter, que e um
    // processo de barramento e continua valendo so para o mix.
    void mixFrameInto(float* outputFrame, int64_t position, float* soloOut = nullptr);

    // --- Origem do VU meter ---
    //
    // Normalmente o medidor mostra o que a track REPRODUZ. Na track
    // selecionada (a que vai receber a proxima gravacao) ele passa a mostrar
    // a ENTRADA ja roteada, mesmo com a track parada ou vazia: e assim que da
    // para ver se ha sinal chegando e dosar o trim ANTES de apertar REC, em
    // vez de gravar no escuro e descobrir depois.
    void setMeterInput(bool meterInput) { meterInput_ = meterInput; }
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
    // nao as camadas separadas. Ao abrir, a track volta com UMA camada so:
    // e a diferenca de gravar aqui e trazer pronto de casa, entao o UNDO nao
    // desfaz camada por camada de uma musica aberta, ele apaga a track. O
    // fader nao entra na soma: o volume e guardado como numero, para poder ser
    // mexido depois de abrir sem estragar o audio.
    void readMix(float* dest, int64_t frames) const;
    void loadMix(const float* source, int64_t frames, bool muted);

    TrackState state() const { return state_; }
    bool isCapturing() const { return state_ == TrackState::RECORDING; }
    int layerCount() const { return layerCount_; }
    bool hasAudio() const { return layerCount_ > 0; }
    bool layersFull() const { return layerCount_ >= config::kMaxLayersPerTrack; }
    int trackIndex() const { return trackIndex_; }
    int64_t capacitySamples() const { return capacitySamples_; }

    // Nivel (peak com decay) do ultimo bloco, para o VU meter da GUI.
    float currentLevel() const { return level_.load(std::memory_order_relaxed); }

private:
    void updateLevel(float peak);

    // Aplica o roteamento de entrada: copia o frame de entrada para out,
    // zerando/somando canais conforme inputMask_.
    void routeInput(const float* in, float* out) const;

    int trackIndex_;
    TrackState state_ = TrackState::EMPTY;

    std::array<std::vector<float>, config::kMaxLayersPerTrack> slots_;
    int64_t allocatedFrames_ = 0;  // tamanho fisico dos slots
    int64_t capacitySamples_ = 0;  // capacidade util (<= allocatedFrames_), depende da sample rate real

    int layerCount_ = 0;
    float* activeBuffer_ = nullptr; // slots_[layerCount_].data() enquanto RECORDING

    // true quando a captura em andamento e a que esta definindo o loop mestre
    // (sobrescreve em vez de acumular - ver beginCapture).
    bool captureDefinesMaster_ = false;

    std::atomic<float> level_{0.0f};
    float levelDecayPerSample_ = 0.0f;

    bool meterInput_ = false; // so a thread de audio toca nisto

    std::atomic<uint32_t> inputMask_{config::kDefaultInputMask};
    std::atomic<float> gain_{config::kDefaultTrackGain};
};
