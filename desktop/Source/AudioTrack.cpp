#include "AudioTrack.h"

#include <algorithm>
#include <cmath>

AudioTrack::AudioTrack(int trackIndex) : trackIndex_(trackIndex) {}

AudioTrack::~AudioTrack() {
    // So roda no fechamento do app, com o audio ja parado: pode destruir
    // direto em vez de devolver ao LayerStore.
    delete mix_;
    delete capture_;
    for (int i = 0; i < recentCount_; ++i) {
        delete recent_[static_cast<size_t>(i)];
    }
    for (int i = 0; i < peelCount_; ++i) {
        delete peels_[static_cast<size_t>(i)].buffer;
    }
    for (int i = 0; i < reserveCount_; ++i) {
        delete reserve_[static_cast<size_t>(i)];
    }
}

void AudioTrack::prepareBuffers(LayerStore* store) {
    store_ = store;
    allocatedFrames_ = store->fullFrames();
    capacitySamples_ = allocatedFrames_;
    if (mix_ == nullptr) {
        mix_ = LayerStore::allocate(allocatedFrames_);
    }
    mixDirtyFrames_ = 0;
    setSampleRate(config::kPreferredSampleRate);
}

void AudioTrack::setSampleRate(double sampleRate) {
    if (sampleRate <= 0.0) {
        return;
    }
    // Se o driver abrir acima de kMaxSupportedSampleRate os buffers nao
    // crescem - o que encolhe e a duracao maxima de loop.
    sampleRate_ = sampleRate;
    const int64_t wanted = static_cast<int64_t>(config::kMaxLoopSeconds * sampleRate);
    capacitySamples_ = std::min(wanted, allocatedFrames_);

    // Decay do VU tipo peak-hold, ~300ms. O coeficiente e POR AMOSTRA (esta
    // funcao roda uma vez por frame), entao fica muito proximo de 1.
    levelDecayPerSample_ = std::exp(-1.0f / (0.3f * static_cast<float>(sampleRate)));
}

// ---------------------------------------------------------------------------
// FSM
// ---------------------------------------------------------------------------

bool AudioTrack::beginCapture(int64_t masterLoopLengthSamples) {
    if (capture_ != nullptr || store_ == nullptr || mix_ == nullptr) {
        return false; // ja ha captura em andamento, ou a track nao foi preparada
    }

    // O buffer ja chega ZERADO do LayerStore. Zerar e obrigatorio: sem isso,
    // audio de um passe anterior voltava a tocar nos trechos que a nova
    // captura ainda nao tinha coberto ("audio fantasma").
    captureDefinesMaster_ = (masterLoopLengthSamples <= 0);
    LayerBuffer* buffer = takeLayerBuffer(captureDefinesMaster_);
    if (buffer == nullptr) {
        captureDefinesMaster_ = false;
        return false; // nenhum buffer pronto (so acontece se o LayerStore nao acompanhar)
    }

    capture_ = buffer;
    state_ = TrackState::RECORDING;
    lapsInPass_ = 0;
    lapFrames_ = 0;
    lapPeak_ = 0.0f;
    passFrames_ = 0;
    passPeak_ = 0.0f;
    return true;
}

bool AudioTrack::closeCapture() {
    if (capture_ == nullptr) {
        return false; // nao havia captura em andamento
    }

    // O passe que define o loop sempre vira camada (mesmo em silencio: pode
    // ser uma contagem de proposito, e e ele que da o tamanho do loop).
    if (!captureDefinesMaster_) {
        if (lapsInPass_ > 0 && (lapFrames_ < loopLength_ || lapPeak_ < config::kSilentLayerPeak) &&
            recentCount_ > 0) {
            // Parou no meio de uma volta: a sobra NAO vira camada nova, entra
            // na ultima volta inteira deste passe (o desfazer tira as duas).
            LayerBuffer* tail = recent_[static_cast<size_t>(recentCount_ - 1)];
            while (tail->chain != nullptr) {
                tail = tail->chain;
            }
            tail->chain = capture_;
            capture_ = nullptr;
            captureDefinesMaster_ = false;
            state_ = TrackState::PLAYING;
            publishCount();
            return true;
        }
        const auto minFrames = static_cast<int64_t>(config::kMinLayerSeconds * sampleRate_);
        if (lapsInPass_ == 0 && (passFrames_ < minFrames || passPeak_ < config::kSilentLayerPeak)) {
            // Passe curto demais ou mudo - por exemplo, parar logo depois de
            // fechar a base, que ja abre um overdub sozinha: nao cria camada.
            cancelCapture();
            return true;
        }
    }

    pushRecent(capture_);
    capture_ = nullptr;
    captureDefinesMaster_ = false;
    state_ = TrackState::PLAYING;
    publishCount();
    return true;
}

bool AudioTrack::splitCapture() {
    if (capture_ == nullptr || captureDefinesMaster_ || store_ == nullptr) {
        return false;
    }
    if (lapPeak_ < config::kSilentLayerPeak) {
        // Volta inteira em silencio: nao vira camada. A proxima volta continua
        // no mesmo buffer, que so tem silencio.
        lapFrames_ = 0;
        lapPeak_ = 0.0f;
        return true;
    }
    LayerBuffer* next = takeLayerBuffer(false);
    if (next == nullptr) {
        return false; // sem buffer pronto: a proxima volta continua nesta camada
    }
    pushRecent(capture_);
    capture_ = next;
    ++lapsInPass_;
    lapFrames_ = 0;
    lapPeak_ = 0.0f;
    publishCount();
    return true;
}

bool AudioTrack::cancelCapture() {
    if (capture_ == nullptr) {
        return false;
    }
    LayerBuffer* cancelled = capture_;
    capture_ = nullptr;
    captureDefinesMaster_ = false;

    if (effectiveLayers() <= 0) {
        // A track so tinha este passe: silencio exato, sem subtrair nada.
        store_->release(cancelled);
        clearTrack();
        return true;
    }

    // O passe ja estava na soma (e tocando): tira de la aos poucos.
    startPeel(cancelled);
    state_ = TrackState::PLAYING;
    publishCount();
    return true;
}

bool AudioTrack::peelLastLayer() {
    const int layers = effectiveLayers();
    if (layers <= 0) {
        return false;
    }
    if (layers == 1) {
        // Ultima camada: troca a soma por uma zerada em vez de subtrair. Fica
        // silencio exato, e e o unico jeito de desfazer a camada base de uma
        // sessao aberta de arquivo, que nao tem buffer proprio.
        clearTrack();
        return true;
    }

    if (recentCount_ > 0) {
        --recentCount_;
        LayerBuffer* layer = recent_[static_cast<size_t>(recentCount_)];
        recent_[static_cast<size_t>(recentCount_)] = nullptr;
        startPeel(layer);
    } else {
        // A camada a tirar esta guardada no LayerStore (talvez em disco).
        // Fica pendente: service() pede de volta e onRestored() aplica.
        ++pendingUndos_;
    }
    if (state_ == TrackState::EMPTY) {
        state_ = TrackState::PLAYING;
    }
    publishCount();
    return true;
}

void AudioTrack::setMuted(bool muted) {
    if (state_ == TrackState::RECORDING || state_ == TrackState::EMPTY) {
        return; // mutar nao faz sentido gravando nem numa track vazia
    }
    state_ = muted ? TrackState::MUTED : TrackState::PLAYING;
}

void AudioTrack::clearTrack() {
    if (store_ != nullptr) {
        store_->release(capture_);
        for (int i = 0; i < recentCount_; ++i) {
            store_->release(recent_[static_cast<size_t>(i)]);
        }
        for (int i = 0; i < peelCount_; ++i) {
            store_->release(peels_[static_cast<size_t>(i)].buffer);
        }
        store_->clearDeep(trackIndex_);
    }
    capture_ = nullptr;
    recent_.fill(nullptr);
    recentCount_ = 0;
    peels_.fill(Peel{});
    peelCount_ = 0;
    deepCount_ = 0;
    pendingUndos_ = 0;
    refillOutstanding_ = false;
    refillExpected_ = 0;
    ++generation_;
    baseOnly_ = false;
    captureDefinesMaster_ = false;

    resetMix();

    state_ = TrackState::EMPTY;
    level_.store(0.0f, std::memory_order_relaxed);
    publishCount();
}

// Troca a soma por uma zerada que ja estava pronta; a velha volta para o
// LayerStore zerar. So zera aqui mesmo se nao houver nenhuma pronta.
void AudioTrack::resetMix() {
    if (mix_ == nullptr) {
        return;
    }
    if (mixDirtyFrames_ == 0) {
        return; // nunca recebeu audio: ja esta zerada
    }
    LayerBuffer* fresh = (store_ != nullptr) ? store_->takeFull() : nullptr;
    if (fresh != nullptr) {
        store_->release(mix_);
        mix_ = fresh;
    } else {
        std::fill(mix_->data(), mix_->data() + mixDirtyFrames_ * config::kNumChannels, 0.0f);
    }
    mixDirtyFrames_ = 0;
}

void AudioTrack::pushRecent(LayerBuffer* layer) {
    if (recentCount_ == config::kRecentLayerSlots) {
        // Nao deveria acontecer (service() manda as antigas para o LayerStore
        // bem antes). Se a fila dele estiver cheia, a mais antiga das recentes
        // perde o buffer: o som dela continua na soma e ela so deixa de poder
        // ser desfeita sozinha - vira parte da base.
        LayerBuffer* oldest = recent_[0];
        if (store_ != nullptr && store_->archive(trackIndex_, generation_, oldest)) {
            ++deepCount_;
        } else {
            if (store_ != nullptr) {
                store_->release(oldest);
            }
            baseOnly_ = true;
        }
        std::move(recent_.begin() + 1, recent_.end(), recent_.begin());
        --recentCount_;
    }
    recent_[static_cast<size_t>(recentCount_)] = layer;
    ++recentCount_;
}

void AudioTrack::startPeel(LayerBuffer* layer) {
    // Uma camada pode ter mais de um pedaco (sobra de volta incompleta): cada
    // pedaco vira um desfazer em andamento proprio.
    while (layer != nullptr) {
        LayerBuffer* next = layer->chain;
        layer->chain = nullptr;
        startPeelOne(layer);
        layer = next;
    }
}

void AudioTrack::startPeelOne(LayerBuffer* layer) {
    if (layer == nullptr) {
        return;
    }
    if (peelCount_ == config::kMaxPendingPeels) {
        finishPeel(0); // muitos desfazer seguidos: termina o mais antigo agora
    }
    Peel peel;
    peel.buffer = layer;
    peel.done = 0;
    const int64_t span = (loopLength_ > 0) ? loopLength_ : mixDirtyFrames_;
    peel.frames = std::min({layer->frames, span, capacitySamples_});
    peels_[static_cast<size_t>(peelCount_)] = peel;
    ++peelCount_;
}

void AudioTrack::finishPeel(int index) {
    Peel& peel = peels_[static_cast<size_t>(index)];
    const int64_t total = peel.frames * config::kNumChannels;
    float* mix = mix_->data();
    const float* layer = peel.buffer->data();
    for (int64_t i = peel.done; i < total; ++i) {
        mix[i] -= layer[i];
    }
    store_->release(peel.buffer);
    peels_[static_cast<size_t>(index)] = peels_[static_cast<size_t>(peelCount_ - 1)];
    peels_[static_cast<size_t>(peelCount_ - 1)] = Peel{};
    --peelCount_;
}

LayerBuffer* AudioTrack::takeLayerBuffer(bool full) {
    if (store_ == nullptr) {
        return nullptr;
    }
    if (full || loopLength_ <= 0) {
        return store_->takeFull(); // passe que define o loop: tamanho ainda desconhecido
    }
    for (int i = reserveCount_ - 1; i >= 0; --i) {
        LayerBuffer* buffer = reserve_[static_cast<size_t>(i)];
        if (buffer->frames >= loopLength_) {
            reserve_[static_cast<size_t>(i)] = reserve_[static_cast<size_t>(reserveCount_ - 1)];
            reserve_[static_cast<size_t>(reserveCount_ - 1)] = nullptr;
            --reserveCount_;
            return buffer;
        }
    }
    return store_->takeLoop(loopLength_);
}

void AudioTrack::topUpReserve() {
    // A reserva so existe enquanto a track GRAVA (e quem abre uma volta nova a
    // cada passagem pelo comeco do loop). Parada, devolve: com um loop de 60 s
    // as 4 tracks segurando reserva seriam ~180 MB parados.
    const bool recording = (state_ == TrackState::RECORDING);
    // Descarta tambem os de um loop anterior (ou de quando nao havia loop).
    for (int i = 0; i < reserveCount_;) {
        LayerBuffer* buffer = reserve_[static_cast<size_t>(i)];
        if (!recording || loopLength_ <= 0 || buffer->frames < loopLength_) {
            store_->release(buffer);
            reserve_[static_cast<size_t>(i)] = reserve_[static_cast<size_t>(reserveCount_ - 1)];
            reserve_[static_cast<size_t>(reserveCount_ - 1)] = nullptr;
            --reserveCount_;
        } else {
            ++i;
        }
    }
    while (recording && loopLength_ > 0 && reserveCount_ < config::kTrackReserveBuffers) {
        LayerBuffer* buffer = store_->takeLoopOnly(loopLength_);
        if (buffer == nullptr) {
            break;
        }
        reserve_[static_cast<size_t>(reserveCount_)] = buffer;
        ++reserveCount_;
    }
}

void AudioTrack::publishCount() {
    publishedLayers_.store(std::max(0, effectiveLayers()), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Manutencao por bloco
// ---------------------------------------------------------------------------

void AudioTrack::service() {
    if (store_ == nullptr || mix_ == nullptr) {
        return;
    }

    // 1. Desfazer em andamento: subtrai da soma mais um pedaco de cada um.
    int64_t budget = config::kPeelSamplesPerBlock;
    for (int i = 0; i < peelCount_ && budget > 0;) {
        Peel& peel = peels_[static_cast<size_t>(i)];
        const int64_t total = peel.frames * config::kNumChannels;
        const int64_t count = std::min(total - peel.done, budget);
        float* mix = mix_->data() + peel.done;
        const float* layer = peel.buffer->data() + peel.done;
        for (int64_t k = 0; k < count; ++k) {
            mix[k] -= layer[k];
        }
        peel.done += count;
        budget -= count;
        if (peel.done >= total) {
            store_->release(peel.buffer);
            peels_[static_cast<size_t>(i)] = peels_[static_cast<size_t>(peelCount_ - 1)];
            peels_[static_cast<size_t>(peelCount_ - 1)] = Peel{};
            --peelCount_;
        } else {
            ++i;
        }
    }

    // 2. Recentes demais: a mais antiga vai para o LayerStore.
    while (recentCount_ > config::kRecentLayersKeep) {
        if (!store_->archive(trackIndex_, generation_, recent_[0])) {
            break; // fila cheia: tenta no proximo bloco
        }
        std::move(recent_.begin() + 1, recent_.begin() + recentCount_, recent_.begin());
        --recentCount_;
        recent_[static_cast<size_t>(recentCount_)] = nullptr;
        ++deepCount_;
    }

    // 3. Poucas recentes (ou desfazer esperando): pede as guardadas de volta
    // antes que o desfazer precise delas.
    if (!refillOutstanding_ && deepCount_ > 0 &&
        (recentCount_ < config::kRecentRefillBelow || pendingUndos_ > 0)) {
        const int want = std::min(deepCount_, std::max(pendingUndos_, config::kRecentLayersKeep - recentCount_));
        if (want > 0 && store_->requestRefill(trackIndex_, generation_, want)) {
            refillOutstanding_ = true;
            refillExpected_ = want;
        }
    }

    // 4. Reserva da track cheia para as proximas voltas.
    topUpReserve();
}

void AudioTrack::onRestored(const LayerRestore& restore) {
    if (restore.generation != generation_ || store_ == nullptr) {
        // Pedida antes de uma limpeza: nao pertence mais a esta track.
        if (store_ != nullptr) {
            store_->release(restore.buffer);
        }
        return;
    }

    --deepCount_;
    if (--refillExpected_ <= 0) {
        refillOutstanding_ = false;
        refillExpected_ = 0;
    }

    if (pendingUndos_ > 0) {
        // As camadas voltam da mais nova para a mais antiga, e o desfazer
        // pendente era justamente para as mais novas das guardadas.
        --pendingUndos_;
        startPeel(restore.buffer);
    } else if (recentCount_ < config::kRecentLayerSlots) {
        // Mais antiga que todas as recentes: entra no fundo do array.
        std::move_backward(recent_.begin(), recent_.begin() + recentCount_, recent_.begin() + recentCount_ + 1);
        recent_[0] = restore.buffer;
        ++recentCount_;
    } else if (store_->archive(trackIndex_, generation_, restore.buffer)) {
        ++deepCount_; // sem vaga: volta a ser a mais nova das guardadas
    } else {
        store_->release(restore.buffer);
        baseOnly_ = true; // mesma saida de pushRecent: vira parte da base
    }
    publishCount();
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

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
    // lado. (A saida do app e mono - ver config::kMonoOutput.)
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

void AudioTrack::writeFrame(const float* inputFrame, int64_t position) {
    if (capture_ == nullptr || state_ != TrackState::RECORDING) {
        return;
    }
    if (position < 0 || position >= capacitySamples_ || position >= capture_->frames) {
        return; // limite de seguranca (kMaxLoopSeconds)
    }

    // O roteamento e aplicado na GRAVACAO (nao na reproducao): o que entra na
    // camada ja e o sinal roteado. Assim mudar a entrada depois nao altera o
    // que ja foi gravado, que e como uma mesa se comporta.
    float routed[config::kNumChannels];
    routeInput(inputFrame, routed);

    // Na camada (para o desfazer) e na soma (para tocar). Acumular preserva o
    // que foi tocado se o passe der mais de uma volta no loop; no passe que
    // define o loop cada posicao e visitada uma vez so, sobre buffer zerado.
    const size_t offset = static_cast<size_t>(position) * config::kNumChannels;
    float* layer = capture_->data() + offset;
    float* mix = mix_->data() + offset;
    float peak = 0.0f;
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        layer[ch] += routed[ch];
        mix[ch] += routed[ch];
        peak = std::max(peak, std::fabs(routed[ch]));
    }
    mixDirtyFrames_ = std::max(mixDirtyFrames_, position + 1);
    ++lapFrames_;
    ++passFrames_;
    lapPeak_ = std::max(lapPeak_, peak);
    passPeak_ = std::max(passPeak_, peak);
    framePeak_ = std::max(framePeak_, peak); // mixFrameInto junta com a reproducao
}

void AudioTrack::meterInputFrame(const float* inputFrame) {
    float routed[config::kNumChannels];
    routeInput(inputFrame, routed);
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        framePeak_ = std::max(framePeak_, std::fabs(routed[ch]));
    }
}

void AudioTrack::mixFrameInto(float* outputFrame, int64_t position, float* soloOut, float meterScale) {
    if (soloOut != nullptr) {
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            soloOut[ch] = 0.0f;
        }
    }

    // A soma ja inclui o passe EM ANDAMENTO: e isso que faz o overdub ser
    // ouvido ja na volta seguinte do loop. Nao ha realimentacao: o ponteiro de
    // escrita fica latencySamples ATRAS do de leitura, entao ler a posicao
    // atual devolve o que foi gravado na volta anterior - nunca o que esta
    // sendo escrito neste instante.
    //
    // O passe que DEFINE o loop mestre e a excecao: o loop ainda nao deu a
    // primeira volta (nao ha o que repetir), e ler a mesma posicao que acabou
    // de ser escrita devolveria a propria entrada, como um monitoramento
    // duplicado.
    const bool hasContent =
        (committedLayers() > 0) || (capture_ != nullptr && !captureDefinesMaster_);

    // O nivel do frame e fechado aqui: o pico da entrada (framePeak_, se a
    // track esta gravando ou selecionada) junto com o da reproducao.
    if (!hasContent || mix_ == nullptr || position < 0 || position >= capacitySamples_) {
        updateLevel(framePeak_); // sem reproducao: so a entrada, ou deixa o VU cair
        framePeak_ = 0.0f;
        return;
    }

    const size_t offset = static_cast<size_t>(position) * config::kNumChannels;
    float trackFrame[config::kNumChannels];
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        trackFrame[ch] = mix_->data()[offset + static_cast<size_t>(ch)];
    }
    // Desfazer em andamento: o trecho que service() ainda nao subtraiu da
    // soma e subtraido aqui, na leitura - a camada some do som na hora.
    for (int p = 0; p < peelCount_; ++p) {
        const Peel& peel = peels_[static_cast<size_t>(p)];
        if (position >= peel.frames) {
            continue;
        }
        const float* layer = peel.buffer->data() + offset;
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            if (static_cast<int64_t>(offset) + ch >= peel.done) {
                trackFrame[ch] -= layer[ch];
            }
        }
    }

    // Fader da mesa aplicado na reproducao - o audio gravado fica intacto.
    const float userGain = gain_.load(std::memory_order_relaxed);
    const bool audible = (state_ != TrackState::MUTED);
    float peak = 0.0f;
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        const float mixed = trackFrame[ch] * userGain * config::kTrackGain;
        if (audible) {
            outputFrame[ch] += mixed;
            if (soloOut != nullptr) {
                soloOut[ch] = mixed;
            }
        }
        peak = std::max(peak, std::fabs(trackFrame[ch] * userGain));
    }
    // Reproducao pos-fader, mas PRE-mute: a track mutada continua medida (a
    // interface mostra em cinza) e so para com o transporte (meterScale). A
    // entrada nao depende do transporte: parado, a selecionada ainda mostra o
    // sinal chegando.
    updateLevel(std::max(framePeak_, peak * meterScale));
    framePeak_ = 0.0f;
}

void AudioTrack::updateLevel(float peak) {
    const float prev = level_.load(std::memory_order_relaxed);
    const float next = (peak > prev) ? peak : prev * levelDecayPerSample_;
    level_.store(next, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Sessao em disco (nao-RT, com o callback de audio desligado)
// ---------------------------------------------------------------------------

void AudioTrack::readMix(float* dest, int64_t frames) const {
    const size_t total = static_cast<size_t>(frames) * config::kNumChannels;
    std::fill(dest, dest + total, 0.0f);
    if (mix_ == nullptr) {
        return;
    }

    const int64_t usable = std::min(frames, capacitySamples_);
    const size_t usableTotal = static_cast<size_t>(usable) * config::kNumChannels;
    std::copy(mix_->data(), mix_->data() + usableTotal, dest);

    // O que a track TOCA: sem o resto dos desfazer em andamento e sem o passe
    // ainda aberto (que so conta depois de fechado).
    for (int p = 0; p < peelCount_; ++p) {
        const Peel& peel = peels_[static_cast<size_t>(p)];
        const size_t end = std::min(usableTotal, static_cast<size_t>(peel.frames) * config::kNumChannels);
        for (size_t i = static_cast<size_t>(peel.done); i < end; ++i) {
            dest[i] -= peel.buffer->data()[i];
        }
    }
    if (capture_ != nullptr) {
        const size_t end = std::min(usableTotal, static_cast<size_t>(capture_->frames) * config::kNumChannels);
        for (size_t i = 0; i < end; ++i) {
            dest[i] -= capture_->data()[i];
        }
    }
    // Um desfazer esperando camada voltar do disco (pendingUndos_) ainda esta
    // na soma e vai para o arquivo: salvar no exato instante em que se aperta
    // desfazer numa pilha de centenas de camadas. Aceitavel.
}

void AudioTrack::loadMix(const float* source, int64_t frames, bool muted) {
    clearTrack();
    if (mix_ == nullptr) {
        return;
    }
    const int64_t usable = std::min(frames, capacitySamples_);
    if (usable <= 0) {
        return;
    }
    std::copy(source, source + static_cast<size_t>(usable) * config::kNumChannels, mix_->data());
    mixDirtyFrames_ = usable;
    baseOnly_ = true; // uma camada, sem buffer proprio: desfazer apaga a track
    state_ = muted ? TrackState::MUTED : TrackState::PLAYING;
    publishCount();
}
