// Canal de mesa de uma track (aba Mixer, e a mesa do plugin): ponto de cor,
// nome editavel, seletor de entrada, fader na cor da track, medidor e botao de
// voltar o fader a 100%.
//
// O strip nao guarda estado: so dispara os callbacks. Quem e dono dos valores
// e a LooperEngine (audio) e a Settings (disco).
#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioTrack.h"
#include "Config.h"
#include "VuMeter.h"

class TrackControlStrip : public juce::Component {
public:
    TrackControlStrip();

    // Qual track e (da a cor). O plugin e o app chamam logo depois de criar.
    void setTrackIndex(int index);

    // Valores iniciais (vindos da Settings), sem disparar os callbacks.
    void setValues(const juce::String& name, uint32_t inputMask, float gain);

    void setLevelSource(std::function<float()> levelProvider) { vuMeter_.setLevelSource(std::move(levelProvider)); }

    // Contorno vermelho da track selecionada e estado da track (o medidor fica
    // cinza quando ela esta mutada).
    void setSelected(bool selected);
    void setTrackState(TrackState state);

    // O tema ou a cor da track mudou.
    void refreshColours();

    std::function<void(juce::String)> onNameChanged;
    // Troca a edicao no lugar por um clique que chama onClick (a pagina abre
    // um dialogo de nome). Sem isto, o nome segue editavel por duplo clique.
    void useRenameDialog(std::function<void()> onClick);
    void mouseUp(const juce::MouseEvent& e) override;
    std::function<void(uint32_t)> onInputMaskChanged;
    std::function<void(float)> onGainChanged;
    // Clique no ponto de cor (sem callback, o ponto nao e clicavel).
    std::function<void()> onColourClicked;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class ColourDot : public juce::Component, public juce::SettableTooltipClient {
    public:
        juce::Colour colour;
        std::function<void()> onClick;
        void paint(juce::Graphics& g) override;
        void mouseUp(const juce::MouseEvent& e) override;
    };

    float localScale() const;

    int index_ = 0;
    bool selected_ = false;
    TrackState state_ = TrackState::EMPTY;
    float scale_ = 0.0f;
    juce::String currentName_; // ultimo nome valido, para reverter se apagarem tudo
    juce::Rectangle<float> stateArea_;

    ColourDot colourDot_;
    juce::Label nameLabel_;
    std::function<void()> onNameClicked_;
    juce::Label inputCaption_;
    juce::ComboBox inputSelector_;
    juce::Slider volumeSlider_;
    juce::TextButton resetGainButton_;
    VuMeter vuMeter_;
};
