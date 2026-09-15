#include "LoopProgressBar.h"

#include "PedalLookAndFeel.h"

void LoopProgressBar::setProgress(double position, bool loopDefined, ui::FrameMood mood) {
    const double clamped = juce::jlimit(0.0, 1.0, position);
    // So repinta quando algo muda de verdade - este componente e atualizado a
    // 30 Hz.
    if (juce::approximatelyEqual(clamped, position_) && loopDefined == loopDefined_ && mood == mood_) {
        return;
    }
    position_ = clamped;
    loopDefined_ = loopDefined;
    mood_ = mood;
    repaint();
}

void LoopProgressBar::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float radius = bounds.getHeight() * 0.5f;

    theme::paintGroove(g, bounds, radius);

    if (!loopDefined_) {
        return; // estado "primordial": so o trilho vazio
    }

    // Cor do modo, chapada: vermelho gravando, verde tocando, cinza parado.
    auto filled = bounds.withWidth(juce::jmax(bounds.getHeight(), static_cast<float>(bounds.getWidth() * position_)));
    g.setColour(ui::frameColour(mood_));
    g.fillRoundedRectangle(filled, radius);
}
