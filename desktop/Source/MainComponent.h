// JANELA PRINCIPAL do The Looper: as seis abas (ver ui/AppShell e ui/Pages).
//
// Esta classe e a dona de toda a cadeia (LooperEngine -> AudioEngine/ASIO,
// SerialLink, VirtualMicOutput, Recorder), da Settings, do setlist e da tela de
// performance (segundo monitor). As abas nao a conhecem: falam com ela pelo
// AppContext, que ela implementa. Ver software/docs/CONTROL_MODEL.md para a
// FSM que a LooperEngine implementa.
#pragma once

#include <array>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "LoopFile.h"
#include "LooperEngine.h"
#include "Messages.h"
#include "PedalLookAndFeel.h"
#include "PerformanceComponent.h"
#include "Recorder.h"
#include "SerialLink.h"
#include "Settings.h"
#include "Setlist.h"
#include "SpscQueue.h"
#include "VirtualMicOutput.h"
#include "ui/AppContext.h"
#include "ui/AppShell.h"

class MainComponent : public juce::Component,
                      public AppContext,
                      private juce::Timer,
                      private juce::ChangeListener {
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Usado pela janela (Main.cpp) para lembrar a posicao entre execucoes.
    void saveControlsWindowBounds(juce::Rectangle<int> bounds);

    // Atalhos: os do pedal (PedalKeys.h), Ctrl+1..6 para as abas e N/P para o
    // setlist. Chamado pelas duas janelas. Devolve true se consumiu a tecla.
    bool handleKey(const juce::KeyPress& key);

    // --- AppContext --------------------------------------------------------
    LooperEngine& engine() override { return looperEngine_; }
    Settings& settings() override { return settings_; }
    void sendPedalEvent(protocol::ButtonId button, protocol::Gesture gesture) override;

    juce::String trackName(int track) override;
    void setTrackName(int track, const juce::String& name) override;
    void setTrackInputMask(int track, uint32_t mask) override;
    void setTrackGain(int track, float gain) override;
    void setInputGain(int channel, float gain) override;
    void setTrackColour(int track, juce::Colour colour) override;
    void confirmClearAll() override;

    juce::File loopsFolder() override;
    juce::String saveSession(const juce::File& file) override;
    juce::String openSession(const juce::File& file) override;
    juce::File stemsFolder() override;

    bool recordingToDisk() override;
    void setRecordingToDisk(bool on) override;
    juce::File recordingFolder() override;
    void chooseRecordingFolder() override;

    bool metersVisible() override;
    void setMetersVisible(bool visible) override;
    int metersDisplay() override;
    void setMetersDisplay(int index) override;
    int controlsDisplay() override;

    juce::AudioDeviceManager& deviceManager() override { return mainDeviceManager_; }
    bool pedalConnected() override;
    void reconnectPedal() override;
    juce::String audioSummary() override;

    void applyAppearance() override;

    SetlistModel& setlist() override { return setlist_; }
    void setlistStep(int direction) override;
    void setlistGoTo(int index) override;
    bool setlistArmed() override;

private:
    void timerCallback() override; // atualiza as janelas (~30 fps)
    // Salva a configuracao de audio sempre que ela muda (driver, device, taxa).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void setupAudioDevice();
    void startVirtualMic();
    void restoreMixer();
    void loadAppearance();
    void loadSetlist();
    void setupPerformanceWindow();
    void showMetersWindow();
    void applySong(int index);

    // Ordem de declaracao = ordem de construcao/destruicao: a LookAndFeel
    // precisa sobreviver a todo componente que a usa, entao vem primeiro.
    PedalLookAndFeel lookAndFeel_;
    Settings settings_;
    LooperEngine looperEngine_;

    SpscQueue<ButtonEventMsg, 64> buttonEventQueue_; // produtor: thread do SerialLink
    SpscQueue<ButtonEventMsg, 64> uiButtonQueue_;    // produtor: thread da GUI
    SpscQueue<LedCommand, 64> ledCommandQueue_;
    SpscQueue<AudioFrame, 8192> micRingQueue_;

    Recorder recorder_; // antes do AudioEngine, que guarda uma referencia
    AudioEngine audioEngine_;
    juce::AudioDeviceManager mainDeviceManager_;
    VirtualMicOutput virtualMic_;
    double virtualMicSampleRate_ = 0.0;
    SerialLink serialLink_;

    std::array<juce::String, config::kNumTracks> trackNames_;
    SetlistModel setlist_;
    juce::uint32 setlistArmedAt_ = 0;

    std::unique_ptr<PerformanceComponent> performanceComponent_;
    std::unique_ptr<PerformanceWindow> performanceWindow_;

    // Por ultimo: as abas apontam para quase tudo acima (inclusive o device
    // manager, pelo seletor de audio embutido), entao morrem primeiro.
    std::unique_ptr<AppShell> shell_;
    juce::TooltipWindow tooltipWindow_;
};
