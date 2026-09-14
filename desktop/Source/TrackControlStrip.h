// Canal de mesa de uma track, na JANELA DE CONTROLES: nome editavel, seletor
// de entrada, fader de volume e botao de voltar o fader ao ganho unitario.
//
// Nao tem VU meter - os medidores ficam todos na outra janela
// (PerformanceComponent), para poder ser jogada num segundo monitor.
//
// O strip nao guarda estado: so dispara os callbacks. Quem e dono dos valores
// e a LooperEngine (audio) e a Settings (disco).
#pragma once

#include <cmath>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioTrack.h"
#include "Config.h"
#include "VuMeter.h"

class TrackControlStrip : public juce::Component {
public:
    TrackControlStrip();

    // Valores iniciais (vindos da Settings), sem disparar os callbacks.
    void setValues(const juce::String& name, uint32_t inputMask, float gain);

    // O canal tem VU proprio: dosar o fader olhando para a outra janela (ou
    // para o outro monitor) nao funciona.
    void setLevelSource(std::function<float()> levelProvider) { vuMeter_.setLevelSource(std::move(levelProvider)); }

    // Moldura vermelha da track selecionada e estado da track (o VU fica
    // cinza quando ela esta mutada).
    void setSelected(bool selected);
    void setTrackState(TrackState state);

    std::function<void(juce::String)> onNameChanged;
    std::function<void(uint32_t)> onInputMaskChanged;
    std::function<void(float)> onGainChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    bool selected_ = false;
    float scale_ = 0.0f; // ultima escala aplicada as fontes (ver resized)
    juce::String currentName_; // ultimo nome valido, para reverter se apagarem tudo

    juce::Label nameLabel_;
    juce::Label inputCaption_;
    juce::ComboBox inputSelector_;
    juce::Label volumeCaption_;
    juce::Slider volumeSlider_;
    juce::TextButton resetGainButton_;
    VuMeter vuMeter_;
};
