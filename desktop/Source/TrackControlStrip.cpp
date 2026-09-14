#include "TrackControlStrip.h"

#include "ModeFrame.h"
#include "PedalLookAndFeel.h"
#include "UiText.h"

namespace {
// A ComboBox do JUCE reserva o id 0 para "nada selecionado", entao o id de
// cada item e a mascara + 1.
int maskToItemId(uint32_t mask) { return static_cast<int>(mask) + 1; }
uint32_t itemIdToMask(int itemId) { return static_cast<uint32_t>(itemId - 1); }

void styleCaption(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setFont(theme::legend(10.5f, true));
    label.setColour(juce::Label::textColourId, theme::inkFaint);
}
} // namespace

TrackControlStrip::TrackControlStrip() {
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setFont(theme::display(19.0f));
    nameLabel_.setColour(juce::Label::textColourId, theme::ink);
    // Renomear e por duplo clique: clique simples abriria o editor sem querer
    // toda vez que se fosse mirar o seletor logo abaixo.
    nameLabel_.setEditable(false, true, false);
    nameLabel_.setTooltip("Duplo clique para renomear");
    nameLabel_.onTextChange = [this] {
        juce::String cleaned = nameLabel_.getText().trim().substring(0, config::kMaxTrackNameLength);
        if (cleaned.isEmpty()) {
            cleaned = currentName_; // nao deixa a track ficar sem nome nenhum
        }
        currentName_ = cleaned;
        nameLabel_.setText(cleaned, juce::dontSendNotification);
        if (onNameChanged) {
            onNameChanged(cleaned);
        }
    };
    addAndMakeVisible(nameLabel_);

    styleCaption(inputCaption_, "ENTRADA");
    addAndMakeVisible(inputCaption_);

    // Uma opcao por combinacao possivel de entradas, mais "Nenhuma" no fim.
    for (uint32_t mask = 1; mask <= config::kAllInputsMask; ++mask) {
        inputSelector_.addItem(ui::inputMaskName(mask), maskToItemId(mask));
    }
    inputSelector_.addItem(ui::inputMaskName(0), maskToItemId(0));
    inputSelector_.setSelectedId(maskToItemId(config::kDefaultInputMask), juce::dontSendNotification);
    inputSelector_.onChange = [this] {
        if (onInputMaskChanged) {
            onInputMaskChanged(itemIdToMask(inputSelector_.getSelectedId()));
        }
    };
    addAndMakeVisible(inputSelector_);

    styleCaption(volumeCaption_, "VOLUME");
    addAndMakeVisible(volumeCaption_);

    volumeSlider_.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider_.setRange(0.0, 100.0 * config::kMaxTrackGain, 1.0);
    // Ponto unitario (100%) no meio do curso do fader: e o comportamento de
    // uma mesa de verdade, e da resolucao fina perto do ganho neutro.
    volumeSlider_.setSkewFactorFromMidPoint(100.0);
    volumeSlider_.setValue(100.0 * config::kDefaultTrackGain, juce::dontSendNotification);
    volumeSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    volumeSlider_.setTextValueSuffix(" %");
    volumeSlider_.setDoubleClickReturnValue(true, 100.0 * config::kDefaultTrackGain);
    volumeSlider_.onValueChange = [this] {
        if (onGainChanged) {
            onGainChanged(static_cast<float>(volumeSlider_.getValue() / 100.0));
        }
    };
    addAndMakeVisible(volumeSlider_);

    // Voltar ao ganho unitario com um clique - mirar 100% na mao e ruim, ainda
    // mais com o curso logaritmico. Antes este botao se chamava "100%" e ficava
    // colado no mostrador que ja dizia "100 %": dois elementos dizendo a mesma
    // coisa, e nenhum deles deixando claro qual era o botao.
    resetGainButton_.setButtonText(ui::utf8("Padrão"));
    resetGainButton_.setTooltip("Voltar o volume para 100%");
    resetGainButton_.onClick = [this] {
        volumeSlider_.setValue(100.0 * config::kDefaultTrackGain, juce::sendNotificationSync);
    };
    addAndMakeVisible(resetGainButton_);

    // Segmentos aqui: o medidor do canal e estreito, e de perto o segmento
    // ajuda a ler valor em vez de so tendencia.
    vuMeter_.setStyle(VuMeter::Style::Segments);
    addAndMakeVisible(vuMeter_);
}

void TrackControlStrip::setTrackState(TrackState state) {
    vuMeter_.setMuted(state == TrackState::MUTED);
}

void TrackControlStrip::setValues(const juce::String& name, uint32_t inputMask, float gain) {
    currentName_ = name;
    nameLabel_.setText(name, juce::dontSendNotification);
    inputSelector_.setSelectedId(maskToItemId(inputMask), juce::dontSendNotification);
    volumeSlider_.setValue(100.0 * static_cast<double>(gain), juce::dontSendNotification);
}

void TrackControlStrip::setSelected(bool selected) {
    if (selected == selected_) {
        return;
    }
    selected_ = selected;
    repaint();
}

void TrackControlStrip::resized() {
    // O canal escala junto com a janela, como o resto da interface (ver
    // MainComponent::resized): 246 px de largura - o que ele mede na janela
    // padrao - e a escala 1. Antes as medidas eram fixas, entao num monitor
    // touch com a janela maximizada o canal continuava do mesmo tamanho e
    // ficava minusculo do lado do desenho do pedal.
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());

    // Teto pela altura: o fader e o ultimo a ser posicionado, entao e ele que
    // absorve o que sobra. Sem este limite, um canal largo e baixo teria o
    // curso do fader espremido a nada - justo o controle que mais se mexe.
    constexpr float kChromeAtScaleOne = 156.0f; // tudo menos o curso do fader
    const float heightCap = juce::jmax(1.0f, (height * 0.58f) / kChromeAtScaleOne);
    const float scale = juce::jlimit(1.0f, juce::jmin(1.9f, heightCap), width / 246.0f);

    const auto sc = [scale](int value) {
        return juce::roundToInt(static_cast<float>(value) * scale);
    };

    if (std::abs(scale - scale_) > 0.01f) {
        scale_ = scale;
        nameLabel_.setFont(theme::display(19.0f * scale));
        inputCaption_.setFont(theme::legend(10.5f * scale, true));
        volumeCaption_.setFont(theme::legend(10.5f * scale, true));
        // Refazer o estilo recria o Label do valor, que e onde a fonte dele e
        // escolhida (ver PedalLookAndFeel::createSliderTextBox).
        volumeSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, sc(60), sc(18));
    }

    auto area = getLocalBounds().reduced(sc(10));

    nameLabel_.setBounds(area.removeFromTop(sc(24)));
    area.removeFromTop(sc(10));

    inputCaption_.setBounds(area.removeFromTop(sc(13)));
    area.removeFromTop(sc(2));
    inputSelector_.setBounds(area.removeFromTop(sc(26)));
    area.removeFromTop(sc(14));

    volumeCaption_.setBounds(area.removeFromTop(sc(13)));
    area.removeFromTop(sc(4));

    resetGainButton_.setBounds(area.removeFromBottom(sc(22)));
    area.removeFromBottom(sc(8));

    // Medidor rente ao fader, como num canal de mesa: nivel a esquerda,
    // controle a direita - da para dosar o volume olhando so para este painel,
    // sem depender da outra janela.
    vuMeter_.setBounds(area.removeFromLeft(sc(20)));
    area.removeFromLeft(sc(10));
    volumeSlider_.setBounds(area);
}

void TrackControlStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().reduced(2);
    theme::paintPanel(g, bounds, selected_);

    if (selected_) {
        auto r = bounds.toFloat();
        const juce::Colour led = theme::ledRed;
        for (int i = 0; i < 5; ++i) {
            const float t = static_cast<float>(i) / 4.0f;
            g.setColour(led.withAlpha(0.16f * (1.0f - t)));
            g.drawRoundedRectangle(r.reduced(1.0f + t * 5.0f), 5.0f, 1.6f);
        }
        g.setColour(led);
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.4f);
    }
}
