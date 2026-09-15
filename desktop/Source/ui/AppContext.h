// O que as telas (paginas) precisam de quem e dono do motor.
//
// As paginas nao conhecem a MainComponent: falam com esta interface. Assim a
// janela principal continua sendo a unica dona da cadeia de audio, da Settings
// e das janelas de apresentacao, e as paginas so desenham e pedem coisas.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "LooperEngine.h"
#include "Setlist.h"
#include "protocol.h"

class Settings;

class AppContext {
public:
    virtual ~AppContext() = default;

    virtual LooperEngine& engine() = 0;
    virtual Settings& settings() = 0;

    // Mesmo caminho dos footswitches (a FSM nao distingue de onde veio).
    virtual void sendPedalEvent(protocol::ButtonId button, protocol::Gesture gesture) = 0;

    // --- Tracks ---
    virtual juce::String trackName(int track) = 0;
    virtual void setTrackName(int track, const juce::String& name) = 0;
    virtual void setTrackInputMask(int track, uint32_t mask) = 0;
    virtual void setTrackGain(int track, float gain) = 0;
    virtual void setInputGain(int channel, float gain) = 0;
    // transparentBlack = volta para a cor da paleta.
    virtual void setTrackColour(int track, juce::Colour colour) = 0;

    // LIMPAR TUDO: sempre com confirmacao (clicar e confirmar, nunca segurar).
    virtual void confirmClearAll() = 0;

    // --- Sessoes (.loop) ---
    virtual juce::File loopsFolder() = 0;
    virtual juce::String saveSession(const juce::File& file) = 0; // vazio = ok
    virtual juce::String openSession(const juce::File& file) = 0;
    virtual juce::File stemsFolder() = 0;

    // --- Gravacao em disco (WAV do que sai) ---
    virtual bool recordingToDisk() = 0;
    virtual void setRecordingToDisk(bool on) = 0;
    virtual juce::File recordingFolder() = 0;
    virtual void chooseRecordingFolder() = 0;

    // --- Tela de performance ---
    virtual bool metersVisible() = 0;
    virtual void setMetersVisible(bool visible) = 0;
    virtual int metersDisplay() = 0;
    virtual void setMetersDisplay(int index) = 0;
    virtual int controlsDisplay() = 0;

    // --- Audio e pedal ---
    virtual juce::AudioDeviceManager& deviceManager() = 0;
    virtual bool pedalConnected() = 0;
    virtual void reconnectPedal() = 0;
    virtual juce::String audioSummary() = 0;

    // --- Aparencia: reaplica tema, tamanho e cores em todas as janelas ---
    virtual void applyAppearance() = 0;

    // --- Setlist ---
    virtual SetlistModel& setlist() = 0;
    // Proxima/anterior: limpa o loop e aplica os nomes da musica. Com loop
    // gravado, o primeiro toque so arma a troca (confirmacao pelo segundo).
    virtual void setlistStep(int direction) = 0;
    virtual void setlistGoTo(int index) = 0;
    virtual bool setlistArmed() = 0;
};
