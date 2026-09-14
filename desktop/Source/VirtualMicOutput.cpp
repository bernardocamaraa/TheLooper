#include "VirtualMicOutput.h"

#include "RtThread.h"

VirtualMicOutput::VirtualMicOutput(SpscQueue<AudioFrame, 8192>& micRing) : micRing_(micRing) {
}

VirtualMicOutput::~VirtualMicOutput() {
    stop();
}

bool VirtualMicOutput::start(double sampleRate) {
    // "Windows Audio" e o nome interno que o JUCE usa para o backend WASAPI
    // (modo compartilhado) - o ASIO fica reservado para a interface real,
    // gerenciada separadamente pelo AudioEngine.
    deviceManager_.setCurrentAudioDeviceType("Windows Audio", true);
    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (type == nullptr) {
        return false;
    }
    type->scanForDevices();

    const juce::StringArray outputs = type->getDeviceNames(false); // false = saidas
    juce::String target;
    for (const auto& name : outputs) {
        if (name.containsIgnoreCase(config::kVirtualCableDeviceNameContains)) {
            target = name;
            break;
        }
    }
    if (target.isEmpty()) {
        return false; // cabo de audio virtual nao instalado/nao encontrado
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = target;
    setup.inputDeviceName = {};
    setup.sampleRate = sampleRate > 0.0 ? sampleRate : config::kPreferredSampleRate;
    setup.bufferSize = 0; // usa o padrao do device
    setup.useDefaultOutputChannels = true;

    const juce::String error = deviceManager_.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        return false;
    }

    deviceManager_.addAudioCallback(this);
    running_ = true;
    return true;
}

void VirtualMicOutput::stop() {
    if (!running_) {
        return;
    }
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
    running_ = false;
}

void VirtualMicOutput::audioDeviceAboutToStart(juce::AudioIODevice* /*device*/) {
}

void VirtualMicOutput::audioDeviceStopped() {
}

void VirtualMicOutput::audioDeviceIOCallbackWithContext(const float* const* /*inputChannelData*/,
                                                          int /*numInputChannels*/,
                                                          float* const* outputChannelData, int numOutputChannels,
                                                          int numSamples,
                                                          const juce::AudioIODeviceCallbackContext& /*context*/) {
    rt::joinProAudio(); // ver RtThread.h

    for (int i = 0; i < numSamples; ++i) {
        AudioFrame frame{};
        if (!micRing_.pop(frame)) {
            frame.fill(0.0f); // underrun (clocks independentes) - silencio em vez de travar
        }
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] != nullptr) {
                outputChannelData[ch][i] = (ch < config::kNumChannels) ? frame[static_cast<size_t>(ch)] : 0.0f;
            }
        }
    }
}
