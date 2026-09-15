// VISTA DO PEDAL: os 8 footswitches na MESMA POSICAO do equipamento (fileira
// embaixo, mesma ordem e proporcao), com o visual do app - e o que tem de ser
// fiel. Em cima deles, o "visor" mostra o mesmo conteudo da tela de
// performance, arrumado nas colunas dos botoes: anel, relogio e progresso em
// cima de PLAY+REC..MODE, e cada medidor em cima do botao da sua track.
//
// Faz tres coisas ao mesmo tempo:
// 1. Clicar num switch dispara o mesmo evento do footswitch - da para usar o
//    looper sem o hardware. O CLEAR decide no soltar, como o firmware: toque
//    desfaz, segurar 3 s limpa tudo.
// 2. O switch acende quando o botao e pressionado, no pedal ou na tela.
// 3. Os LEDs mostram o que o app esta MANDANDO para o pedal.
//
// Tudo e desenhado num espaco virtual de kArtWidth x kArtHeight, escalado para
// o tamanho disponivel sem distorcer.
#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioTrack.h"
#include "Config.h"
#include "ModeFrame.h"
#include "protocol.h"

class PedalMap : public juce::Component {
public:
    static constexpr float kArtWidth = 1600.0f;
    static constexpr float kArtHeight = 756.0f;

    PedalMap();

    std::function<void(protocol::ButtonId, protocol::Gesture)> onPedalEvent;

    struct State {
        bool pressed[protocol::kButtonCount] = {};
        protocol::LedColor led[config::kNumTracks] = {};
        bool ledBlink[config::kNumTracks] = {};
        float level[config::kNumTracks] = {};
        float loopPosition = 0.0f; // 0..1
        bool loopDefined = false;
        bool recording = false;
        ui::FrameMood mood = ui::FrameMood::Stopped;

        // Visor (quem nao preencher fica com o padrao - o plugin preenche o
        // que tem).
        TrackState trackState[config::kNumTracks] = {};
        int layers[config::kNumTracks] = {};
        int selectedTrack = -1;
        double loopSeconds = 0.0;
        double positionSeconds = 0.0;
        juce::String names[config::kNumTracks];
        juce::String songText;
    };

    void updateState(const State& state);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Rectangle<float> art(float x, float y, float w, float h) const;
    juce::Rectangle<float> switchArt(int index) const;
    int switchAt(juce::Point<int> position) const;
    bool sameStructure(const State& a, const State& b) const;

    void paintVisor(juce::Graphics& g) const;
    void paintLeds(juce::Graphics& g) const;
    void paintSwitches(juce::Graphics& g) const;

    float scale_ = 1.0f;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;

    State state_;
    bool blinkPhase_ = false;

    int mouseDownSwitch_ = -1;
    juce::uint32 mouseDownAtMs_ = 0;
};
