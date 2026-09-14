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
        return; // estado "primordial": so o poco vazio, sem enfeite
    }

    const juce::Colour colour = ui::frameColour(mood_);
    auto filled = bounds.reduced(1.0f);
    filled.setWidth(juce::jmax(filled.getHeight(),
                                static_cast<float>(filled.getWidth() * position_)));

    g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.35f), bounds.getX(), 0.0f, colour,
                                            filled.getRight(), 0.0f, false));
    g.fillRoundedRectangle(filled, radius);

    // Ponta viva: marca onde o loop esta agora, nao so quanto ja andou.
    g.setColour(colour.brighter(0.3f));
    g.fillRoundedRectangle(filled.removeFromRight(2.0f), 1.0f);
}
