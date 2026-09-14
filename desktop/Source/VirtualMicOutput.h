// Segunda saida de audio (WASAPI, independente do device ASIO principal),
// alimentando um cabo de audio virtual (ex: VB-CABLE - instalado
// separadamente pelo usuario, ver docs/BUILD.md) para que o mix dos loops
// apareca como um microfone no Windows, utilizavel em apps de chamada.
//
// Le do ring buffer preenchido pelo AudioEngine (callback ASIO principal);
// os dois dispositivos tem clocks independentes, entao um underrun ocasional
// (ring vazio) e tratado inserindo silencio, nunca bloqueando.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Config.h"
#include "Messages.h"
#include "SpscQueue.h"

class VirtualMicOutput : public juce::AudioIODeviceCallback {
public:
    explicit VirtualMicOutput(SpscQueue<AudioFrame, 8192>& micRing);
    ~VirtualMicOutput() override;

    // Procura um device de saida WASAPI cujo nome contenha
    // config::kVirtualCableDeviceNameContains e abre um stream nele. Retorna
    // false se nao encontrado (recurso opcional - o resto do app continua
    // funcionando normalmente sem o microfone virtual).
    // sampleRate: a taxa REAL do device principal. O cabo virtual tem de abrir
    // na MESMA taxa - o audio e passado por uma fila, amostra a amostra, sem
    // conversao. Aberto a 48 kHz enquanto a interface roda a 44,1, o consumidor
    // drena 8% mais rapido do que o produtor enche e a fila vive em underrun:
    // o som sai picotado e "errado" nessa saida. Passe 0 para usar o padrao do
    // projeto (util so quando o device principal ainda nao abriu).
    bool start(double sampleRate);
    void stop();

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    SpscQueue<AudioFrame, 8192>& micRing_;
    juce::AudioDeviceManager deviceManager_;
    bool running_ = false;
};
