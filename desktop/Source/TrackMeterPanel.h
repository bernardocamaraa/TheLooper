// Coluna de uma track na TELA DE PERFORMANCE (segundo monitor): nome com o
// ponto de cor, estado, camadas e o medidor grande - a barra continua verde,
// que e a parte principal da tela. Nenhum controle.
#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioTrack.h"
#include "VuMeter.h"

class TrackMeterPanel : public juce::Component {
public:
    TrackMeterPanel();

    void setTrackIndex(int index);
    void setLevelSource(std::function<float()> levelProvider) { vuMeter_.setLevelSource(std::move(levelProvider)); }

    // Na track selecionada (REC) o painel ganha o contorno vermelho e o rotulo
    // da entrada aparece destacado (o medidor mostra o que toca e a entrada).
    void update(const juce::String& name, TrackState state, bool selectedInRecMode, int layerCount,
                uint32_t inputMask);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    float localScale() const;
    juce::Rectangle<float> headerArea() const;

    int index_ = 0;
    juce::String name_;
    TrackState state_ = TrackState::EMPTY;
    bool selected_ = false;
    int layers_ = 0;
    uint32_t inputMask_ = 0;
    VuMeter vuMeter_;
};
