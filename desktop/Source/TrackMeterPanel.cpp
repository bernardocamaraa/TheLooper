#include "TrackMeterPanel.h"

#include "PedalLookAndFeel.h"
#include "UiText.h"
#include "ui/Widgets.h"

TrackMeterPanel::TrackMeterPanel() {
    vuMeter_.setStyle(VuMeter::Style::Bar);
    addAndMakeVisible(vuMeter_);
}

void TrackMeterPanel::setTrackIndex(int index) {
    index_ = index;
    repaint();
}

void TrackMeterPanel::update(const juce::String& name, TrackState state, bool selectedInRecMode, int layerCount,
                             uint32_t inputMask) {
    vuMeter_.setMuted(state == TrackState::MUTED);
    if (name == name_ && state == state_ && selectedInRecMode == selected_ && layerCount == layers_ &&
        inputMask == inputMask_) {
        return;
    }
    name_ = name;
    state_ = state;
    selected_ = selectedInRecMode;
    layers_ = layerCount;
    inputMask_ = inputMask;
    repaint();
}

// A escala sai da LARGURA da coluna: numa tela cheia de 1920 px cada coluna
// tem ~450 px, e o texto cresce junto para ser lido de longe.
float TrackMeterPanel::localScale() const {
    return juce::jlimit(0.8f, 3.0f, static_cast<float>(getWidth()) / 260.0f);
}

juce::Rectangle<float> TrackMeterPanel::headerArea() const {
    const float s = localScale();
    return getLocalBounds().toFloat().reduced(14.0f * s).removeFromTop(64.0f * s);
}

void TrackMeterPanel::resized() {
    const float s = localScale();
    auto r = getLocalBounds().toFloat().reduced(14.0f * s);
    r.removeFromTop(64.0f * s + 10.0f * s); // cabecalho
    r.removeFromBottom(24.0f * s);          // rodape (entrada)
    vuMeter_.setBounds(r.toNearestInt());
}

void TrackMeterPanel::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    const float s = localScale();
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    const float radius = 14.0f * s;
    theme::paintCard(g, bounds, radius);
    if (selected_) {
        theme::paintSelection(g, bounds, radius, p.rec);
    }

    auto header = headerArea();
    auto nameRow = header.removeFromTop(30.0f * s);
    const float dot = 13.0f * s;
    g.setColour(theme::track(index_));
    g.fillEllipse(nameRow.getX(), nameRow.getCentreY() - dot * 0.5f, dot, dot);
    g.setColour(p.t1);
    g.setFont(theme::text(24.0f * s, theme::Weight::Semibold));
    g.drawFittedText(name_, nameRow.withTrimmedLeft(dot * 1.7f).toNearestInt(), juce::Justification::centredLeft, 1,
                     0.8f);

    header.removeFromTop(8.0f * s);
    auto row = header.removeFromTop(24.0f * s);
    ui::paintStateChip(g, row, state_);
    g.setColour(p.t2);
    g.setFont(theme::text(15.0f * s, theme::Weight::Semibold));
    g.drawText(ui::layersText(layers_), row, juce::Justification::centredRight, false);

    // Rodape: a entrada roteada (destacada na track selecionada).
    auto footer = getLocalBounds().toFloat().reduced(14.0f * s).removeFromBottom(20.0f * s);
    g.setColour(selected_ ? p.rec : p.t3);
    g.setFont(theme::text(14.0f * s, selected_ ? theme::Weight::Semibold : theme::Weight::Regular));
    g.drawText((selected_ ? "ENTRADA: " : "") + ui::inputMaskName(inputMask_), footer,
               juce::Justification::centredLeft, false);
}
