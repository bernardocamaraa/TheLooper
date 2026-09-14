#include "VuMeter.h"

#include <cmath>

#include "PedalLookAndFeel.h"

namespace {
constexpr int kRefreshHz = 30;
constexpr float kFloorDb = -48.0f;     // fundo de escala da regua
constexpr float kPeakHoldMs = 1100.0f; // quanto o pico fica parado antes de cair
constexpr float kPeakFallPerSecond = 1.2f;

constexpr float kSegmentHeight = 7.0f;
constexpr float kSegmentGap = 3.0f;

// Zonas da regua, de baixo para cima. Sao a escala: com elas nao e preciso
// desenhar marcacao de dB, que a distancia so viraria sujeira.
juce::Colour segmentColour(float positionFromBottom) {
    if (positionFromBottom > 0.90f) {
        return theme::ledRed;
    }
    if (positionFromBottom > 0.74f) {
        return theme::amber;
    }
    return theme::ledGreen;
}

float amplitudeToFraction(float amplitude) {
    if (amplitude <= 0.0001f) {
        return 0.0f;
    }
    const float db = 20.0f * std::log10(amplitude);
    return juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
}
} // namespace

VuMeter::VuMeter() {
    startTimerHz(kRefreshHz);
}

VuMeter::~VuMeter() {
    stopTimer();
}

void VuMeter::setMuted(bool muted) {
    if (muted == muted_) {
        return;
    }
    muted_ = muted;
    repaint();
}

void VuMeter::setStyle(Style style) {
    if (style == style_) {
        return;
    }
    style_ = style;
    repaint();
}

void VuMeter::timerCallback() {
    const float amplitude = levelProvider_ ? levelProvider_() : 0.0f;
    level_ = amplitudeToFraction(amplitude);

    // Retencao de pico: sobe na hora, fica parado, depois desce devagar. E o
    // que permite ver um transiente que ja passou.
    constexpr float tickMs = 1000.0f / static_cast<float>(kRefreshHz);
    if (level_ >= peak_) {
        peak_ = level_;
        peakHoldMs_ = kPeakHoldMs;
    } else if (peakHoldMs_ > 0.0f) {
        peakHoldMs_ -= tickMs;
    } else {
        peak_ = juce::jmax(level_, peak_ - kPeakFallPerSecond * tickMs / 1000.0f);
    }

    repaint();
}

void VuMeter::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    theme::paintGroove(g, bounds, 3.0f);

    auto area = bounds.reduced(3.0f);
    if (area.getHeight() < 4.0f || area.getWidth() < 4.0f) {
        return;
    }

    if (style_ == Style::Bar) {
        paintBar(g, area);
    } else {
        paintSegments(g, area);
    }
}

void VuMeter::paintBar(juce::Graphics& g, juce::Rectangle<float> area) {
    const float radius = 2.0f;
    const juce::Colour colour = muted_ ? theme::inkDim : theme::ledGreen;

    // Leito apagado bem discreto. A area aqui e grande (medidor de largura
    // cheia), e o mesmo alpha que funciona na coluna estreita do mixer vira
    // um retangulo esverdeado grande demais parado. O que impede o buraco
    // preto de antes e principalmente o rebaixo (paintGroove); o leito so
    // sugere que ali e uma coluna de nivel.
    g.setColour(colour.withAlpha(0.09f));
    g.fillRoundedRectangle(area, radius);

    if (level_ > 0.0f) {
        auto filled = area.withTop(area.getBottom() - area.getHeight() * level_);
        g.setColour(colour);
        g.fillRoundedRectangle(filled, radius);
    }

    if (peak_ > 0.0f && !muted_) {
        // Retencao de pico: uma risca fina no ponto mais alto recente, na cor
        // da zona em que ele caiu. E o unico aviso de que o sinal esta perto
        // do teto - a barra sozinha nao diz isso.
        const float y = area.getBottom() - area.getHeight() * peak_;
        g.setColour(segmentColour(peak_));
        g.fillRect(area.getX(), juce::jmax(area.getY(), y - 1.0f), area.getWidth(), 2.0f);
    }
}

void VuMeter::paintSegments(juce::Graphics& g, juce::Rectangle<float> area) {
    auto ladder = area;
    if (ladder.getHeight() < kSegmentHeight) {
        return;
    }

    const int segmentCount =
        juce::jmax(1, static_cast<int>((ladder.getHeight() + kSegmentGap) / (kSegmentHeight + kSegmentGap)));
    // Recalcula a altura para a regua encher a area exata, sem sobra no topo.
    const float step = ladder.getHeight() / static_cast<float>(segmentCount);
    const float segHeight = juce::jmax(2.0f, step - kSegmentGap);

    const int litCount = static_cast<int>(std::ceil(level_ * static_cast<float>(segmentCount)));
    const int peakIndex = static_cast<int>(std::ceil(peak_ * static_cast<float>(segmentCount))) - 1;

    for (int i = 0; i < segmentCount; ++i) {
        // i = 0 embaixo.
        const float positionFromBottom =
            (static_cast<float>(i) + 0.5f) / static_cast<float>(segmentCount);
        const float top = ladder.getBottom() - static_cast<float>(i + 1) * step;
        juce::Rectangle<float> seg(ladder.getX(), top, ladder.getWidth(), segHeight);

        const juce::Colour zone = muted_ ? theme::inkFaint : segmentColour(positionFromBottom);
        const bool lit = (i < litCount) && !muted_;
        const bool isPeak = (i == peakIndex) && (peakIndex >= litCount) && !muted_;

        if (lit) {
            g.setColour(zone);
            g.fillRect(seg);
            // Bloom curto: so o suficiente para o segundo aceso parecer luz, e
            // nao tinta.
            g.setColour(zone.withAlpha(0.30f));
            g.fillRect(seg.expanded(1.0f, 0.5f));
        } else if (isPeak) {
            g.setColour(zone.withAlpha(0.85f));
            g.fillRect(seg);
        } else {
            // Segmento apagado: continua la, fraco. E o que impede o medidor
            // de virar um buraco preto quando nao ha sinal - e a distancia e
            // o que da a ESCALA, o quanto ainda falta para o topo.
            g.setColour(zone.withAlpha(0.18f));
            g.fillRect(seg);
        }
    }
}
