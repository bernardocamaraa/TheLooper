#include "LooperEngine.h"

#include <algorithm>
#include <cmath>

LooperEngine::LooperEngine() : tracks_{AudioTrack(0), AudioTrack(1), AudioTrack(2), AudioTrack(3)} {
    for (auto& gain : inputGain_) {
        gain.store(config::kDefaultInputGain, std::memory_order_relaxed);
    }
    for (auto& frame : lastPressFrame_) {
        frame.store(-1, std::memory_order_relaxed); // -1 = nunca pressionado
    }
}

void LooperEngine::prepare() {
    for (auto& t : tracks_) {
        t.prepareBuffers();
    }
    setAudioDeviceInfo(config::kPreferredSampleRate, 0);
    recomputeLeds();
    updateMetering();
}

void LooperEngine::setAudioDeviceInfo(double sampleRate, int64_t latencySamples) {
    if (sampleRate <= 0.0) {
        return;
    }
    sampleRate_ = sampleRate;
    for (auto& t : tracks_) {
        t.setSampleRate(sampleRate);
    }

    deviceLatencySamples_ = latencySamples;
    recomputeLatency();

    transportFadeStep_ =
        1.0f / std::max(1.0f, static_cast<float>(config::kTransportFadeMs * 0.001 * sampleRate));
    limiterRelease_ =
        1.0f - std::exp(-1.0f / static_cast<float>(config::kLimiterReleaseMs * 0.001 * sampleRate));
}

void LooperEngine::setLatencyTrimMs(double trimMs) {
    latencyTrimMs_ = trimMs;
    recomputeLatency();
}

void LooperEngine::recomputeLatency() {
    // Teto de seguranca: drivers bugados as vezes reportam valores absurdos, e
    // o trim vem de um controle que o usuario pode arrastar longe demais.
    const int64_t trim = static_cast<int64_t>(latencyTrimMs_ * 0.001 * sampleRate_);
    const int64_t maxLatency = static_cast<int64_t>(config::kMaxLatencyMs * 0.001 * sampleRate_);
    latencySamples_ = std::clamp<int64_t>(deviceLatencySamples_ + trim, 0, maxLatency);
}

// ---------------------------------------------------------------------------
// Botoes
// ---------------------------------------------------------------------------

void LooperEngine::handleButtonEvent(protocol::ButtonId id, protocol::Gesture gesture) {
    // Registrado ANTES de agir: o mapa do pedal acende o botao mesmo quando o
    // gesto acaba sendo ignorado pela FSM, que e o que se quer para conferir
    // que o toque chegou.
    if (id < protocol::kButtonCount) {
        lastPressFrame_[id].store(framesElapsed_.load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
    }

    switch (id) {
        case protocol::kButtonMode:
            // Alterna REC_MODE <-> Mute Mode. Nao mexe no transporte nem nas
            // tracks - so muda o que os botoes de track fazem.
            if (gesture == protocol::kGesturePress) {
                mode_ = (mode_ == GlobalMode::REC_MODE) ? GlobalMode::PLAY_MODE : GlobalMode::REC_MODE;
            }
            break;

        case protocol::kButtonUndo: // fisicamente rotulado "CLEAR" no pedal
            if (gesture == protocol::kGesturePress) {
                handleUndo();
            } else if (gesture == protocol::kGestureLongPress) {
                clearAll();
            }
            break;

        case protocol::kButtonPause: // = STOP - so para, nao e toggle
            if (gesture == protocol::kGesturePress) {
                handleStop();
            }
            break;

        case protocol::kButtonRecPlay:
            if (gesture == protocol::kGesturePress) {
                if (mode_ == GlobalMode::PLAY_MODE) {
                    // Em Mute Mode, REC_PLAY assume a funcao de PLAY: retoma
                    // do inicio. Fecha qualquer captura viva ANTES de mexer na
                    // posicao - resetar o transporte com uma track gravando
                    // corrompia a camada e o comprimento do loop.
                    closeCapturingTrack();
                    resumeTransport();
                    transportPosition_ = 0;
                    pendingRewind_ = false;
                } else {
                    handleRecPlay();
                }
            }
            break;

        case protocol::kButtonTrack1:
        case protocol::kButtonTrack2:
        case protocol::kButtonTrack3:
        case protocol::kButtonTrack4:
            if (gesture == protocol::kGesturePress) {
                handleTrackButton(static_cast<int>(id) - static_cast<int>(protocol::kButtonTrack1));
            }
            break;

        default:
            break;
    }

    recomputeLeds();
    updateMetering();
}

void LooperEngine::updateMetering() {
    // Em REC_MODE a track selecionada e a que vai receber a proxima gravacao:
    // o VU dela mostra a ENTRADA roteada, para dar para conferir sinal e
    // dosar o trim antes de apertar REC. Gravando, writeFrame ja alimenta o
    // medidor com a entrada, entao nao ha o que trocar.
    //
    // O criterio bate de proposito com o da moldura vermelha de selecao: o
    // medidor que muda de significado e exatamente o que esta destacado.
    const int metering =
        (mode_ == GlobalMode::REC_MODE && capturingTrack_ < 0) ? selectedTrack_ : -1;
    if (metering == meteringTrack_) {
        return;
    }
    meteringTrack_ = metering;
    for (int i = 0; i < config::kNumTracks; ++i) {
        tracks_[i].setMeterInput(i == meteringTrack_);
    }
}

void LooperEngine::resumeTransport() {
    transportPlaying_ = true;
    if (pendingRewind_) {
        transportPosition_ = 0;
        pendingRewind_ = false;
    }
}

void LooperEngine::handleStop() {
    // STOP funciona a qualquer momento, independente do modo. Se algo estiver
    // capturando, fecha antes de parar. O rewind para 0 e adiado ate o
    // fade-out terminar (ver processFrame) para nao dar click.
    closeCapturingTrack();
    transportPlaying_ = false;
    pendingRewind_ = true;
}

void LooperEngine::handleRecPlay() {
    // REC_PLAY sempre retoma o transporte. Sem isto, depois de um STOP o app
    // ficava mudo para sempre em REC_MODE: iniciava a captura mas
    // processFrame() saia logo no inicio por !transportPlaying_, entao nada
    // era gravado nem tocado e nenhum outro botao religava o transporte.
    resumeTransport();

    const int s = selectedTrack_;

    if (capturingTrack_ == s) {
        // Se ainda estamos "primordiais", esta captura e a que esta definindo
        // o comprimento do loop - ao fecha-la, continua direto numa nova
        // captura (overdub imediato).
        const bool wasDefiningMaster = (masterLoopLengthSamples_ == 0);
        closeCapturingTrack();
        if (wasDefiningMaster) {
            startCapture(s);
        }
        return;
    }

    // Invariante: se capturingTrack_ != s entao capturingTrack_ == -1. O
    // fecha defensivo abaixo garante isso mesmo se algo escapar.
    closeCapturingTrack();
    startCapture(s);
}

void LooperEngine::handleTrackButton(int n) {
    if (mode_ == GlobalMode::PLAY_MODE) {
        AudioTrack& t = tracks_[n];
        t.setMuted(t.state() != TrackState::MUTED);
        return;
    }

    // REC_MODE: o botao de track e SEMPRE e SO uma acao de selecao, exceto
    // pelo atalho abaixo quando ha OUTRA track capturando no momento.
    if (capturingTrack_ >= 0 && capturingTrack_ != n) {
        closeCapturingTrack(); // A -> PLAYING
        selectedTrack_ = n;
        resumeTransport();
        startCapture(n);       // B entra em captura
        return;
    }

    selectedTrack_ = n;
}

void LooperEngine::handleUndo() {
    if (capturingTrack_ >= 0 && capturingTrack_ == selectedTrack_) {
        // Passe em andamento: cancela o passe em vez de remover uma camada ja
        // commitada. capturingTrack_ TEM de ser zerado junto - antes a track
        // saia de RECORDING sozinha e a engine continuava achando que ela
        // estava gravando, o que definia o loop mestre com o comprimento de
        // um passe cancelado e deixava a track "tocando" em silencio.
        cancelCapturingTrack();
        return;
    }

    if (!tracks_[selectedTrack_].peelLastLayer()) {
        return;
    }

    // Se sobrou audio em alguma track, o comprimento do loop mestre continua
    // valendo. Se TODAS ficaram vazias, esse comprimento viraria invisivel e
    // inalteravel (so o Clear All o resetava), entao voltamos ao estado
    // primordial para a proxima gravacao poder definir um loop novo.
    bool anyAudio = false;
    for (const auto& t : tracks_) {
        if (t.hasAudio()) {
            anyAudio = true;
            break;
        }
    }
    if (!anyAudio) {
        masterLoopLengthSamples_ = 0;
        transportPosition_ = 0;
        pendingRewind_ = false;
    }
}

// ---------------------------------------------------------------------------
// Captura
// ---------------------------------------------------------------------------

bool LooperEngine::closeCapturingTrack() {
    if (capturingTrack_ < 0) {
        return false;
    }
    const int idx = capturingTrack_;
    capturingTrack_ = -1;

    if (masterLoopLengthSamples_ == 0) {
        // Este passe e o que define o comprimento do loop mestre (pode ser
        // qualquer track, nao so a Track1).
        const int64_t length = std::min(transportPosition_, tracks_[idx].capacitySamples());
        if (length <= 0) {
            // Dois toques em REC_PLAY praticamente no mesmo bloco de audio:
            // nao ha o que commitar. Descarta o passe em vez de criar uma
            // camada de comprimento zero (que deixaria a track "com audio"
            // sem audio nenhum e travaria o estado primordial).
            tracks_[idx].cancelCapture();
            transportPosition_ = 0;
            return false;
        }
        if (!tracks_[idx].closeCapture()) {
            return false;
        }
        masterLoopLengthSamples_ = length;
        // Posiciona o transporte de forma que o PONTEIRO DE ESCRITA
        // (transportPosition_ - latencySamples_) caia exatamente em 0: o
        // proximo passe continua sem emenda de onde este parou, ja compensado
        // pela latencia do driver.
        transportPosition_ = latencySamples_ % masterLoopLengthSamples_;
        return true;
    }

    return tracks_[idx].closeCapture();
}

bool LooperEngine::cancelCapturingTrack() {
    if (capturingTrack_ < 0) {
        return false;
    }
    const int idx = capturingTrack_;
    capturingTrack_ = -1;
    tracks_[idx].cancelCapture();
    if (masterLoopLengthSamples_ == 0) {
        transportPosition_ = 0; // o passe cancelado era o que definiria o loop
    }
    return true;
}

void LooperEngine::startCapture(int trackIndex) {
    if (capturingTrack_ >= 0) {
        return; // ja ha captura em andamento - a engine deve fechar antes
    }
    AudioTrack& t = tracks_[trackIndex];
    if (t.state() == TrackState::MUTED) {
        t.setMuted(false); // gravar numa track mutada tem que desmuta-la
    }
    if (masterLoopLengthSamples_ == 0) {
        transportPosition_ = 0; // o passe que define o loop grava a partir de 0
    }
    if (t.beginCapture(masterLoopLengthSamples_)) {
        capturingTrack_ = trackIndex;
    }
    // Se beginCapture falhar (limite de camadas), simplesmente nao inicia - a
    // track continua tocando o que ja tinha.
}

void LooperEngine::applyLoadedSession(int64_t lengthSamples, const float* const* audio,
                                       const bool* muted) {
    clearAll();

    if (lengthSamples <= 0) {
        recomputeLeds();
        updateMetering();
        return;
    }

    for (int i = 0; i < config::kNumTracks; ++i) {
        if (audio[i] != nullptr) {
            tracks_[i].loadMix(audio[i], lengthSamples, muted[i]);
        }
    }

    masterLoopLengthSamples_ = lengthSamples;
    transportPosition_ = 0;
    // Parado e com o fade zerado: a musica so comeca quando mandarem tocar.
    transportPlaying_ = false;
    transportGain_ = 0.0f;
    pendingRewind_ = false;

    recomputeLeds();
    updateMetering();
}

void LooperEngine::clearAll() {
    for (auto& t : tracks_) {
        t.clearTrack();
    }
    // Nao mexe em mode_: Clear All so limpa o audio.
    selectedTrack_ = 0;
    capturingTrack_ = -1;
    masterLoopLengthSamples_ = 0;
    transportPosition_ = 0;
    pendingRewind_ = false;
    transportPlaying_ = true;
    limiterGain_ = 1.0f;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

int64_t LooperEngine::writePositionFor(int64_t readPosition) const {
    if (masterLoopLengthSamples_ <= 0) {
        return readPosition; // passe que define o loop: escreve em sequencia
    }
    // Compensacao de latencia: o que chega na entrada AGORA e a resposta do
    // musico ao que saiu pelos alto-falantes latencySamples_ atras, entao tem
    // de ser gravado naquela posicao - nao na atual. Sem isto toda camada de
    // overdub entra atrasada, e o erro se acumula a cada camada (o classico
    // "o loop desanda").
    int64_t wp = (readPosition - latencySamples_) % masterLoopLengthSamples_;
    if (wp < 0) {
        wp += masterLoopLengthSamples_;
    }
    return wp;
}

void LooperEngine::processFrame(const float* input, float* output, float* perTrackOut) {
    framesElapsed_.fetch_add(1, std::memory_order_relaxed);

    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        output[ch] = 0.0f;
    }

    // Fade de transporte (evita click no STOP/PLAY).
    const float targetGain = transportPlaying_ ? 1.0f : 0.0f;
    if (transportGain_ < targetGain) {
        transportGain_ = std::min(targetGain, transportGain_ + transportFadeStep_);
    } else if (transportGain_ > targetGain) {
        transportGain_ = std::max(targetGain, transportGain_ - transportFadeStep_);
    }

    // Trim de entrada aplicado ANTES de qualquer outra coisa, entao vale tanto
    // para o que e gravado quanto para o monitoramento por software.
    float trimmed[config::kNumChannels];
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        trimmed[ch] = input[ch] * inputGain_[ch].load(std::memory_order_relaxed);
    }

    if (transportPlaying_ && capturingTrack_ >= 0) {
        tracks_[capturingTrack_].writeFrame(trimmed, writePositionFor(transportPosition_));
    }

    // VU da track selecionada mostrando a entrada. Roda mesmo com o
    // transporte parado - conferir sinal e justamente algo que se faz antes
    // de comecar.
    if (meteringTrack_ >= 0) {
        tracks_[meteringTrack_].meterInputFrame(trimmed);
    }

    for (int i = 0; i < config::kNumTracks; ++i) {
        float* solo = (perTrackOut != nullptr) ? perTrackOut + i * config::kNumChannels : nullptr;
        tracks_[i].mixFrameInto(output, transportPosition_, solo);
    }
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        output[ch] *= transportGain_;
    }
    // O mesmo fade de STOP/PLAY nas saidas isoladas - senao elas dariam o
    // click que o fade existe para evitar, so que na track do mixer.
    if (perTrackOut != nullptr) {
        for (int i = 0; i < config::kNumTracks * config::kNumChannels; ++i) {
            perTrackOut[i] *= transportGain_;
        }
        // Dobra em mono pelo mesmo motivo do mix (ver abaixo). Na pratica ja
        // chega centrado, porque o roteamento grava mono nos dois canais - e a
        // media, entao nao muda nada quando os canais ja sao iguais.
        if (config::kMonoOutput) {
            for (int t = 0; t < config::kNumTracks; ++t) {
                float* frame = perTrackOut + t * config::kNumChannels;
                float sum = 0.0f;
                for (int ch = 0; ch < config::kNumChannels; ++ch) {
                    sum += frame[ch];
                }
                const float mono = sum / static_cast<float>(config::kNumChannels);
                for (int ch = 0; ch < config::kNumChannels; ++ch) {
                    frame[ch] = mono;
                }
            }
        }
    }

    // Monitoramento por software (desligado por padrao - ver Config.h): a
    // maioria das interfaces ja faz monitoramento direto por hardware, e somar
    // os dois causa comb filtering (som "de lata").
    if (softwareMonitoring_.load(std::memory_order_relaxed)) {
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            output[ch] += trimmed[ch];
        }
    }

    // Dobra em MONO (ver config::kMonoOutput): os dois canais saem iguais.
    //
    // E a MEDIA, nao a soma. Tudo o que chega aqui ja e material centrado (o
    // roteamento grava mono nos dois canais), entao a media devolve o proprio
    // sinal, sem os +6 dB que a soma daria. O unico caminho que ainda traz
    // canais diferentes e o monitoramento por software, e para ele a media e
    // exatamente a dobra correta.
    if (config::kMonoOutput) {
        float sum = 0.0f;
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            sum += output[ch];
        }
        const float mono = sum / static_cast<float>(config::kNumChannels);
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            output[ch] = mono;
        }
    }

    applyLimiter(output);

    // Avanca o transporte (tambem durante o fade-out, para o fade sair suave).
    if (transportPlaying_ || transportGain_ > 0.0f) {
        ++transportPosition_;
        if (masterLoopLengthSamples_ > 0) {
            if (transportPosition_ >= masterLoopLengthSamples_) {
                transportPosition_ = 0;
            }
        } else if (capturingTrack_ >= 0) {
            // Passe que define o loop: para no teto de kMaxLoopSeconds em vez
            // de contar indefinidamente. Antes transportPosition_ crescia sem
            // limite e o comprimento do loop podia acabar MAIOR que o buffer,
            // levando a leitura e escrita fora dos limites - as violacoes de
            // acesso (0xc0000005) e a corrupcao de heap (0xc0000374) que
            // apareciam no Windows Error Reporting.
            if (transportPosition_ >= tracks_[capturingTrack_].capacitySamples()) {
                closeCapturingTrack();
                recomputeLeds();
                updateMetering();
            }
        } else {
            transportPosition_ = 0; // nada gravado e nada definido: fica em 0
        }
    }

    if (!transportPlaying_ && transportGain_ <= 0.0f && pendingRewind_) {
        transportPosition_ = 0;
        pendingRewind_ = false;
    }
}

void LooperEngine::applyLimiter(float* frame) {
    float peak = 0.0f;
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        peak = std::max(peak, std::fabs(frame[ch]));
    }

    // Ataque instantaneo / release lento. Abaixo do threshold o sinal passa
    // INTACTO - diferente do tanh() anterior, que era aplicado em todas as
    // amostras e distorcia o mix inteiro assim que a soma das tracks se
    // aproximava de 1.0.
    const float desired =
        (peak > config::kLimiterThreshold) ? (config::kLimiterThreshold / peak) : 1.0f;
    if (desired < limiterGain_) {
        limiterGain_ = desired;
    } else {
        limiterGain_ += (desired - limiterGain_) * limiterRelease_;
    }

    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        frame[ch] *= limiterGain_;
    }
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

bool LooperEngine::consumeLedUpdate(LedCommand& out) {
    for (int i = 0; i < config::kNumTracks; ++i) {
        if (ledDirty_[i]) {
            ledDirty_[i] = false;
            out.trackId = i;
            out.color = desiredColor_[i];
            out.blink = desiredBlink_[i];
            return true;
        }
    }
    return false;
}

void LooperEngine::recomputeLeds() {
    for (int i = 0; i < config::kNumTracks; ++i) {
        protocol::LedColor color;
        if (mode_ == GlobalMode::REC_MODE) {
            color = (i == selectedTrack_) ? protocol::kLedRed : protocol::kLedOff;
        } else {
            color = (tracks_[i].state() == TrackState::MUTED) ? protocol::kLedOff : protocol::kLedGreen;
        }
        // A track que esta gravando pisca vermelho, em qualquer modo - o pedal
        // nao dava nenhum retorno visual de que estava gravando.
        const bool blink = (i == capturingTrack_);
        if (blink) {
            color = protocol::kLedRed;
        }
        if (color != desiredColor_[i] || blink != desiredBlink_[i]) {
            desiredColor_[i] = color;
            desiredBlink_[i] = blink;
            ledDirty_[i] = true;
        }
    }
}
