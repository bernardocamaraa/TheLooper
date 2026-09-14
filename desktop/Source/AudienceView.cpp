#include "AudienceView.h"

#include <cmath>

#include "PedalLookAndFeel.h"
#include "UiText.h"

namespace {
// Corpo de letra derivado do tamanho da janela: esta tela e projetada ou vista
// de longe, entao nada aqui pode ter tamanho fixo em pixel.
float fontScale(juce::Rectangle<int> bounds) {
    return juce::jlimit(0.75f, 3.0f, static_cast<float>(bounds.getWidth()) / 900.0f);
}

juce::String stateName(TrackState state) {
    switch (state) {
        case TrackState::RECORDING: return "GRAVANDO";
        case TrackState::PLAYING: return "TOCANDO";
        case TrackState::MUTED: return "MUDO";
        case TrackState::EMPTY:
        default: return "VAZIA";
    }
}
} // namespace

AudienceView::AudienceView(LooperEngine& engine, std::function<juce::String(int)> nameProvider)
    : engine_(engine), nameProvider_(std::move(nameProvider)) {
    setInterceptsMouseClicks(false, false);
}

void AudienceView::refresh() {
    Snapshot next;
    next.recMode = (engine_.mode() == GlobalMode::REC_MODE);
    next.playing = engine_.transportPlaying();
    next.mood = ui::moodFor(next.recMode, next.playing);
    next.loopDefined = engine_.loopDefined();
    next.selected = engine_.selectedTrack();
    next.progress = engine_.loopProgress();
    next.loopSeconds = engine_.loopLengthSeconds();

    for (int t = 0; t < config::kNumTracks; ++t) {
        next.state[t] = engine_.trackState(t);
        next.layers[t] = engine_.trackLayers(t);
        next.level[t] = engine_.trackLevel(t);
        next.recording = next.recording || (next.state[t] == TrackState::RECORDING);
    }

    // O piscar acompanha o relogio, como no resto do app (o LED do firmware
    // pisca a cada 300 ms).
    const bool phase = ((juce::Time::getMillisecondCounter() / 300) % 2) == 0;
    const bool blinkChanged = (phase != blinkPhase_);
    blinkPhase_ = phase;

    // REPINTURA POR AREA. Esta janela ocupa um MONITOR INTEIRO, e o desenho e
    // feito por software: repintar tudo a 30 Hz por causa do ponteiro que
    // andou tres pixels chega a travar a thread da interface. Entao so o que
    // realmente mudou e marcado como sujo - e o que muda a todo quadro (o
    // ponteiro e os niveis) mora em retangulos pequenos.
    const bool structuralChange =
        blinkChanged || next.mood != state_.mood || next.recMode != state_.recMode ||
        next.playing != state_.playing || next.loopDefined != state_.loopDefined ||
        next.recording != state_.recording || next.selected != state_.selected ||
        !juce::approximatelyEqual(next.loopSeconds, state_.loopSeconds);

    bool cardsChanged = false;
    for (int t = 0; t < config::kNumTracks && !cardsChanged; ++t) {
        cardsChanged = (next.state[t] != state_.state[t]) || (next.layers[t] != state_.layers[t]);
    }

    const bool moved = !juce::approximatelyEqual(next.progress, state_.progress);
    bool levelsChanged = false;
    for (int t = 0; t < config::kNumTracks && !levelsChanged; ++t) {
        levelsChanged = !juce::approximatelyEqual(next.level[t], state_.level[t]);
    }

    state_ = next;

    const juce::String hint = hintText();
    if (hint != hint_) {
        hint_ = hint;
        repaint(hintArea_);
    }

    if (structuralChange) {
        repaint();
        return;
    }
    if (cardsChanged || levelsChanged) {
        repaint(cardsArea_);
    }
    if (moved) {
        repaint(wheelCircle_);
    }
}

void AudienceView::resized() {
    auto area = getLocalBounds().reduced(static_cast<int>(ui::frameThickness()));
    const float scale = fontScale(getLocalBounds());

    headerArea_ = area.removeFromTop(juce::roundToInt(38.0f * scale));
    hintArea_ = area.removeFromBottom(juce::roundToInt(76.0f * scale));
    area.removeFromBottom(juce::roundToInt(10.0f * scale));

    // O circulo fica com a maior parte: e ele o retrato do que esta
    // acontecendo. Os cartoes das tracks sao a legenda dele.
    wheelArea_ = area.removeFromLeft(juce::roundToInt(static_cast<float>(area.getWidth()) * 0.56f));
    // Retangulo do circulo, com uma folga para o halo do ponteiro (mesma
    // geometria de paintWheel).
    {
        const float outer = juce::jmin(static_cast<float>(wheelArea_.getWidth()),
                                        static_cast<float>(wheelArea_.getHeight())) *
                            0.44f;
        const float reach = outer * 1.22f;
        wheelCircle_ = juce::Rectangle<float>(reach * 2.0f, reach * 2.0f)
                            .withCentre(wheelArea_.toFloat().getCentre())
                            .getSmallestIntegerContainer();
    }
    area.removeFromLeft(juce::roundToInt(16.0f * scale));
    cardsArea_ = area;
}

juce::Colour AudienceView::trackColour(int track) const {
    switch (state_.state[track]) {
        case TrackState::RECORDING: return theme::ledRed;
        case TrackState::PLAYING: return theme::ledGreen;
        case TrackState::MUTED: return theme::inkDim;
        case TrackState::EMPTY:
        default: return theme::hairline;
    }
}

// A frase e escolhida pelo ESTADO REAL do motor, nunca por um passo-a-passo
// guardado a parte: se o visitante fizer algo fora da ordem, a tela acompanha
// em vez de continuar pedindo o passo que ele ja pulou.
juce::String AudienceView::hintText() const {
    if (!state_.recMode) {
        return ui::utf8("MODO MUDO · os botões 1 a 4 ligam e desligam cada track");
    }
    if (state_.recording && !state_.loopDefined) {
        return ui::utf8("Gravando a base · aperte PLAY+REC de novo para fechar o loop");
    }
    if (state_.recording) {
        return ui::utf8("Gravando por cima · a camada entra na próxima volta");
    }
    if (!state_.loopDefined) {
        return ui::utf8("Aperte PLAY+REC e toque ou cante · o loop começa agora");
    }
    if (!state_.playing) {
        return ui::utf8("Parado · aperte PLAY+REC para tocar o loop de novo");
    }
    return ui::utf8("Escolha uma track (1 a 4) e aperte PLAY+REC para somar uma camada");
}

// O circulo: quatro aneis concentricos, um por track, e um ponteiro que da uma
// volta a cada volta do loop.
void AudienceView::paintWheel(juce::Graphics& g) const {
    const auto area = wheelArea_.toFloat();
    const float cx = area.getCentreX();
    const float cy = area.getCentreY();
    const float outer = juce::jmin(area.getWidth(), area.getHeight()) * 0.44f;
    const float ringGap = outer * 0.17f;
    const float thickness = ringGap * 0.62f;
    const juce::Colour moodColour = ui::frameColour(state_.mood);

    float innerRadius = outer;
    for (int t = 0; t < config::kNumTracks; ++t) {
        const float radius = outer - static_cast<float>(t) * ringGap;
        innerRadius = radius;

        const bool recording = (state_.state[t] == TrackState::RECORDING);
        const bool empty = (state_.state[t] == TrackState::EMPTY);
        // O brilho conta as camadas: quanto mais gravacao empilhada, mais
        // "cheio" o anel parece. Quatro camadas ja saturam - depois disso a
        // diferenca nao seria mais legivel de longe.
        const float fill = juce::jlimit(0.0f, 1.0f, static_cast<float>(state_.layers[t]) / 4.0f);
        juce::Colour colour = trackColour(t);
        if (recording && !blinkPhase_) {
            colour = colour.darker(0.6f);
        }
        colour = colour.withAlpha(empty ? 0.6f : (0.62f + 0.38f * fill));

        if (!empty) {
            g.setColour(colour.withAlpha(0.16f));
            g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, thickness * 2.1f);
        }
        g.setColour(colour);
        g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, thickness);
    }

    // Numero da track apoiado no proprio anel, as 9 horas: e o que liga o
    // desenho aos botoes 1-4 do pedal sem precisar de legenda.
    const float scale = fontScale(getLocalBounds());
    g.setFont(theme::legend(15.0f * scale, true));
    for (int t = 0; t < config::kNumTracks; ++t) {
        const float radius = outer - static_cast<float>(t) * ringGap;
        g.setColour(trackColour(t).withAlpha(state_.state[t] == TrackState::EMPTY ? 0.8f : 1.0f));
        g.drawText(juce::String(t + 1),
                    juce::Rectangle<float>(cx - radius - ringGap * 0.5f, cy - ringGap * 0.5f, ringGap,
                                            ringGap),
                    juce::Justification::centred, false);
    }

    // Miolo: o comprimento do loop em segundos. E o numero que a plateia
    // reconhece - "tudo isso cabe em 4,8 segundos".
    g.setColour(theme::inkFaint);
    g.setFont(theme::legend(13.0f * scale, true));
    g.drawText("LOOP",
                juce::Rectangle<float>(cx - outer, cy - 34.0f * scale, outer * 2.0f, 18.0f * scale),
                juce::Justification::centred, false);

    g.setColour(state_.loopDefined ? theme::ink : theme::inkFaint);
    g.setFont(theme::data(30.0f * scale));
    g.drawText(state_.loopDefined ? juce::String(state_.loopSeconds, 1) + " s" : juce::String("--"),
                juce::Rectangle<float>(cx - outer, cy - 14.0f * scale, outer * 2.0f, 40.0f * scale),
                juce::Justification::centred, false);

    if (!state_.loopDefined) {
        return;
    }

    // O ponteiro cruza os quatro aneis de uma vez: e o mesmo loop passando
    // pelas quatro tracks, nao quatro progressos independentes.
    const float sweep = static_cast<float>(state_.progress) * juce::MathConstants<float>::twoPi;
    const float sinA = std::sin(sweep);
    const float cosA = std::cos(sweep);
    const float from = innerRadius - thickness;
    const float to = outer + thickness;

    g.setColour(moodColour.withAlpha(0.9f));
    g.drawLine(cx + sinA * from, cy - cosA * from, cx + sinA * to, cy - cosA * to,
                juce::jmax(2.0f, 3.0f * scale));

    const float headRadius = juce::jmax(4.0f, 7.0f * scale);
    g.setColour(moodColour.withAlpha(0.3f));
    g.fillEllipse(cx + sinA * outer - headRadius * 2.2f, cy - cosA * outer - headRadius * 2.2f,
                   headRadius * 4.4f, headRadius * 4.4f);
    g.setColour(juce::Colours::white.interpolatedWith(moodColour, 0.5f));
    g.fillEllipse(cx + sinA * outer - headRadius, cy - cosA * outer - headRadius, headRadius * 2.0f,
                   headRadius * 2.0f);
}

void AudienceView::paintCards(juce::Graphics& g) const {
    const float scale = fontScale(getLocalBounds());
    const int gap = juce::roundToInt(10.0f * scale);
    const int cardHeight = (cardsArea_.getHeight() - gap * (config::kNumTracks - 1)) / config::kNumTracks;

    auto area = cardsArea_;
    for (int t = 0; t < config::kNumTracks; ++t) {
        auto card = area.removeFromTop(cardHeight);
        area.removeFromTop(gap);

        const bool selected = state_.recMode && (t == state_.selected);
        theme::paintPanel(g, card, selected);

        auto inner = card.reduced(juce::roundToInt(12.0f * scale), juce::roundToInt(8.0f * scale));

        // Barra de cor a esquerda: a mesma cor do anel da track no circulo, que
        // e o que amarra as duas metades da tela.
        auto swatch = inner.removeFromLeft(juce::roundToInt(7.0f * scale));
        g.setColour(trackColour(t));
        g.fillRoundedRectangle(swatch.toFloat().reduced(0.0f, 2.0f * scale), 3.0f * scale);
        inner.removeFromLeft(juce::roundToInt(12.0f * scale));

        // Medidor de nivel na direita: e o VU, so que reduzido ao essencial -
        // quem quiser o medidor completo abre a janela de VUs.
        auto meter = inner.removeFromRight(juce::roundToInt(10.0f * scale));
        theme::paintGroove(g, meter.toFloat(), 3.0f * scale);
        const float level = juce::jlimit(0.0f, 1.0f, state_.level[t]);
        if (level > 0.001f) {
            auto filled = meter.toFloat().reduced(1.5f * scale);
            filled = filled.withTop(filled.getBottom() - filled.getHeight() * level);
            g.setColour(trackColour(t));
            g.fillRect(filled);
        }
        inner.removeFromRight(juce::roundToInt(10.0f * scale));

        auto nameRow = inner.removeFromTop(juce::roundToInt(static_cast<float>(inner.getHeight()) * 0.56f));
        g.setColour(theme::ink);
        g.setFont(theme::display(23.0f * scale));
        g.drawText(nameProvider_ ? nameProvider_(t) : juce::String(), nameRow,
                    juce::Justification::centredLeft, false);

        g.setColour(trackColour(t));
        g.setFont(theme::legend(13.0f * scale, true));
        g.drawText(stateName(state_.state[t]), inner, juce::Justification::centredLeft, false);

        if (state_.layers[t] > 0) {
            g.setColour(theme::inkDim);
            g.setFont(theme::legend(13.0f * scale));
            const juce::String layers = juce::String(state_.layers[t]) +
                                         (state_.layers[t] == 1 ? " camada" : " camadas");
            g.drawText(layers, inner, juce::Justification::centredRight, false);
        }
    }
}

void AudienceView::paintHint(juce::Graphics& g) const {
    const float scale = fontScale(getLocalBounds());
    const auto box = hintArea_.toFloat();

    g.setColour(theme::panel);
    g.fillRoundedRectangle(box, 8.0f * scale);
    g.setColour(ui::frameColour(state_.mood));
    g.drawRoundedRectangle(box.reduced(1.0f), 8.0f * scale, juce::jmax(1.5f, 2.5f * scale));

    g.setColour(theme::ink);
    g.setFont(theme::legend(21.0f * scale, true));
    g.drawFittedText(hint_, hintArea_.reduced(juce::roundToInt(16.0f * scale), 0),
                      juce::Justification::centred, 2, 0.5f);
}

void AudienceView::paint(juce::Graphics& g) {
    g.fillAll(theme::enclosure);

    const float scale = fontScale(getLocalBounds());

    g.setColour(theme::inkDim);
    g.setFont(theme::legend(17.0f * scale, true));
    g.drawText("PEDAL LOOPER", headerArea_, juce::Justification::centredLeft, false);

    const juce::String modeName = !state_.playing ? ui::utf8("PARADO")
                                  : state_.recMode ? ui::utf8("MODO GRAVAÇÃO")
                                                   : ui::utf8("MODO MUDO");
    g.setColour(ui::frameColour(state_.mood));
    g.setFont(theme::legend(17.0f * scale, true));
    g.drawText(modeName, headerArea_, juce::Justification::centredRight, false);

    paintWheel(g);
    paintCards(g);
    paintHint(g);

    ui::paintModeFrame(g, getLocalBounds(), state_.mood);
}
