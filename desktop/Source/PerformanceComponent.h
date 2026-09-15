// TELA DE PERFORMANCE (a janela de medidores, no segundo monitor, virada para
// quem toca). Requisito inegociavel: ela nunca pode deixar de existir.
//
// Em cima: relogio grande do loop, barra de progresso e a musica do setlist.
// Embaixo: uma coluna por track com a barra continua verde - a parte principal
// da tela. O modo aparece so pela cor da moldura (vermelha, verde ou cinza).
// Nenhum controle - so leitura.
//
// Nao tem Timer proprio: quem chama refresh() e o timer da MainComponent, de
// modo que as janelas mostram sempre o mesmo instante do estado.
#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Config.h"
#include "LoopProgressBar.h"
#include "LooperEngine.h"
#include "ModeFrame.h"
#include "TrackMeterPanel.h"

class PerformanceComponent : public juce::Component {
public:
    PerformanceComponent(LooperEngine& engine, std::function<juce::String(int)> nameProvider,
                         std::function<juce::String()> songProvider);

    void refresh();
    // Tema ou cores das tracks mudaram.
    void refreshColours();

    void resized() override;
    void paint(juce::Graphics& g) override;
    // O playhead cruza POR CIMA das colunas, entao e pintado depois dos filhos.
    void paintOverChildren(juce::Graphics& g) override;

private:
    LooperEngine& engine_;
    std::function<juce::String(int)> nameProvider_;
    std::function<juce::String()> songProvider_;

    ui::FrameMood mood_ = ui::FrameMood::Stopped;
    double progress_ = 0.0;
    bool loopDefined_ = false;
    juce::String clock_;
    juce::String length_;
    juce::String song_;

    juce::Rectangle<int> clockArea_;
    juce::Rectangle<int> songArea_;
    juce::Rectangle<int> meterRow_;
    LoopProgressBar progressBar_;
    std::array<std::unique_ptr<TrackMeterPanel>, config::kNumTracks> meters_;
};

// Janela que hospeda o componente acima: sem barra de titulo, tela cheia no
// monitor escolhido. Fechar (ESC) so esconde - quem encerra o app e a janela
// principal, e e na aba Telas que se escolhe o monitor.
class PerformanceWindow : public juce::DocumentWindow {
public:
    PerformanceWindow(const juce::String& name, juce::Component* content);

    void showOnDisplay(int displayIndex);
    void closeButtonPressed() override;

    // Os atalhos do pedal valem tambem com esta janela na frente.
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void()> onHidden;
    std::function<bool(const juce::KeyPress&)> onKeyPressed;
};
