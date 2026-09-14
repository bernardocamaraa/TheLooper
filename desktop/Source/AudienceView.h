// TELA DE PLATEIA: a leitura do looper para quem esta OLHANDO, nao para quem
// esta tocando.
//
// A janela de VUs (PerformanceComponent) responde "meu sinal esta bom?". Esta
// aqui responde outra pergunta, a da plateia: "o que ele acabou de fazer?".
// Por isso o desenho central nao e uma regua de medidores e sim UM CIRCULO com
// quatro aneis concentricos - um por track - e um ponteiro que da uma volta a
// cada volta do loop. E a propria ideia de looper desenhada: as quatro tracks
// dividem um unico loop, e a cada volta uma camada nova pode entrar.
//
// A faixa de baixo mostra o PROXIMO PASSO em funcao do estado real do motor.
// Numa feira de ciencias qualquer pessoa chega, le a frase, pisa no botao que
// ela manda pisar e ouve o proprio loop - sem ninguem por perto explicando.
#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Config.h"
#include "LooperEngine.h"
#include "ModeFrame.h"

class AudienceView : public juce::Component {
public:
    AudienceView(LooperEngine& engine, std::function<juce::String(int)> nameProvider);

    // Chamado pelo timer da MainComponent, como o resto das telas - assim as
    // janelas mostram sempre o mesmo instante do estado.
    void refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Snapshot {
        ui::FrameMood mood = ui::FrameMood::Stopped;
        bool recMode = true;
        bool playing = false;
        bool loopDefined = false;
        bool recording = false;
        int selected = 0;
        double progress = 0.0;
        double loopSeconds = 0.0;
        TrackState state[config::kNumTracks] = {};
        int layers[config::kNumTracks] = {};
        float level[config::kNumTracks] = {};
    };

    juce::Colour trackColour(int track) const;
    juce::String hintText() const;

    void paintWheel(juce::Graphics& g) const;
    void paintCards(juce::Graphics& g) const;
    void paintHint(juce::Graphics& g) const;

    LooperEngine& engine_;
    std::function<juce::String(int)> nameProvider_;

    Snapshot state_;
    bool blinkPhase_ = false;
    juce::String hint_;

    juce::Rectangle<int> headerArea_;
    juce::Rectangle<int> wheelArea_;
    juce::Rectangle<int> wheelCircle_; // so o circulo, para repintar o ponteiro sem a tela toda
    juce::Rectangle<int> cardsArea_;
    juce::Rectangle<int> hintArea_;
};
