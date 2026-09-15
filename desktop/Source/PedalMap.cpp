#include "PedalMap.h"

#include <cmath>

#include "PedalLookAndFeel.h"
#include "UiText.h"
#include "ui/Widgets.h"

namespace {
constexpr int kClearHoldMs = 3000; // igual ao config::kClearAllHoldMs do firmware

// --- Geometria, no espaco do desenho (1600 x 756) ---------------------------
// Os switches ocupam a fileira de baixo inteira, 8 do mesmo tamanho, na ordem
// do pedal. O visor tem a largura da fileira e usa o MESMO passo de colunas,
// entao cada medidor fica exatamente em cima do botao da sua track.
constexpr float kSwitchLeft = 26.0f;
constexpr float kSwitchRight = 1574.0f;
constexpr float kSwitchTop = 476.0f;
constexpr float kSwitchHeight = 254.0f;
constexpr float kSwitchGap = 14.0f;
constexpr float kSwitchWidth =
    ((kSwitchRight - kSwitchLeft) - kSwitchGap * (protocol::kButtonCount - 1)) / protocol::kButtonCount;

constexpr float kLedBarY = 446.0f;
constexpr float kLedBarH = 12.0f;

constexpr float kVisorX = kSwitchLeft;
constexpr float kVisorY = 26.0f;
constexpr float kVisorW = kSwitchRight - kSwitchLeft;
constexpr float kVisorH = 400.0f;
constexpr float kVisorPad = 18.0f;

float columnX(int index) {
    return kSwitchLeft + static_cast<float>(index) * (kSwitchWidth + kSwitchGap);
}

juce::String switchLabel(int index) {
    switch (index) {
        case protocol::kButtonRecPlay: return "PLAY+REC";
        case protocol::kButtonPause: return "STOP";
        case protocol::kButtonUndo: return "CLEAR";
        case protocol::kButtonMode: return "MODE";
        case protocol::kButtonTrack1: return "1";
        case protocol::kButtonTrack2: return "2";
        case protocol::kButtonTrack3: return "3";
        case protocol::kButtonTrack4: return "4";
        default: return {};
    }
}

juce::Colour ledColourFor(protocol::LedColor color) {
    const auto& p = theme::palette();
    switch (color) {
        case protocol::kLedRed: return p.rec;
        case protocol::kLedGreen: return p.play;
        case protocol::kLedOrange: return p.amber;
        case protocol::kLedOff:
        default: return p.s3;
    }
}

// Mesma escala do VuMeter: -48 dB no fundo.
float levelToFraction(float amplitude) {
    if (amplitude <= 0.0001f) {
        return 0.0f;
    }
    const float db = 20.0f * std::log10(amplitude);
    return juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
}

juce::String clockText(double seconds) {
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return juce::String(minutes) + ":" + juce::String(rest, 1).paddedLeft('0', 4);
}
} // namespace

PedalMap::PedalMap() {
    setInterceptsMouseClicks(true, false);
}

bool PedalMap::sameStructure(const State& a, const State& b) const {
    for (int i = 0; i < protocol::kButtonCount; ++i) {
        if (a.pressed[i] != b.pressed[i]) {
            return false;
        }
    }
    for (int t = 0; t < config::kNumTracks; ++t) {
        if (a.led[t] != b.led[t] || a.ledBlink[t] != b.ledBlink[t] || a.trackState[t] != b.trackState[t] ||
            a.layers[t] != b.layers[t] || a.names[t] != b.names[t]) {
            return false;
        }
    }
    return a.loopDefined == b.loopDefined && a.recording == b.recording && a.mood == b.mood &&
           a.selectedTrack == b.selectedTrack && a.songText == b.songText &&
           juce::approximatelyEqual(a.loopSeconds, b.loopSeconds);
}

void PedalMap::updateState(const State& state) {
    // O piscar acompanha o relogio (300 ms, como o firmware), nao o numero de
    // repinturas.
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    const bool phase = ((now / 300) % 2) == 0;
    const bool structural = phase != blinkPhase_ || !sameStructure(state, state_);

    blinkPhase_ = phase;
    state_ = state;

    // Repintura por area: o que muda a todo quadro (medidores, relogio, anel)
    // vive no visor; o resto so muda por evento.
    if (structural) {
        repaint();
    } else {
        repaint(art(kVisorX, kVisorY, kVisorW, kVisorH).getSmallestIntegerContainer());
    }
}

juce::Rectangle<float> PedalMap::art(float x, float y, float w, float h) const {
    return {offsetX_ + x * scale_, offsetY_ + y * scale_, w * scale_, h * scale_};
}

juce::Rectangle<float> PedalMap::switchArt(int index) const {
    return {columnX(index), kSwitchTop, kSwitchWidth, kSwitchHeight};
}

void PedalMap::resized() {
    const auto bounds = getLocalBounds().toFloat();
    scale_ = juce::jmin(bounds.getWidth() / kArtWidth, bounds.getHeight() / kArtHeight);
    offsetX_ = bounds.getX() + (bounds.getWidth() - kArtWidth * scale_) * 0.5f;
    offsetY_ = bounds.getY() + (bounds.getHeight() - kArtHeight * scale_) * 0.5f;
}

int PedalMap::switchAt(juce::Point<int> position) const {
    for (int i = 0; i < protocol::kButtonCount; ++i) {
        const auto sw = switchArt(i);
        if (art(sw.getX(), sw.getY(), sw.getWidth(), sw.getHeight()).contains(position.toFloat())) {
            return i;
        }
    }
    return -1;
}

void PedalMap::mouseDown(const juce::MouseEvent& e) {
    mouseDownSwitch_ = switchAt(e.getPosition());
    mouseDownAtMs_ = juce::Time::getMillisecondCounter();
    if (mouseDownSwitch_ < 0) {
        return;
    }
    repaint();
    // Todo switch dispara no press-down, igual ao firmware - menos o CLEAR,
    // que precisa distinguir toque curto de hold e so decide no soltar.
    if (mouseDownSwitch_ != protocol::kButtonUndo && onPedalEvent) {
        onPedalEvent(static_cast<protocol::ButtonId>(mouseDownSwitch_), protocol::kGesturePress);
    }
}

void PedalMap::mouseUp(const juce::MouseEvent&) {
    if (mouseDownSwitch_ == protocol::kButtonUndo && onPedalEvent) {
        const bool held = (juce::Time::getMillisecondCounter() - mouseDownAtMs_) >=
                          static_cast<juce::uint32>(kClearHoldMs);
        onPedalEvent(protocol::kButtonUndo, held ? protocol::kGestureLongPress : protocol::kGesturePress);
    }
    mouseDownSwitch_ = -1;
    repaint();
}

void PedalMap::paint(juce::Graphics& g) {
    theme::paintCard(g, art(0.0f, 0.0f, kArtWidth, kArtHeight), 22.0f * scale_);
    paintVisor(g);
    paintLeds(g);
    paintSwitches(g);
}

void PedalMap::paintVisor(juce::Graphics& g) const {
    const auto& p = theme::palette();
    const auto visor = art(kVisorX, kVisorY, kVisorW, kVisorH);
    const float radius = 14.0f * scale_;
    g.setColour(p.bg);
    g.fillRoundedRectangle(visor, radius);

    // Moldura de modo, como a da tela de performance: luz na borda do visor.
    const juce::Colour mood = ui::frameColour(state_.mood);
    const bool stopped = state_.mood == ui::FrameMood::Stopped;
    g.setColour(mood.withAlpha(stopped ? 0.08f : 0.16f));
    g.drawRoundedRectangle(visor.reduced(4.0f * scale_), radius, 8.0f * scale_);
    g.setColour(mood.withAlpha(stopped ? 0.35f : 0.8f));
    g.drawRoundedRectangle(visor.reduced(1.0f), radius, juce::jmax(1.0f, 2.5f * scale_));

    // --- Info: em cima de PLAY+REC, STOP, CLEAR e MODE ---
    const float infoLeft = columnX(0) + kVisorPad;
    const float infoRight = columnX(3) + kSwitchWidth - kVisorPad;
    const float top = kVisorY + kVisorPad;
    const float height = kVisorH - kVisorPad * 2.0f;

    ui::RingState ring;
    ring.progress = state_.loopPosition;
    ring.defined = state_.loopDefined;
    ring.mood = state_.mood;
    ring.lengthSeconds = state_.loopSeconds;
    for (int t = 0; t < config::kNumTracks; ++t) {
        ring.states[static_cast<size_t>(t)] = state_.trackState[t];
    }
    ui::paintLoopRings(g, art(infoLeft, top, height, height), ring);

    const float textLeft = infoLeft + height + 28.0f;
    const float textW = infoRight - textLeft;
    auto clock = art(textLeft, top + 40.0f, textW, 80.0f);
    g.setColour(p.t1);
    g.setFont(theme::numbers(74.0f * scale_, true));
    const juce::String now = state_.loopDefined ? clockText(state_.positionSeconds) : juce::String("--");
    g.drawText(now, clock, juce::Justification::centredLeft, false);
    const float nowWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), now);
    g.setColour(p.t3);
    g.setFont(theme::numbers(28.0f * scale_));
    g.drawText(state_.loopDefined ? "/ " + juce::String(state_.loopSeconds, 1) + " s" : juce::String(),
               clock.withTrimmedLeft(nowWidth + 14.0f * scale_), juce::Justification::centredLeft, false);

    auto bar = art(textLeft, top + 150.0f, textW, 14.0f);
    theme::paintGroove(g, bar, bar.getHeight() * 0.5f);
    if (state_.loopDefined) {
        g.setColour(mood);
        g.fillRoundedRectangle(bar.withWidth(juce::jmax(bar.getHeight(), bar.getWidth() * state_.loopPosition)),
                               bar.getHeight() * 0.5f);
    }

    g.setColour(p.t2);
    g.setFont(theme::text(24.0f * scale_));
    const juce::String info = state_.songText.isNotEmpty()
                                  ? state_.songText
                                  : (state_.loopDefined ? juce::String()
                                                        : ui::utf8("Sem loop · aperte PLAY+REC para gravar"));
    g.drawFittedText(info, art(textLeft, top + 190.0f, textW, 40.0f).toNearestInt(),
                     juce::Justification::centredLeft, 1, 0.85f);

    // --- Um medidor em cima do botao de cada track ---
    for (int t = 0; t < config::kNumTracks; ++t) {
        const auto column = art(columnX(protocol::kButtonTrack1 + t) + 5.0f, top, kSwitchWidth - 10.0f, height);
        const float colRadius = 10.0f * scale_;
        g.setColour(p.s1);
        g.fillRoundedRectangle(column, colRadius);
        g.setColour(p.line);
        g.drawRoundedRectangle(column.reduced(0.5f), colRadius, 1.0f);
        if (t == state_.selectedTrack) {
            theme::paintSelection(g, column, colRadius, p.rec);
        }

        auto inner = column.reduced(12.0f * scale_);
        ui::paintStateChip(g, inner.removeFromTop(26.0f * scale_), state_.trackState[t]);
        inner.removeFromTop(6.0f * scale_);
        g.setColour(p.t2);
        g.setFont(theme::text(19.0f * scale_, theme::Weight::Semibold));
        g.drawText(ui::layersText(state_.layers[t]), inner.removeFromTop(24.0f * scale_),
                   juce::Justification::centredLeft, false);
        inner.removeFromTop(10.0f * scale_);

        // Barra continua verde; cinza na mutada (que continua medindo).
        const float barRadius = 6.0f * scale_;
        theme::paintGroove(g, inner, barRadius);
        const float fraction = levelToFraction(state_.level[t]);
        if (fraction > 0.0f) {
            g.setColour(state_.trackState[t] == TrackState::MUTED ? p.t3 : p.play);
            g.fillRoundedRectangle(inner.withTop(inner.getBottom() - inner.getHeight() * fraction), barRadius);
        }
    }
}

void PedalMap::paintLeds(juce::Graphics& g) const {
    for (int t = 0; t < config::kNumTracks; ++t) {
        const auto sw = switchArt(protocol::kButtonTrack1 + t);
        const float barW = sw.getWidth() * 0.46f;
        const auto bar = art(sw.getCentreX() - barW * 0.5f, kLedBarY, barW, kLedBarH);
        const bool on = (state_.led[t] != protocol::kLedOff) && (!state_.ledBlink[t] || blinkPhase_);
        const juce::Colour colour = ledColourFor(on ? state_.led[t] : protocol::kLedOff);
        if (on) {
            g.setColour(colour.withAlpha(0.3f));
            g.fillRoundedRectangle(bar.expanded(bar.getHeight() * 0.8f), bar.getHeight());
        }
        g.setColour(colour);
        g.fillRoundedRectangle(bar, bar.getHeight() * 0.5f);
    }
}

void PedalMap::paintSwitches(juce::Graphics& g) const {
    const auto& p = theme::palette();
    const float stroke = juce::jmax(1.0f, 2.0f * scale_);
    for (int i = 0; i < protocol::kButtonCount; ++i) {
        const auto sw = switchArt(i);
        const auto r = art(sw.getX(), sw.getY(), sw.getWidth(), sw.getHeight());
        const float radius = 20.0f * scale_;
        const bool pressed = state_.pressed[i] || (mouseDownSwitch_ == i);

        if (pressed) {
            // Flash ambar: o toque chegou. Nao se confunde com o vermelho de
            // gravacao nem com o verde de reproducao.
            g.setColour(p.amber.withAlpha(0.28f));
            g.fillRoundedRectangle(r.expanded(8.0f * scale_), radius * 1.3f);
            g.setColour(p.amber);
            g.fillRoundedRectangle(r, radius);
        } else {
            g.setColour(juce::Colours::black.withAlpha(theme::isDark() ? 0.35f : 0.08f));
            g.fillRoundedRectangle(r.translated(0.0f, 4.0f * scale_), radius);
            g.setGradientFill(juce::ColourGradient(p.s2, r.getX(), r.getY(), p.s3, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle(r, radius);
            g.setColour(p.line2);
            g.drawRoundedRectangle(r.reduced(stroke * 0.5f), radius, stroke);
        }

        const juce::Colour ink = pressed ? juce::Colour(0xff141414) : p.t1;
        const bool isTrack = (i >= protocol::kButtonTrack1);
        if (!isTrack) {
            g.setColour(ink);
            g.setFont(theme::caps(34.0f * scale_));
            g.drawFittedText(switchLabel(i), r.toNearestInt().reduced(static_cast<int>(10.0f * scale_), 0),
                             juce::Justification::centred, 1, 0.6f);
            continue;
        }

        const int t = i - protocol::kButtonTrack1;
        auto content = r.reduced(12.0f * scale_);
        auto numberArea = content.removeFromTop(content.getHeight() * 0.68f);
        g.setColour(ink);
        g.setFont(theme::text(120.0f * scale_, theme::Weight::Light));
        g.drawText(switchLabel(i), numberArea, juce::Justification::centredBottom, false);

        // Nome da track com o ponto de cor: o botao diz a qual track pertence.
        auto nameRow = content.withSizeKeepingCentre(content.getWidth(), 28.0f * scale_);
        const float dot = 12.0f * scale_;
        const juce::String name = state_.names[t];
        g.setFont(theme::text(20.0f * scale_, theme::Weight::Semibold));
        const float nameWidth = juce::jmin(nameRow.getWidth() - dot * 2.0f,
                                           juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name));
        const float startX = nameRow.getCentreX() - (dot * 1.6f + nameWidth) * 0.5f;
        g.setColour(theme::track(t));
        g.fillEllipse(startX, nameRow.getCentreY() - dot * 0.5f, dot, dot);
        g.setColour(pressed ? ink : p.t2);
        g.drawFittedText(name, juce::Rectangle<float>(startX + dot * 1.6f, nameRow.getY(), nameWidth + 4.0f,
                                                      nameRow.getHeight()).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.8f);
    }
}
