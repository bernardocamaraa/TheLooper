// DESENHO do pedal na tela - a caixa preta, o anel de loop e os 8
// footswitches, na mesma disposicao do equipamento real.
//
// E um desenho e nao uma fileira de botoes de propósito: o ponto e bater o
// olho e reconhecer o pedal, para achar o switch na tela pelo mesmo lugar em
// que ele esta no chao. Sem logo e sem visor: so o que tem funcao.
//
// Faz tres coisas ao mesmo tempo:
//
// 1. Clicar num switch dispara o mesmo evento do footswitch correspondente -
//    da para usar o looper sem o hardware.
// 2. O switch AFUNDA quando o botao e pressionado, no pedal ou na tela. E a
//    forma de conferir que o toque chegou (fiacao, porta serial, debounce)
//    sem ter que ouvir o resultado.
// 3. O anel vermelho gira com o loop mestre (uma volta = uma volta do loop) e
//    os LEDs mostram o que o app esta MANDANDO para o pedal. Se o desenho e o
//    equipamento discordarem, o problema esta no hardware ou no firmware, nao
//    na FSM.
//
// Todo o desenho e feito num espaco virtual de kArtWidth x kArtHeight e
// escalado para o tamanho disponivel, entao as proporcoes do pedal se mantem
// em qualquer tamanho de janela.
#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Config.h"
#include "ModeFrame.h"
#include "protocol.h"

class PedalMap : public juce::Component {
public:
    // Proporcao do pedal real. Quem posiciona o componente deve usar estes
    // dois valores para calcular a altura - ver PedalMap::resized.
    static constexpr float kArtWidth = 1600.0f;
    static constexpr float kArtHeight = 580.0f;

    PedalMap();

    std::function<void(protocol::ButtonId, protocol::Gesture)> onPedalEvent;

    struct State {
        bool pressed[protocol::kButtonCount] = {};
        protocol::LedColor led[config::kNumTracks] = {};
        bool ledBlink[config::kNumTracks] = {};
        float level[config::kNumTracks] = {};   // VU de cada track
        float loopPosition = 0.0f; // 0..1, ponteiro de leitura do loop mestre
        bool loopDefined = false;  // false enquanto nenhuma gravacao fechou
        bool recording = false;    // alguma track gravando
        // Modo global, que da a COR do anel: vermelho em REC, verde em Mute
        // Mode, cinza parado - as mesmas cores da moldura das janelas.
        ui::FrameMood mood = ui::FrameMood::Stopped;
    };

    void updateState(const State& state);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Converte um retangulo do espaco do desenho para coordenadas do
    // componente.
    juce::Rectangle<float> art(float x, float y, float w, float h) const;
    juce::Rectangle<float> switchArt(int index) const;
    int switchAt(juce::Point<int> position) const;

    void paintLoopRing(juce::Graphics& g) const;
    void paintMeters(juce::Graphics& g) const;
    void paintClearAll(juce::Graphics& g) const;

    float scale_ = 1.0f;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;

    State state_;
    bool blinkPhase_ = false;

    int mouseDownSwitch_ = -1;
    juce::uint32 mouseDownAtMs_ = 0;

    // LIMPAR TUDO: e um botao de SEGURAR, nao de tocar. Apagar as quatro
    // tracks e a unica acao do app que nao tem volta, e num monitor touch,
    // ainda mais num estande de feira, um toque acidental e questao de tempo.
    // Segurar tambem repete a gramatica do proprio pedal, onde limpar tudo e
    // o hold do CLEAR.
    bool clearHolding_ = false;
    juce::uint32 clearHoldStartMs_ = 0;
    juce::uint32 clearedAtMs_ = 0; // instante do ultimo "limpou", para o flash
};
