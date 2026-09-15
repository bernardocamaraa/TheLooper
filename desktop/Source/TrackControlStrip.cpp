#include "TrackControlStrip.h"

#include <cmath>

#include "PedalLookAndFeel.h"
#include "UiText.h"
#include "ui/Widgets.h"

namespace {
// A ComboBox do JUCE reserva o id 0 para "nada selecionado", entao o id de
// cada item e a mascara + 1.
int maskToItemId(uint32_t mask) { return static_cast<int>(mask) + 1; }
uint32_t itemIdToMask(int itemId) { return static_cast<uint32_t>(itemId - 1); }
} // namespace

void TrackControlStrip::ColourDot::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    const float d = juce::jmin(r.getWidth(), r.getHeight()) - 4.0f;
    const auto circle = juce::Rectangle<float>(d, d).withCentre(r.getCentre());
    if (onClick && isMouseOver()) {
        g.setColour(theme::palette().line2);
        g.drawEllipse(circle.expanded(2.5f), 1.5f);
    }
    g.setColour(colour);
    g.fillEllipse(circle);
}

void TrackControlStrip::ColourDot::mouseUp(const juce::MouseEvent& e) {
    if (e.mouseWasClicked() && onClick) {
        onClick();
    }
}

TrackControlStrip::TrackControlStrip() {
    colourDot_.onClick = [this] {
        if (onColourClicked) {
            onColourClicked();
        }
    };
    colourDot_.setTooltip("Trocar a cor da track");
    addAndMakeVisible(colourDot_);

    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    // Renomear e por duplo clique: um clique simples abriria o editor toda vez
    // que se fosse mirar o seletor logo abaixo.
    nameLabel_.setEditable(false, true, false);
    nameLabel_.setTooltip("Duplo clique para renomear");
    nameLabel_.onTextChange = [this] {
        juce::String cleaned = nameLabel_.getText().trim().substring(0, config::kMaxTrackNameLength);
        if (cleaned.isEmpty()) {
            cleaned = currentName_;
        }
        currentName_ = cleaned;
        nameLabel_.setText(cleaned, juce::dontSendNotification);
        if (onNameChanged) {
            onNameChanged(cleaned);
        }
    };
    addAndMakeVisible(nameLabel_);

    inputCaption_.setText("ENTRADA", juce::dontSendNotification);
    inputCaption_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(inputCaption_);

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

    volumeSlider_.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider_.setRange(0.0, 100.0 * config::kMaxTrackGain, 1.0);
    // 100% no meio do curso, como numa mesa de verdade.
    volumeSlider_.setSkewFactorFromMidPoint(100.0);
    volumeSlider_.setValue(100.0 * config::kDefaultTrackGain, juce::dontSendNotification);
    volumeSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 22);
    volumeSlider_.setTextValueSuffix(" %");
    volumeSlider_.setDoubleClickReturnValue(true, 100.0 * config::kDefaultTrackGain);
    volumeSlider_.onValueChange = [this] {
        if (onGainChanged) {
            onGainChanged(static_cast<float>(volumeSlider_.getValue() / 100.0));
        }
    };
    addAndMakeVisible(volumeSlider_);

    resetGainButton_.setButtonText("100%");
    resetGainButton_.setTooltip("Voltar o volume para 100%");
    resetGainButton_.setWantsKeyboardFocus(false);
    resetGainButton_.onClick = [this] {
        volumeSlider_.setValue(100.0 * config::kDefaultTrackGain, juce::sendNotificationSync);
    };
    addAndMakeVisible(resetGainButton_);

    vuMeter_.setStyle(VuMeter::Style::Segments);
    addAndMakeVisible(vuMeter_);

    refreshColours();
}

void TrackControlStrip::setTrackIndex(int index) {
    index_ = index;
    refreshColours();
}

void TrackControlStrip::refreshColours() {
    const auto& p = theme::palette();
    colourDot_.colour = theme::track(index_);
    colourDot_.setMouseCursor(onColourClicked ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    nameLabel_.setColour(juce::Label::textColourId, p.t1);
    inputCaption_.setColour(juce::Label::textColourId, p.t3);
    volumeSlider_.setColour(juce::Slider::trackColourId, theme::track(index_));
    colourDot_.repaint();
    repaint();
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

void TrackControlStrip::setTrackState(TrackState state) {
    vuMeter_.setMuted(state == TrackState::MUTED);
    if (state != state_) {
        state_ = state;
        repaint(stateArea_.getSmallestIntegerContainer().expanded(2));
    }
}

float TrackControlStrip::localScale() const {
    // 246 px de largura (o canal na janela padrao) = escala 1; limitado pela
    // altura para o fader, que absorve o que sobra, nunca ficar espremido.
    const float byWidth = static_cast<float>(getWidth()) / 246.0f;
    const float byHeight = static_cast<float>(getHeight()) / 520.0f;
    return juce::jlimit(0.75f, 1.9f, juce::jmin(byWidth, juce::jmax(0.75f, byHeight * 1.2f)));
}

void TrackControlStrip::resized() {
    const float s = localScale();
    const auto sc = [s](float v) { return juce::roundToInt(v * s); };

    if (std::abs(s - scale_) > 0.01f) {
        scale_ = s;
        nameLabel_.setFont(theme::text(18.0f * s, theme::Weight::Semibold));
        inputCaption_.setFont(theme::caps(10.5f * s));
        volumeSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, sc(72), sc(22));
    }

    auto area = getLocalBounds().reduced(sc(14));
    auto header = area.removeFromTop(sc(28));
    colourDot_.setBounds(header.removeFromLeft(sc(22)));
    header.removeFromLeft(sc(6));
    nameLabel_.setBounds(header);
    area.removeFromTop(sc(12));

    inputCaption_.setBounds(area.removeFromTop(sc(14)));
    area.removeFromTop(sc(4));
    inputSelector_.setBounds(area.removeFromTop(sc(32)));
    area.removeFromTop(sc(14));

    resetGainButton_.setBounds(area.removeFromBottom(sc(32)));
    area.removeFromBottom(sc(8));
    stateArea_ = area.removeFromBottom(sc(22)).toFloat();
    area.removeFromBottom(sc(10));

    // Medidor rente ao fader, como num canal de mesa.
    vuMeter_.setBounds(area.removeFromLeft(sc(16)).withTrimmedBottom(sc(26)));
    area.removeFromLeft(sc(12));
    volumeSlider_.setBounds(area);
}

void TrackControlStrip::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    const float radius = juce::jlimit(10.0f, 20.0f, 14.0f * localScale());
    theme::paintCard(g, bounds, radius);
    if (selected_) {
        theme::paintSelection(g, bounds, radius, p.rec);
    }
    if (!stateArea_.isEmpty()) {
        const float w = stateArea_.getWidth();
        const auto chip = stateArea_.withWidth(w);
        ui::paintStateChip(g, chip.withSizeKeepingCentre(juce::jmin(w, stateArea_.getHeight() * 5.5f),
                                                         stateArea_.getHeight()),
                           state_);
    }
}
