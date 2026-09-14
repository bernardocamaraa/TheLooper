#include "AudioEngine.h"

#include "RtThread.h"

AudioEngine::AudioEngine(LooperEngine& looperEngine,
                          SpscQueue<ButtonEventMsg, 64>& pedalButtonEvents,
                          SpscQueue<ButtonEventMsg, 64>& uiButtonEvents,
                          SpscQueue<LedCommand, 64>& outgoingLedCommands,
                          SpscQueue<AudioFrame, 8192>& micRing,
                          Recorder& recorder)
    : looperEngine_(looperEngine),
      pedalButtonEvents_(pedalButtonEvents),
      uiButtonEvents_(uiButtonEvents),
      outgoingLedCommands_(outgoingLedCommands),
      micRing_(micRing),
      recorder_(recorder) {
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device == nullptr) {
        return;
    }

    // A sample rate REAL do driver (que nem sempre e a que foi pedida) e a
    // latencia de ida-e-volta sao lidas aqui e entregues a LooperEngine. Antes
    // isto era so um jassert (sem efeito em Release) e a engine trabalhava com
    // a taxa presumida em Config.h, o que desalinhava buffers e loop quando o
    // driver abria noutra taxa.
    const double sampleRate = device->getCurrentSampleRate();

    // Latencia de ida-e-volta: o driver ja inclui o tamanho de buffer nos dois
    // numeros. E o que a LooperEngine usa para compensar a posicao de
    // gravacao dos overdubs.
    const int64_t latencySamples = static_cast<int64_t>(device->getInputLatencyInSamples()) +
                                    static_cast<int64_t>(device->getOutputLatencyInSamples());

    looperEngine_.setAudioDeviceInfo(sampleRate, latencySamples);
    recorder_.prepare(sampleRate, config::kNumChannels);
}

void AudioEngine::audioDeviceStopped() {
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                     float* const* outputChannelData, int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext& /*context*/) {
    // Esta thread e criada pelo driver ASIO dentro do nosso processo, entao ela
    // herda o estrangulamento do processo se nao se registrar - ver RtThread.h.
    rt::joinProAudio();

    // Antes dos botoes: camadas que voltaram do LayerStore ja entram antes de
    // um desfazer que chegue neste mesmo bloco.
    looperEngine_.serviceBlock();

    // Pedal e interface entram pelo mesmo caminho da FSM - o looper nao
    // distingue de onde veio o toque.
    ButtonEventMsg evt;
    while (pedalButtonEvents_.pop(evt)) {
        looperEngine_.handleButtonEvent(evt.buttonId, evt.gesture);
    }
    while (uiButtonEvents_.pop(evt)) {
        looperEngine_.handleButtonEvent(evt.buttonId, evt.gesture);
    }

    for (int i = 0; i < numSamples; ++i) {
        float inputFrame[config::kNumChannels] = {};
        for (int ch = 0; ch < config::kNumChannels && ch < numInputChannels; ++ch) {
            if (inputChannelData[ch] != nullptr) {
                inputFrame[ch] = inputChannelData[ch][i];
            }
        }

        float outputFrame[config::kNumChannels] = {};
        looperEngine_.processFrame(inputFrame, outputFrame);

        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] != nullptr) {
                outputChannelData[ch][i] = (ch < config::kNumChannels) ? outputFrame[ch] : 0.0f;
            }
        }

        AudioFrame frame{};
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            frame[static_cast<size_t>(ch)] = outputFrame[ch];
        }
        // Best-effort: se o ring buffer estiver cheio (VirtualMicOutput
        // atrasado), descarta o frame em vez de bloquear o callback ASIO.
        micRing_.push(frame);
    }

    // Grava exatamente o que sai pelos alto-falantes, ja mixado e limitado.
    // O writeBlock e chamado SEMPRE (mesmo sem gravacao ativa): ele tambem
    // serve de sinal para o Recorder::stop() saber que o callback passou por
    // aqui - ver o comentario la.
    {
        const float* recordChannels[config::kNumChannels] = {};
        bool haveAll = true;
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            if (ch < numOutputChannels && outputChannelData[ch] != nullptr) {
                recordChannels[ch] = outputChannelData[ch];
            } else {
                haveAll = false;
            }
        }
        recorder_.writeBlock(haveAll ? recordChannels : nullptr, haveAll ? numSamples : 0);
    }

    LedCommand led;
    while (looperEngine_.consumeLedUpdate(led)) {
        outgoingLedCommands_.push(led); // best-effort, mesma logica
    }
}
