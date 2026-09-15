#include "Brand.h"

#include "BinaryData.h"
#include "PedalLookAndFeel.h"

namespace brand {

namespace {
juce::Image load(const void* data, int size) {
    return juce::ImageCache::getFromMemory(data, size);
}
} // namespace

const juce::Image& mark() {
    static const juce::Image image = load(BinaryData::logomark_png, BinaryData::logomark_pngSize);
    return image;
}

const juce::Image& wordmark() {
    static const juce::Image image = load(BinaryData::logowordmark_png, BinaryData::logowordmark_pngSize);
    return image;
}

const juce::Image& full() {
    static const juce::Image image = load(BinaryData::logofull_png, BinaryData::logofull_pngSize);
    return image;
}

void draw(juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> area, juce::Colour colour) {
    if (!image.isValid()) {
        return;
    }
    g.setColour(colour);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(image, area, juce::RectanglePlacement::centred, true);
}

juce::Image appIcon() {
    return load(BinaryData::appicon_png, BinaryData::appicon_pngSize);
}

juce::Image splashImage(int width, int height, const juce::String& status) {
    const auto& p = theme::palette();
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

    // Opaca de ponta a ponta: a janela de abertura nao tem transparencia.
    g.fillAll(p.bg);
    g.setColour(p.line2);
    g.drawRect(bounds, 1.0f);

    auto area = bounds.reduced(static_cast<float>(width) * 0.12f, static_cast<float>(height) * 0.1f);
    auto footer = area.removeFromBottom(static_cast<float>(height) * 0.12f);
    draw(g, full(), area, p.t1);

    g.setColour(p.t3);
    g.setFont(theme::text(static_cast<float>(height) * 0.038f));
    g.drawText(status, footer, juce::Justification::centred, false);
    return image;
}

} // namespace brand
