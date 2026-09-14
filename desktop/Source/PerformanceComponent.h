// Conteudo da JANELA DE VUs (a que vai no segundo monitor, virada para quem
// esta tocando): moldura de modo, barra de progresso do loop e os 4
// medidores. Nenhum controle - so leitura.
//
// Nao tem Timer proprio: quem chama refresh() e o timer da MainComponent, de
// modo que as duas janelas mostram sempre o mesmo instante do estado.
#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudienceView.h"
#include "Config.h"
#include "LooperEngine.h"
#include "ModeFrame.h"
#include "TrackMeterPanel.h"

class PerformanceComponent : public juce::Component {
public:
    PerformanceComponent(LooperEngine& engine, std::function<juce::String(int)> nameProvider);

    void refresh();

    // A MESMA janela serve as duas leituras: os medidores, para quem esta
    // tocando, e a tela de plateia, para quem esta olhando. Com dois monitores
    // da para alternar entre elas; com tres, a de plateia ganha uma janela so
    // dela (ver MainComponent).
    void setAudienceView(bool audience);
    bool audienceView() const { return audienceView_; }

    void resized() override;
    void paint(juce::Graphics& g) override;
    // O playhead cruza POR CIMA dos painies, entao tem de ser pintado depois
    // dos filhos - ver o comentario em PerformanceComponent.cpp.
    void paintOverChildren(juce::Graphics& g) override;

private:
    LooperEngine& engine_;
    std::function<juce::String(int)> nameProvider_;

    bool audienceView_ = false;
    std::unique_ptr<AudienceView> audience_;

    ui::FrameMood mood_ = ui::FrameMood::Stopped;
    double progress_ = 0.0;
    bool loopDefined_ = false;
    juce::Rectangle<int> meterRow_; // faixa que o playhead percorre
    std::array<std::unique_ptr<TrackMeterPanel>, config::kNumTracks> meters_;
};

// Janela que hospeda o componente acima. Fechar no X apenas esconde (o app so
// termina pela janela de controles), e a posicao/tamanho e salva pela
// MainComponent para a divisao entre monitores nao se perder.
class PerformanceWindow : public juce::DocumentWindow {
public:
    PerformanceWindow(const juce::String& name, juce::Component* content);

    // Ocupa um monitor inteiro. Sem barra de titulo, e por aqui que a janela
    // muda de tela - ver MainComponent::showScreensMenu.
    void showOnDisplay(int displayIndex);

    void closeButtonPressed() override;

    // Os atalhos do pedal valem tambem com esta janela em primeiro plano -
    // durante a apresentacao e ela que esta na frente.
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void()> onHidden;
    std::function<bool(const juce::KeyPress&)> onKeyPressed;
};
