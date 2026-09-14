#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace {
// Uma entrada fisica por barramento estereo, somada em mono. O usuario manda o
// mesmo sinal nos dois lados (violao duplicado numa track, mic duplicado
// noutra), entao a MEDIA devolve o proprio sinal - a soma daria +6 dB. Se
// algum dia chegar material realmente estereo, a media e a dobra mono correta.
float monoSum(const float* const* channels, int numChannels, int sample) {
    const int n = juce::jmin(numChannels, config::kNumChannels);
    if (n <= 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (int ch = 0; ch < n; ++ch) {
        if (channels[ch] != nullptr) {
            sum += channels[ch][sample];
        }
    }
    return sum / static_cast<float>(n);
}

} // namespace

juce::AudioProcessor::BusesProperties PedalLooperProcessor::buildBuses() {
    BusesProperties buses = BusesProperties()
                                .withInput("Violao", juce::AudioChannelSet::stereo(), true)
                                .withInput("Voz", juce::AudioChannelSet::stereo(), true)
                                .withOutput("Mix", juce::AudioChannelSet::stereo(), true);

    // Uma saida por track, habilitadas por padrao para que o "Auto map
    // outputs" do wrapper do FL as enxergue sem o usuario ter de ativar nada.
    for (int i = 0; i < config::kNumTracks; ++i) {
        buses = buses.withOutput(juce::String("TRACK ") + juce::String(i + 1),
                                 juce::AudioChannelSet::stereo(), true);
    }
    return buses;
}

PedalLooperProcessor::PedalLooperProcessor()
    : juce::AudioProcessor(buildBuses()),
      serialLink_(pedalButtonQueue_, ledCommandQueue_) {
    for (int i = 0; i < config::kNumTracks; ++i) {
        trackNames_[static_cast<size_t>(i)] = config::kDefaultTrackNames[i];
    }
    // De proposito NAO chama looperEngine_.prepare() aqui: ele aloca ~250 MB
    // de buffers de loop, e o FL instancia o plugin so para varrer a pasta.
    // Fica para o prepareToPlay, que so acontece quando o plugin vai mesmo
    // processar audio.
}

PedalLooperProcessor::~PedalLooperProcessor() {
    serialLink_.stop();
}

void PedalLooperProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    // Uma vez so: prepare() zera as tracks, e ser chamado a cada troca de
    // buffer size do FL apagaria a musica no meio do ensaio.
    if (!prepared_) {
        looperEngine_.prepare();
        prepared_ = true;
    }

    // Latencia = 0: dentro do host, entrada e saida do plugin ja estao
    // alinhadas na linha do tempo. O que continua existindo e a ida-e-volta da
    // interface de audio (o que o musico ouve chega atrasado ao ouvido dele), e
    // ESSA o plugin nao tem como perguntar a ninguem - o driver e do FL. Sobra
    // o ajuste manual em ms, que ja e o parametro mais importante do looper.
    looperEngine_.setAudioDeviceInfo(sampleRate, 0);
}

bool PedalLooperProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // A saida principal precisa existir e ser mono ou estereo.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo()) {
        return false;
    }

    // As demais podem estar desabilitadas (quem nao usa multi-out) ou estereo.
    for (int i = 1; i < layouts.outputBuses.size(); ++i) {
        const auto& set = layouts.outputBuses.getReference(i);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo() &&
            set != juce::AudioChannelSet::mono()) {
            return false;
        }
    }
    for (int i = 0; i < layouts.inputBuses.size(); ++i) {
        const auto& set = layouts.inputBuses.getReference(i);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo() &&
            set != juce::AudioChannelSet::mono()) {
            return false;
        }
    }
    return true;
}

void PedalLooperProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    looperEngine_.serviceBlock(); // camadas infinitas: ver LooperEngine::serviceBlock
    juce::ScopedNoDenormals noDenormals;

    // Pedal e interface entram pelo mesmo caminho da FSM - o looper nao
    // distingue de onde veio o toque (igual ao AudioEngine do standalone).
    ButtonEventMsg evt;
    while (pedalButtonQueue_.pop(evt)) {
        looperEngine_.handleButtonEvent(evt.buttonId, evt.gesture);
    }
    while (uiButtonQueue_.pop(evt)) {
        looperEngine_.handleButtonEvent(evt.buttonId, evt.gesture);
    }

    const int numSamples = buffer.getNumSamples();

    // Os barramentos de entrada e de saida sao VIEWS do MESMO buffer e se
    // sobrepoem (a entrada 1, por exemplo, mora nos mesmos canais da saida 1).
    // Por isso o laco abaixo le as duas entradas da amostra i ANTES de
    // escrever qualquer saida da amostra i - nunca reordene isso.
    const juce::AudioBuffer<float> mainIn = getBusBuffer(buffer, true, 0);
    const juce::AudioBuffer<float> sideIn =
        (getBusCount(true) > 1) ? getBusBuffer(buffer, true, 1) : juce::AudioBuffer<float>();
    juce::AudioBuffer<float> mainOut = getBusBuffer(buffer, false, 0);

    // Ponteiros resolvidos FORA do laco: isto roda por amostra, e um
    // getWritePointer por canal por amostra em 5 barramentos nao e de graca.
    const int mainInChannels = mainIn.getNumChannels();
    const int sideInChannels = sideIn.getNumChannels();
    const int mainOutChannels = mainOut.getNumChannels();

    const float* mainInPtr[config::kNumChannels] = {};
    const float* sideInPtr[config::kNumChannels] = {};
    float* mainOutPtr[config::kNumChannels] = {};
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        mainInPtr[ch] = (ch < mainInChannels) ? mainIn.getReadPointer(ch) : nullptr;
        sideInPtr[ch] = (ch < sideInChannels) ? sideIn.getReadPointer(ch) : nullptr;
        mainOutPtr[ch] = (ch < mainOutChannels) ? mainOut.getWritePointer(ch) : nullptr;
    }

    float* trackOutPtr[config::kNumTracks][config::kNumChannels] = {};
    for (int t = 0; t < config::kNumTracks; ++t) {
        const int bus = t + 1;
        if (bus >= getBusCount(false)) {
            continue;
        }
        juce::AudioBuffer<float> out = getBusBuffer(buffer, false, bus);
        for (int ch = 0; ch < out.getNumChannels(); ++ch) {
            if (ch < config::kNumChannels) {
                trackOutPtr[t][ch] = out.getWritePointer(ch);
            } else {
                // Canal que o looper nao escreve ficaria com lixo do bloco
                // anterior.
                out.clear(ch, 0, numSamples);
            }
        }
    }

    const bool guitarOnMain = mainBusIsGuitar_.load(std::memory_order_relaxed);
    const bool wantMixOnMain = mixOnMain_.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i) {
        const float onMain = monoSum(mainInPtr, mainInChannels, i);
        const float onSide = monoSum(sideInPtr, sideInChannels, i);

        // config::kInputChannelNames e a fonte da verdade: canal 0 = Voz,
        // canal 1 = Violao.
        float inputFrame[config::kNumChannels] = {};
        inputFrame[0] = guitarOnMain ? onSide : onMain; // Voz
        inputFrame[1] = guitarOnMain ? onMain : onSide; // Violao

        float mixFrame[config::kNumChannels] = {};
        float perTrack[config::kNumTracks * config::kNumChannels] = {};
        looperEngine_.processFrame(inputFrame, mixFrame, perTrack);

        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            if (mainOutPtr[ch] != nullptr) {
                mainOutPtr[ch][i] = wantMixOnMain ? mixFrame[ch] : 0.0f;
            }
        }

        for (int t = 0; t < config::kNumTracks; ++t) {
            const float* src = perTrack + t * config::kNumChannels;
            for (int ch = 0; ch < config::kNumChannels; ++ch) {
                if (trackOutPtr[t][ch] != nullptr) {
                    trackOutPtr[t][ch][i] = src[ch];
                }
            }
        }
    }

    // Idem para a saida principal: o host manda no layout, e o que o looper
    // nao escreve tem de ser silenciado, nao herdado.
    for (int ch = config::kNumChannels; ch < mainOutChannels; ++ch) {
        mainOut.clear(ch, 0, numSamples);
    }

    LedCommand led;
    while (looperEngine_.consumeLedUpdate(led)) {
        ledCommandQueue_.push(led); // best-effort: sem pedal ligado ninguem drena
    }
}

void PedalLooperProcessor::sendPedalEvent(protocol::ButtonId button, protocol::Gesture gesture) {
    uiButtonQueue_.push(ButtonEventMsg{button, gesture});
}

void PedalLooperProcessor::setPedalEnabled(bool enabled) {
    if (enabled == pedalEnabled_) {
        return;
    }
    pedalEnabled_ = enabled;
    if (enabled) {
        serialLink_.start(comPort_);
    } else {
        serialLink_.stop();
    }
}

void PedalLooperProcessor::setComPortOverride(const juce::String& port) {
    if (port == comPort_) {
        return;
    }
    comPort_ = port;
    // A porta e lida uma vez no start() - trocar exige reciclar a thread.
    if (pedalEnabled_) {
        serialLink_.stop();
        serialLink_.start(comPort_);
    }
}

juce::AudioProcessorEditor* PedalLooperProcessor::createEditor() {
    return new PedalLooperEditor(*this);
}

// --- Estado salvo COM O PROJETO DO FL ---
//
// O standalone guarda isto num arquivo em %APPDATA% (ver Settings); aqui vai
// tudo dentro do .flp, que e o comportamento que se espera de um plugin: cada
// projeto com a sua mesa.
void PedalLooperProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree state("PedalLooperPlugin");
    state.setProperty("latencyTrimMs", looperEngine_.latencyTrimMs(), nullptr);
    state.setProperty("softwareMonitoring", looperEngine_.softwareMonitoring(), nullptr);
    state.setProperty("mixOnMain", mixOnMainOutput(), nullptr);
    state.setProperty("mainBusIsGuitar", mainBusIsGuitar(), nullptr);
    state.setProperty("pedalEnabled", pedalEnabled_, nullptr);
    state.setProperty("comPort", comPort_, nullptr);

    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        state.setProperty("inputGain" + juce::String(ch), looperEngine_.inputGain(ch), nullptr);
    }
    for (int t = 0; t < config::kNumTracks; ++t) {
        const juce::String n(t);
        state.setProperty("trackName" + n, trackNames_[static_cast<size_t>(t)], nullptr);
        state.setProperty("trackGain" + n, looperEngine_.trackGain(t), nullptr);
        state.setProperty("trackMask" + n, static_cast<int>(looperEngine_.trackInputMask(t)), nullptr);
    }

    juce::MemoryOutputStream out(destData, false);
    state.writeToStream(out);
}

void PedalLooperProcessor::setStateInformation(const void* data, int sizeInBytes) {
    juce::MemoryInputStream in(data, static_cast<size_t>(sizeInBytes), false);
    const juce::ValueTree state = juce::ValueTree::readFromStream(in);
    if (!state.hasType("PedalLooperPlugin")) {
        return;
    }

    looperEngine_.setLatencyTrimMs(state.getProperty("latencyTrimMs", 0.0));
    looperEngine_.setSoftwareMonitoring(state.getProperty("softwareMonitoring", false));
    setMixOnMainOutput(state.getProperty("mixOnMain", true));
    setMainBusIsGuitar(state.getProperty("mainBusIsGuitar", true));

    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        looperEngine_.setInputGain(ch, state.getProperty("inputGain" + juce::String(ch), 1.0f));
    }
    for (int t = 0; t < config::kNumTracks; ++t) {
        const juce::String n(t);
        trackNames_[static_cast<size_t>(t)] =
            state.getProperty("trackName" + n, config::kDefaultTrackNames[t]).toString();
        looperEngine_.setTrackGain(t, state.getProperty("trackGain" + n, 1.0f));
        looperEngine_.setTrackInputMask(
            t, static_cast<uint32_t>(static_cast<int>(
                   state.getProperty("trackMask" + n, static_cast<int>(config::kAllInputsMask)))));
    }

    // A porta antes do enable: setPedalEnabled(true) ja abre com ela.
    comPort_ = state.getProperty("comPort", "").toString();
    setPedalEnabled(state.getProperty("pedalEnabled", false));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PedalLooperProcessor();
}
