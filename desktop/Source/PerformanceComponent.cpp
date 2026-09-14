#include "PerformanceComponent.h"

#include "PedalLookAndFeel.h"

PerformanceComponent::PerformanceComponent(LooperEngine& engine,
                                            std::function<juce::String(int)> nameProvider)
    : engine_(engine), nameProvider_(std::move(nameProvider)) {
    for (int i = 0; i < config::kNumTracks; ++i) {
        auto meter = std::make_unique<TrackMeterPanel>();
        meter->setLevelSource([this, i] { return engine_.trackLevel(i); });
        addAndMakeVisible(*meter);
        meters_[static_cast<size_t>(i)] = std::move(meter);
    }

    audience_ = std::make_unique<AudienceView>(engine_, nameProvider_);
    addChildComponent(*audience_); // so aparece quando setAudienceView(true)
}

void PerformanceComponent::setAudienceView(bool audience) {
    if (audience == audienceView_) {
        return;
    }
    audienceView_ = audience;

    audience_->setVisible(audience);
    for (auto& meter : meters_) {
        meter->setVisible(!audience);
    }
    resized();
    repaint();
}

void PerformanceComponent::refresh() {
    if (audienceView_) {
        audience_->refresh();
        return;
    }

    const bool recMode = (engine_.mode() == GlobalMode::REC_MODE);
    const bool playing = engine_.transportPlaying();
    const int selected = engine_.selectedTrack();

    const ui::FrameMood mood = ui::moodFor(recMode, playing);
    const double progress = engine_.loopProgress();
    const bool defined = engine_.loopDefined();

    if (mood != mood_) {
        mood_ = mood;
        repaint();
    }
    if (!juce::approximatelyEqual(progress, progress_) || defined != loopDefined_) {
        progress_ = progress;
        loopDefined_ = defined;
        // So a faixa dos medidores precisa ser repintada para o playhead
        // andar - repintar a janela inteira a 30 Hz seria desperdicio.
        repaint(meterRow_);
    }

    for (int i = 0; i < config::kNumTracks; ++i) {
        meters_[static_cast<size_t>(i)]->update(nameProvider_ ? nameProvider_(i) : juce::String(),
                                                 engine_.trackState(i), recMode && (i == selected),
                                                 engine_.trackLayers(i), engine_.trackInputMask(i));
    }
}

void PerformanceComponent::resized() {
    // A tela de plateia pinta a propria moldura de modo, entao ela recebe a
    // janela inteira, sem a margem que os medidores precisam.
    audience_->setBounds(getLocalBounds());
    if (audienceView_) {
        return;
    }

    // Margem para o conteudo nao encostar na moldura de modo.
    auto area = getLocalBounds().reduced(static_cast<int>(ui::frameThickness()));

    meterRow_ = area;

    const int gap = 14;
    const int panelWidth = (area.getWidth() - gap * (config::kNumTracks - 1)) / config::kNumTracks;
    for (int i = 0; i < config::kNumTracks; ++i) {
        meters_[static_cast<size_t>(i)]->setBounds(area.removeFromLeft(panelWidth));
        area.removeFromLeft(gap);
    }
}

void PerformanceComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::enclosure);
    ui::paintModeFrame(g, getLocalBounds(), mood_);
}

void PerformanceComponent::paintOverChildren(juce::Graphics& g) {
    // Na tela de plateia quem marca a posicao do loop e o ponteiro do circulo -
    // o playhead dos medidores passaria por cima dela.
    if (!loopDefined_ || audienceView_) {
        return;
    }

    // ------------------------------------------------------------------
    // O playhead.
    //
    // A invariante central deste looper e que as 4 tracks compartilham UM
    // comprimento de loop (ver docs/CONTROL_MODEL.md). Quatro barras de
    // progresso separadas nao diriam isso - diriam o contrario. Uma linha so,
    // atravessando a regua inteira da esquerda para a direita e voltando ao
    // reiniciar, e a propria informacao: e o mesmo loop passando pelas
    // quatro.
    //
    // Precisa ser pintada aqui (paintOverChildren) e nao em paint(): os
    // painies das tracks sao filhos e se desenham por cima do pai, entao uma
    // linha pintada antes deles ficaria escondida.
    // ------------------------------------------------------------------
    const auto row = meterRow_.toFloat();
    const float x = row.getX() + static_cast<float>(progress_) * row.getWidth();
    const juce::Colour colour = ui::frameColour(mood_);

    // Rastro curto atras da linha, para o sentido do movimento ficar obvio
    // mesmo num relance.
    const float trail = 46.0f;
    if (x > row.getX()) {
        auto trailArea = juce::Rectangle<float>(juce::jmax(row.getX(), x - trail), row.getY(),
                                                 juce::jmin(trail, x - row.getX()), row.getHeight());
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.0f), trailArea.getX(), 0.0f,
                                                colour.withAlpha(0.16f), x, 0.0f, false));
        g.fillRect(trailArea);
    }

    g.setColour(colour.withAlpha(0.85f));
    g.fillRect(x - 0.75f, row.getY(), 1.5f, row.getHeight());

    // Cabeca da linha nas duas pontas: e o que a faz parecer um cursor de
    // transporte, e nao uma divisoria entre painies.
    juce::Path head;
    const float s = 5.0f;
    head.addTriangle(x - s, row.getY(), x + s, row.getY(), x, row.getY() + s * 1.4f);
    head.addTriangle(x - s, row.getBottom(), x + s, row.getBottom(), x, row.getBottom() - s * 1.4f);
    g.setColour(colour);
    g.fillPath(head);
}

// ---------------------------------------------------------------------------

// Janela de APRESENTACAO: sem barra de titulo, sem botoes, ocupando um monitor
// inteiro. Barra de titulo e botao de fechar nao tem uso aqui - esta janela
// fica virada para a plateia ou para quem toca, e um "X" no canto so serve
// para alguem fechar sem querer no meio da feira. Quem mostra, esconde e
// escolhe o monitor e a janela de controles (menu "Telas").
PerformanceWindow::PerformanceWindow(const juce::String& name, juce::Component* content)
    : DocumentWindow(name, theme::enclosure, 0) {
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);
    setDropShadowEnabled(false);
    setContentNonOwned(content, false);
    setResizable(false, false);
}

void PerformanceWindow::showOnDisplay(int displayIndex) {
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (displays.isEmpty()) {
        setVisible(true);
        return;
    }

    const auto& display = displays[juce::jlimit(0, displays.size() - 1, displayIndex)];
    // totalArea, nao userArea: tela cheia de verdade, sem a faixa da barra de
    // tarefas. Nao e always-on-top de proposito - se as duas janelas caissem no
    // mesmo monitor, uma janela sempre no topo esconderia os controles.
    setBounds(display.totalArea);
    setVisible(true);
    toFront(true);
}

void PerformanceWindow::closeButtonPressed() {
    // Esconder, nao encerrar: quem fecha o app e a janela de controles.
    setVisible(false);
    if (onHidden) {
        onHidden();
    }
}

bool PerformanceWindow::keyPressed(const juce::KeyPress& key) {
    // Valvula de escape: sem barra de titulo, ESC e a unica saida a partir da
    // propria janela se ela ficar na frente de algo na hora errada.
    if (key == juce::KeyPress::escapeKey) {
        closeButtonPressed();
        return true;
    }
    if (onKeyPressed && onKeyPressed(key)) {
        return true;
    }
    return DocumentWindow::keyPressed(key);
}
