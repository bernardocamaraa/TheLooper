// Painel de uma track na JANELA DE VUs (segundo monitor): nome, estado,
// numero de camadas e o medidor de nivel, em tamanho grande o suficiente para
// ser lido de longe tocando.
//
// Sem nenhum controle - tudo o que se ajusta fica na janela de controles.
#pragma once

#include <functional>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioTrack.h"
#include "VuMeter.h"

class TrackMeterPanel : public juce::Component {
public:
    TrackMeterPanel();

    void setLevelSource(std::function<float()> levelProvider) { vuMeter_.setLevelSource(std::move(levelProvider)); }

    // inputMask e mostrado como texto: na janela de performance, saber qual
    // entrada alimenta cada track e o que explica o que o VU esta medindo.
    // Na track selecionada o medidor mostra a ENTRADA (ver
    // AudioTrack::setMeterInput), e o rotulo aparece destacado.
    void update(const juce::String& name, TrackState state, bool selectedInRecMode, int layerCount,
                uint32_t inputMask);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    bool selectedInRecMode_ = false;

    juce::Label nameLabel_;
    juce::Label stateLabel_;
    juce::Label inputLabel_;
    float scale_ = 0.0f; // ultima escala aplicada as fontes (ver resized)
    VuMeter vuMeter_;
};
