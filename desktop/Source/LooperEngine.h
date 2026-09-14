// FSM completa do looper (ver software/docs/CONTROL_MODEL.md para a
// especificacao em prosa). Todos os metodos publicos sao RT-safe e devem ser
// chamados a partir do callback de audio: handleButtonEvent() no inicio do
// bloco (drenando a fila vinda do SerialLink) e processFrame() uma vez por
// amostra.
//
// A engine e a UNICA dona da FSM. capturingTrack_ e a unica fonte de verdade
// sobre "quem esta gravando" e so muda junto com o estado da AudioTrack
// correspondente (startCapture/closeCapturingTrack/cancelCapturingTrack) -
// nunca deixe os dois divergirem.
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

#include "AudioTrack.h"
#include "Config.h"
#include "Messages.h"
#include "protocol.h"

// PLAY_MODE = "Mute Mode" na terminologia do manual do Sheeran Looper X: os
// botoes de track viram mute/unmute em vez de selecionar/armar.
enum class GlobalMode : uint8_t { REC_MODE, PLAY_MODE };

class LooperEngine {
public:
    LooperEngine();
    ~LooperEngine();

    // Nao-RT, chamado uma vez no startup do app: aloca a soma de cada track e
    // sobe a thread do LayerStore.
    void prepare();

    // RT-safe. Uma vez por bloco de audio, ANTES dos eventos de botao:
    // recebe as camadas que voltaram do LayerStore e avanca os desfazer em
    // andamento (ver AudioTrack::service).
    void serviceBlock();

    // Chamado quando o device de audio abre, com os valores REAIS do driver.
    // latencySamples = latencia de ida-e-volta (entrada + saida) reportada
    // pelo driver, que sera compensada na gravacao - ver processFrame().
    void setAudioDeviceInfo(double sampleRate, int64_t latencySamples);

    // Ajuste fino manual somado por cima do que o driver reporta, em ms.
    // Positivo adianta a gravacao (corrige overdub atrasado). Deixou de ser
    // uma constante de compilacao: ajustar o parametro mais importante do
    // looper exigia editar C++ e recompilar.
    void setLatencyTrimMs(double trimMs);
    double latencyTrimMs() const { return latencyTrimMs_; }

    // Monitoramento da entrada por software (ver processFrame). Tambem era
    // constante de compilacao.
    void setSoftwareMonitoring(bool enabled) {
        softwareMonitoring_.store(enabled, std::memory_order_relaxed);
    }
    bool softwareMonitoring() const { return softwareMonitoring_.load(std::memory_order_relaxed); }

    // RT-safe.
    void handleButtonEvent(protocol::ButtonId id, protocol::Gesture gesture);

    // perTrackOut (opcional) recebe kNumTracks * kNumChannels floats: a saida
    // isolada de cada track, na mesma ordem das tracks, ja com fader, mute e
    // fade de transporte, mas SEM o limiter (que e do barramento de mix). E o
    // que a versao plugin manda para uma track separada do mixer do FL.
    void processFrame(const float* input, float* output, float* perTrackOut = nullptr);

    // RT-safe. Chamar em loop ate retornar false para drenar todas as
    // mudancas de LED pendentes desde a ultima chamada a handleButtonEvent.
    bool consumeLedUpdate(LedCommand& out);

    // --- Mixer (chamado da thread da GUI, lock-free) ---
    void setTrackInputMask(int track, uint32_t mask) { tracks_[track].setInputMask(mask); }
    uint32_t trackInputMask(int track) const { return tracks_[track].inputMask(); }
    void setTrackGain(int track, float gain) { tracks_[track].setGain(gain); }
    float trackGain(int track) const { return tracks_[track].gain(); }

    // Trim de cada entrada fisica, aplicado antes do roteamento - ou seja,
    // entra no que for gravado (destrutivo, como um trim de canal de mesa).
    void setInputGain(int channel, float gain) {
        inputGain_[channel].store(gain, std::memory_order_relaxed);
    }
    float inputGain(int channel) const {
        return inputGain_[channel].load(std::memory_order_relaxed);
    }

    // Leitura para a GUI (lock-free, pode ser chamada de outra thread).
    GlobalMode mode() const { return mode_; }
    bool transportPlaying() const { return transportPlaying_; }
    int selectedTrack() const { return selectedTrack_; }
    TrackState trackState(int i) const { return tracks_[i].state(); }
    float trackLevel(int i) const { return tracks_[i].currentLevel(); }
    int trackLayers(int i) const { return tracks_[i].layerCount(); }
    // Para o teste: nenhum desfazer em andamento nesta track.
    bool trackLayersSettled(int i) const { return tracks_[i].layersSettled(); }
    // Memoria das camadas guardadas (RAM e disco) e orcamento de RAM.
    LayerStore& layerStore() { return layers_; }
    const LayerStore& layerStore() const { return layers_; }
    int64_t latencySamples() const { return latencySamples_; }
    // Progresso do loop mestre, 0..1 (para a barra de progresso das janelas).
    double loopProgress() const {
        return masterLoopLengthSamples_ > 0
                   ? static_cast<double>(transportPosition_) / static_cast<double>(masterLoopLengthSamples_)
                   : 0.0;
    }
    bool loopDefined() const { return masterLoopLengthSamples_ > 0; }
    int64_t masterLoopLength() const { return masterLoopLengthSamples_; }

    // --- Salvar / abrir a musica inteira (arquivo .loop - ver LoopFile) ---
    //
    // Nenhum dos dois e RT-safe: sao chamados da thread da GUI com o callback
    // de audio DESLIGADO (removeAudioCallback bloqueia ate o callback sair,
    // entao depois dele ninguem mais toca nos buffers). Foi essa a escolha em
    // vez de uma troca atomica de ponteiros: abrir um arquivo e uma acao rara
    // e explicita, e um engasgo de alguns milissegundos ali e barato perto da
    // complexidade de fazer isso sem parar o audio.
    void readTrackMix(int track, float* dest, int64_t frames) const {
        tracks_[track].readMix(dest, frames);
    }
    bool trackMuted(int track) const { return tracks_[track].state() == TrackState::MUTED; }

    // audio[i] == nullptr = track vazia. Entra PARADO no comeco do loop: abrir
    // um arquivo nao pode disparar som sozinho no meio de uma feira - quem
    // manda tocar e o PLAY.
    void applyLoadedSession(int64_t lengthSamples, const float* const* audio, const bool* muted);

    // --- Espelho do pedal na tela (ver PedalMap) ---
    //
    // Os LEDs sao expostos aqui em vez de a GUI consumir a fila de LedCommand:
    // aquela fila e SPSC e o SerialLink ja e o consumidor dela. Assim o mapa
    // mostra exatamente o que o app manda para o pedal, o que tambem serve de
    // diagnostico quando o LED fisico nao bate.
    protocol::LedColor ledColor(int track) const { return desiredColor_[track]; }
    bool ledBlink(int track) const { return desiredBlink_[track]; }

    // Ha quanto tempo (em frames de audio) o botao foi pressionado pela ultima
    // vez. Serve para acender o botao no mapa por alguns instantes - o
    // firmware atual so envia o press, nao o release, entao "segurando" e
    // aproximado por "foi pressionado ha pouco".
    int64_t framesSincePress(int button) const {
        const int64_t at = lastPressFrame_[button].load(std::memory_order_relaxed);
        return at < 0 ? std::numeric_limits<int64_t>::max() : framesElapsed_.load(std::memory_order_relaxed) - at;
    }
    double sampleRate() const { return sampleRate_; }
    double latencyMs() const {
        return sampleRate_ > 0.0 ? 1000.0 * static_cast<double>(latencySamples_) / sampleRate_ : 0.0;
    }
    double loopLengthSeconds() const {
        return sampleRate_ > 0.0 ? static_cast<double>(masterLoopLengthSamples_) / sampleRate_ : 0.0;
    }
    double loopPositionSeconds() const {
        return sampleRate_ > 0.0 ? static_cast<double>(transportPosition_) / sampleRate_ : 0.0;
    }

private:
    void handleTrackButton(int n);
    void handleRecPlay();
    void handleStop();
    void handleUndo();
    void resumeTransport();
    bool closeCapturingTrack();
    bool cancelCapturingTrack();
    void startCapture(int trackIndex);
    void clearAll();
    // O UNICO lugar que muda masterLoopLengthSamples_: avisa as tracks e o
    // LayerStore (que passa a preparar buffers do tamanho novo).
    void setLoopLength(int64_t frames);
    void recomputeLeds();
    void recomputeLatency();
    void updateMetering();
    void applyLimiter(float* frame);
    int64_t writePositionFor(int64_t readPosition) const;

    // Declarado ANTES das tracks: elas guardam um ponteiro para ele, entao ele
    // tem de nascer antes e morrer depois delas.
    LayerStore layers_;
    AudioTrack tracks_[config::kNumTracks];

    GlobalMode mode_ = GlobalMode::REC_MODE;
    int selectedTrack_ = 0;
    int capturingTrack_ = -1; // -1 = nenhuma track capturando
    int64_t masterLoopLengthSamples_ = 0; // 0 = "primordial" (nenhuma track fechada ainda)
    int64_t transportPosition_ = 0;
    bool transportPlaying_ = true;

    // Rewind adiado: STOP faz fade-out primeiro e so entao volta a posicao
    // para 0, senao o fade tocaria o inicio do loop (click audivel).
    bool pendingRewind_ = false;

    double sampleRate_ = config::kPreferredSampleRate;
    // A latencia crua do driver e o trim do usuario ficam separados para os
    // dois poderem mudar independente; latencySamples_ e a soma ja limitada.
    int64_t deviceLatencySamples_ = 0;
    double latencyTrimMs_ = config::kDefaultLatencyTrimMs;
    int64_t latencySamples_ = 0;

    std::atomic<bool> softwareMonitoring_{config::kDefaultSoftwareMonitoring};

    // Contador livre de frames: nao da a volta como transportPosition_, entao
    // serve para medir intervalos (piscar do botao no mapa; mais adiante, o
    // duplo-toque do solo).
    std::atomic<int64_t> framesElapsed_{0};
    std::atomic<int64_t> lastPressFrame_[protocol::kButtonCount];

    std::atomic<float> inputGain_[config::kNumChannels];

    // Track cujo VU esta mostrando a entrada em vez da reproducao (-1 =
    // nenhuma). Ver AudioTrack::setMeterInput.
    int meteringTrack_ = -1;

    float transportGain_ = 1.0f;
    float transportFadeStep_ = 1.0f;

    float limiterGain_ = 1.0f;
    float limiterRelease_ = 0.0f;

    protocol::LedColor desiredColor_[config::kNumTracks] = {};
    bool desiredBlink_[config::kNumTracks] = {};
    bool ledDirty_[config::kNumTracks] = {};
};
