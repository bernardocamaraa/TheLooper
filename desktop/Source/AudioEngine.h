// Callback de audio realtime principal (device ASIO, via JUCE). Drena os
// eventos de botao vindos do Mega, aciona a LooperEngine amostra a amostra,
// escreve a saida mixada, replica o mix para o ring buffer do microfone
// virtual e encaminha as mudancas de LED para o SerialLink.
//
// Regra de realtime-safety: nenhum metodo chamado a partir do callback pode
// alocar memoria ou bloquear (sem locks, sem I/O). As filas SPSC (ver
// SpscQueue.h) sao o unico ponto de contato com as outras threads.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Config.h"
#include "LooperEngine.h"
#include "Messages.h"
#include "Recorder.h"
#include "SpscQueue.h"

class AudioEngine : public juce::AudioIODeviceCallback {
public:
    // Sao DUAS filas de botao, uma por origem (pedal e interface), e nao uma
    // so compartilhada: SpscQueue e estritamente single-producer (ver o
    // comentario no topo de SpscQueue.h), entao a thread do SerialLink e a
    // thread da GUI empurrando na mesma fila seria corrida. Ambas sao
    // drenadas aqui, que continua sendo o unico consumidor.
    AudioEngine(LooperEngine& looperEngine,
                SpscQueue<ButtonEventMsg, 64>& pedalButtonEvents,
                SpscQueue<ButtonEventMsg, 64>& uiButtonEvents,
                SpscQueue<LedCommand, 64>& outgoingLedCommands,
                SpscQueue<AudioFrame, 8192>& micRing,
                Recorder& recorder);

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    LooperEngine& looperEngine_;
    SpscQueue<ButtonEventMsg, 64>& pedalButtonEvents_;
    SpscQueue<ButtonEventMsg, 64>& uiButtonEvents_;
    SpscQueue<LedCommand, 64>& outgoingLedCommands_;
    SpscQueue<AudioFrame, 8192>& micRing_;
    Recorder& recorder_;
};
