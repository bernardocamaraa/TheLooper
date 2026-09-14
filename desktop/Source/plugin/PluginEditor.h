// Interface da versao VST3.
//
// NAO reaproveita a MainComponent do standalone de proposito: aquela classe e
// dona do AudioDeviceManager, do microfone virtual, da Settings em disco e das
// janelas de apresentacao - nada disso existe aqui, e deixa-la bimodal
// espalharia "se for plugin" por 1400 linhas de um arquivo que ja funciona.
//
// O que E reaproveitado sao os COMPONENTES (PedalMap, TrackControlStrip,
// LoopProgressBar, ModeFrame, PedalLookAndFeel), que nunca souberam de onde
// vinha o audio. As duas interfaces continuam com a mesma cara porque
// desenham com as mesmas pecas.
#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Config.h"
#include "LoopProgressBar.h"
#include "ModeFrame.h"
#include "PedalLookAndFeel.h"
#include "PedalMap.h"
#include "PluginProcessor.h"
#include "TrackControlStrip.h"

class PedalLooperEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PedalLooperEditor(PedalLooperProcessor& processor);
    ~PedalLooperEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void setupInputStrips();
    void setupTrackStrips();
    void setupControls();

    enum class Page { Pedal, Mixer };
    void setPage(Page page);
    void applyUiScale(float scale);

    // Um trim por entrada fisica (Voz, Violao) - igual ao standalone.
    struct InputStrip {
        juce::Label name;
        juce::Slider gain;
        juce::TextButton reset;
    };

    // A LookAndFeel tem de sobreviver a todo componente que a usa, entao vem
    // primeiro na ordem de declaracao (e e desinstalada no destrutor).
    PedalLookAndFeel lookAndFeel_;

    PedalLooperProcessor& processor_;

    Page page_ = Page::Pedal;
    ui::FrameMood mood_ = ui::FrameMood::Stopped;
    float uiScale_ = 0.0f;

    LoopProgressBar progressBar_;
    PedalMap pedalMap_;

    juce::TextButton pageToggle_;

    // Roteamento: as duas escolhas que so existem dentro do host.
    juce::Label routingCaption_;
    juce::TextButton mixOnMainToggle_;
    juce::TextButton mainBusToggle_;

    juce::Label latencyCaption_;
    juce::Slider latencyTrimSlider_;
    juce::TextButton monitorToggle_;

    juce::Label pedalCaption_;
    juce::TextButton pedalToggle_;
    juce::TextEditor comPortEditor_;
    juce::Label statusLabel_;

    juce::Rectangle<int> pedalRule_;
    juce::Rectangle<int> routingRule_;
    juce::Rectangle<int> inputsRule_;
    juce::Rectangle<int> channelsRule_;

    std::array<std::unique_ptr<InputStrip>, config::kNumChannels> inputStrips_;
    std::array<std::unique_ptr<TrackControlStrip>, config::kNumTracks> trackStrips_;

    juce::TooltipWindow tooltipWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalLooperEditor)
};
