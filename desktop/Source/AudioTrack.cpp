#include "AudioTrack.h"

#include <algorithm>
#include <cmath>

AudioTrack::AudioTrack(int trackIndex) : trackIndex_(trackIndex) {}

void AudioTrack::prepareBuffers() {
    allocatedFrames_ =
        static_cast<int64_t>(config::kMaxLoopSeconds * config::kMaxSupportedSampleRate);
    capacitySamples_ = allocatedFrames_;
    const size_t samplesTotal = static_cast<size_t>(allocatedFrames_) * config::kNumChannels;
    for (auto& slot : slots_) {
        slot.assign(samplesTotal, 0.0f);
    }
    setSampleRate(config::kPreferredSampleRate);
}

void AudioTrack::setSampleRate(double sampleRate) {
    if (sampleRate <= 0.0) {
        return;
    }
    // Se o driver abrir acima de kMaxSupportedSampleRate os buffers nao
    // crescem - o que encolhe e a duracao maxima de loop.
    const int64_t wanted = static_cast<int64_t>(config::kMaxLoopSeconds * sampleRate);
    capacitySamples_ = std::min(wanted, allocatedFrames_);

    // Decay do VU tipo peak-hold, ~300ms. O coeficiente e POR AMOSTRA (esta
    // funcao roda uma vez por frame), entao fica muito proximo de 1.
    levelDecayPerSample_ = std::exp(-1.0f / (0.3f * static_cast<float>(sampleRate)));
}

bool AudioTrack::beginCapture(int64_t masterLoopLengthSamples) {
    if (layerCount_ >= config::kMaxLayersPerTrack) {
        return false; // limite de camadas atingido
    }
    if (activeBuffer_ != nullptr) {
        return false; // ja ha uma captura em andamento (a engine nao deveria chamar aqui)
    }

    float* buf = slots_[static_cast<size_t>(layerCount_)].data();
    captureDefinesMaster_ = (masterLoopLengthSamples <= 0);

    if (!captureDefinesMaster_) {
        // Comprimento ja conhecido: zera o trecho que vai ser usado. Sem
        // isso, lixo de um passe anterior (desfeito ou limpo) volta a tocar
        // nas posicoes que este passe nao chegar a cobrir.
        const int64_t clearFrames = std::min(masterLoopLengthSamples, capacitySamples_);
        std::fill(buf, buf + clearFrames * config::kNumChannels, 0.0f);
    }

    state_ = TrackState::RECORDING;
    activeBuffer_ = buf;
    return true;
}

bool AudioTrack::closeCapture() {
    if (activeBuffer_ == nullptr) {
        return false; // nao havia captura em andamento
    }
    ++layerCount_; // comita a camada que estava em slots_[layerCount_ antigo]
    activeBuffer_ = nullptr;
    captureDefinesMaster_ = false;
    state_ = TrackState::PLAYING;
    return true;
}

bool AudioTrack::cancelCapture() {
    if (activeBuffer_ == nullptr) {
        return false;
    }
    activeBuffer_ = nullptr;
    captureDefinesMaster_ = false;
    state_ = (layerCount_ > 0) ? TrackState::PLAYING : TrackState::EMPTY;
    return true;
}

bool AudioTrack::peelLastLayer() {
    if (layerCount_ == 0) {
        return false;
    }
    --layerCount_;
    if (layerCount_ == 0) {
        state_ = TrackState::EMPTY;
        level_.store(0.0f, std::memory_order_relaxed);
    } else if (state_ == TrackState::EMPTY) {
        state_ = TrackState::PLAYING;
    }
    // O slot removido NAO precisa ser zerado aqui: beginCapture zera o trecho
    // antes de reusa-lo, e mixFrameInto so le camadas < layerCount_.
    return true;
}

void AudioTrack::setMuted(bool muted) {
    if (state_ == TrackState::RECORDING || state_ == TrackState::EMPTY) {
        return; // mutar nao faz sentido gravando nem numa track vazia
    }
    state_ = muted ? TrackState::MUTED : TrackState::PLAYING;
}

void AudioTrack::readMix(float* dest, int64_t frames) const {
    const size_t total = static_cast<size_t>(frames) * config::kNumChannels;
    std::fill(dest, dest + total, 0.0f);

    const int64_t usable = std::min(frames, capacitySamples_);
    const size_t usableTotal = static_cast<size_t>(usable) * config::kNumChannels;
    for (int layer = 0; layer < layerCount_; ++layer) {
        const float* src = slots_[static_cast<size_t>(layer)].data();
        for (size_t i = 0; i < usableTotal; ++i) {
            dest[i] += src[i];
        }
    }
}

void AudioTrack::loadMix(const float* source, int64_t frames, bool muted) {
    clearTrack();

    const int64_t usable = std::min(frames, capacitySamples_);
    if (usable <= 0 || slots_[0].empty()) {
        return;
    }

    // Zera o slot INTEIRO antes de copiar: o que sobra depois do trecho
    // carregado seria audio da sessao anterior, e ele voltaria a tocar se o
    // loop desta musica for mais curto que o da anterior - o mesmo "audio
    // fantasma" que beginCapture evita.
    std::fill(slots_[0].begin(), slots_[0].end(), 0.0f);
    std::copy(source, source + static_cast<size_t>(usable) * config::kNumChannels, slots_[0].begin());

    layerCount_ = 1;
    state_ = muted ? TrackState::MUTED : TrackState::PLAYING;
}

void AudioTrack::clearTrack() {
    layerCount_ = 0;
    activeBuffer_ = nullptr;
    captureDefinesMaster_ = false;
    state_ = TrackState::EMPTY;
    level_.store(0.0f, std::memory_order_relaxed);
}

void AudioTrack::routeInput(const float* in, float* out) const {
    const uint32_t mask = inputMask_.load(std::memory_order_relaxed);

    if (mask == 0) {
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            out[ch] = 0.0f; // nenhuma entrada roteada: grava silencio
        }
        return;
    }

    // QUALQUER selecao de entradas - uma so ou todas - e somada em MONO e vai
    // para os dois canais, como um canal mono de mesa com o pan no centro. Se
    // mantivesse o canal original, uma track so de violao sairia so de um
    // lado.
    //
    // "Todas as entradas" ja foi um caso a parte que preservava o par estereo,
    // e era o unico lugar do app que produzia canais diferentes: uma track
    // "Voz + Violao" saia com a voz num lado e o violao no outro. Numa saida
    // mono (ver config::kMonoOutput) isso nao faz sentido - e ligado num canal
    // so da mesa, um dos dois sumiria.
    float mono = 0.0f;
    int count = 0;
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        if ((mask >> ch) & 1u) {
            mono += in[ch];
            ++count;
        }
    }
    if (count > 1) {
        mono /= static_cast<float>(count);
    }
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        out[ch] = mono;
    }
}

void AudioTrack::meterInputFrame(const float* inputFrame) {
    if (!meterInput_) {
        return;
    }
    float routed[config::kNumChannels];
    routeInput(inputFrame, routed);

    float peak = 0.0f;
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        peak = std::max(peak, std::fabs(routed[ch]));
    }
    updateLevel(peak);
}

void AudioTrack::writeFrame(const float* inputFrame, int64_t position) {
    if (activeBuffer_ == nullptr || state_ != TrackState::RECORDING) {
        return;
    }
    if (position < 0 || position >= capacitySamples_) {
        return; // limite de seguranca (kMaxLoopSeconds)
    }

    // O roteamento e aplicado na GRAVACAO (nao na reproducao): o que entra na
    // camada ja e o sinal roteado. Assim mudar a entrada depois nao altera o
    // que ja foi gravado, que e como uma mesa se comporta.
    float routed[config::kNumChannels];
    routeInput(inputFrame, routed);

    float* dest = &activeBuffer_[static_cast<size_t>(position) * config::kNumChannels];
    float peak = 0.0f;
    if (captureDefinesMaster_) {
        // Passe que define o loop mestre: cada posicao e visitada exatamente
        // uma vez, em ordem - sobrescrever e correto (e dispensa o memset).
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            dest[ch] = routed[ch];
            peak = std::max(peak, std::fabs(routed[ch]));
        }
    } else {
        // Camada normal: o slot ja foi zerado em beginCapture, entao acumular
        // preserva o que foi tocado se o passe der mais de uma volta no loop.
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            dest[ch] += routed[ch];
            peak = std::max(peak, std::fabs(routed[ch]));
        }
    }
    updateLevel(peak);
}

void AudioTrack::mixFrameInto(float* outputFrame, int64_t position, float* soloOut) {
    if (soloOut != nullptr) {
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            soloOut[ch] = 0.0f;
        }
    }

    // A camada EM ANDAMENTO tambem toca, nao so as ja commitadas. Ela vive em
    // slots_[layerCount_], entao basta somar uma camada a mais.
    //
    // Isso e o que faz o overdub ser ouvido ja na volta seguinte do loop, em
    // vez de so aparecer quando o passe e fechado (era o comportamento
    // anterior, e soava como se a gravacao nao estivesse acontecendo).
    // Nao ha realimentacao: o ponteiro de escrita fica latencySamples ATRAS
    // do de leitura, entao ler a posicao atual devolve o que foi gravado na
    // volta anterior - nunca o que esta sendo escrito neste instante.
    //
    // O passe que DEFINE o loop mestre e a excecao: o slot dele nao e zerado
    // (ver beginCapture) e o comprimento ainda nem existe, entao toca-lo
    // devolveria lixo. Alem disso o loop ainda nao deu a primeira volta - nao
    // ha o que repetir.
    const bool playActiveLayer = (activeBuffer_ != nullptr && !captureDefinesMaster_);
    const int layersToMix = layerCount_ + (playActiveLayer ? 1 : 0);

    // Quem alimenta o medidor quando meterInput_ esta ligado e
    // meterInputFrame(); gravando, e writeFrame(). Nos dois casos a
    // reproducao nao pode sobrescrever o nivel.
    const bool meterFromPlayback = !meterInput_ && (state_ != TrackState::RECORDING);

    if (state_ == TrackState::MUTED || layersToMix == 0 || position < 0 ||
        position >= capacitySamples_) {
        if (meterFromPlayback) {
            updateLevel(0.0f); // deixa o VU cair em vez de congelar
        }
        return;
    }

    float trackFrame[config::kNumChannels] = {};
    const size_t offset = static_cast<size_t>(position) * config::kNumChannels;
    for (int layer = 0; layer < layersToMix; ++layer) {
        const float* sample = &slots_[static_cast<size_t>(layer)][offset];
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            trackFrame[ch] += sample[ch];
        }
    }

    // Fader da mesa aplicado na reproducao - o audio gravado fica intacto.
    const float userGain = gain_.load(std::memory_order_relaxed);
    float peak = 0.0f;
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        const float mixed = trackFrame[ch] * userGain * config::kTrackGain;
        outputFrame[ch] += mixed;
        if (soloOut != nullptr) {
            soloOut[ch] = mixed;
        }
        peak = std::max(peak, std::fabs(trackFrame[ch] * userGain));
    }
    if (meterFromPlayback) {
        // VU pos-fader: acompanha o que se ouve.
        updateLevel(peak);
    }
}

void AudioTrack::updateLevel(float peak) {
    const float prev = level_.load(std::memory_order_relaxed);
    const float next = (peak > prev) ? peak : prev * levelDecayPerSample_;
    level_.store(next, std::memory_order_relaxed);
}
