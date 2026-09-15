#include "PerformanceComponent.h"

#include "PedalLookAndFeel.h"

namespace {
juce::String clockText(double seconds) {
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return juce::String(minutes) + ":" + juce::String(rest, 1).paddedLeft('0', 4);
}
} // namespace

PerformanceComponent::PerformanceComponent(LooperEngine& engine, std::function<juce::String(int)> nameProvider,
                                           std::function<juce::String()> songProvider)
    : engine_(engine), nameProvider_(std::move(nameProvider)), songProvider_(std::move(songProvider)) {
    addAndMakeVisible(progressBar_);
    for (int i = 0; i < config::kNumTracks; ++i) {
        auto meter = std::make_unique<TrackMeterPanel>();
        meter->setTrackIndex(i);
        meter->setLevelSource([this, i] { return engine_.trackLevel(i); });
        addAndMakeVisible(*meter);
        meters_[static_cast<size_t>(i)] = std::move(meter);
    }
}

void PerformanceComponent::refreshColours() {
    for (auto& meter : meters_) {
        meter->repaint();
    }
    repaint();
}

void PerformanceComponent::refresh() {
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
    progressBar_.setProgress(progress, defined, mood);

    const juce::String clock = defined ? clockText(engine_.loopPositionSeconds()) : juce::String("--");
    const juce::String length = defined ? "/ " + juce::String(engine_.loopLengthSeconds(), 1) + " s" : juce::String();
    if (clock != clock_ || length != length_) {
        clock_ = clock;
        length_ = length;
        repaint(clockArea_);
    }
    const juce::String song = songProvider_ ? songProvider_() : juce::String();
    if (song != song_) {
        song_ = song;
        repaint(songArea_);
    }

    if (!juce::approximatelyEqual(progress, progress_) || defined != loopDefined_) {
        progress_ = progress;
        loopDefined_ = defined;
        repaint(meterRow_); // so a faixa das colunas, para o playhead andar
    }

    for (int i = 0; i < config::kNumTracks; ++i) {
        meters_[static_cast<size_t>(i)]->update(nameProvider_ ? nameProvider_(i) : juce::String(),
                                                engine_.trackState(i), recMode && (i == selected),
                                                engine_.trackLayers(i), engine_.trackInputMask(i));
    }
}

void PerformanceComponent::resized() {
    const float h = static_cast<float>(getHeight());
    const int margin = juce::roundToInt(ui::frameThickness() + h * 0.02f);
    auto area = getLocalBounds().reduced(margin);

    // Barra de cima: relogio | progresso | musica.
    auto top = area.removeFromTop(juce::roundToInt(h * 0.1f));
    clockArea_ = top.removeFromLeft(juce::roundToInt(static_cast<float>(top.getWidth()) * 0.3f));
    songArea_ = top.removeFromRight(juce::roundToInt(static_cast<float>(top.getWidth()) * 0.3f));
    progressBar_.setBounds(top.reduced(juce::roundToInt(h * 0.012f), 0)
                               .withSizeKeepingCentre(top.getWidth() - juce::roundToInt(h * 0.024f),
                                                      juce::jmax(6, juce::roundToInt(h * 0.012f))));
    area.removeFromTop(juce::roundToInt(h * 0.025f));

    meterRow_ = area;
    const int gap = juce::roundToInt(h * 0.015f);
    const int panelWidth = (area.getWidth() - gap * (config::kNumTracks - 1)) / config::kNumTracks;
    for (int i = 0; i < config::kNumTracks; ++i) {
        meters_[static_cast<size_t>(i)]->setBounds(area.removeFromLeft(panelWidth));
        area.removeFromLeft(gap);
    }
}

void PerformanceComponent::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    g.fillAll(p.bg);
    ui::paintModeFrame(g, getLocalBounds(), mood_);

    const float h = static_cast<float>(clockArea_.getHeight());
    g.setColour(p.t1);
    g.setFont(theme::numbers(h * 0.78f, true));
    g.drawText(clock_, clockArea_, juce::Justification::centredLeft, false);
    const float clockWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), clock_);
    g.setColour(p.t3);
    g.setFont(theme::numbers(h * 0.34f));
    g.drawText(length_, clockArea_.withTrimmedLeft(juce::roundToInt(clockWidth + h * 0.2f)),
               juce::Justification::centredLeft, false);

    g.setColour(p.t2);
    g.setFont(theme::text(h * 0.32f, theme::Weight::Semibold));
    g.drawFittedText(song_, songArea_, juce::Justification::centredRight, 1, 0.8f);
}

void PerformanceComponent::paintOverChildren(juce::Graphics& g) {
    // O playhead: UMA linha atravessando as quatro colunas - o mesmo loop
    // passando por todas as tracks, que e a invariante central do looper.
    if (!loopDefined_) {
        return;
    }
    const auto row = meterRow_.toFloat();
    const float x = row.getX() + static_cast<float>(progress_) * row.getWidth();
    g.setColour(ui::frameColour(mood_).withAlpha(0.55f));
    g.fillRect(x - 1.0f, row.getY(), 2.0f, row.getHeight());
}

// ---------------------------------------------------------------------------

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
    // Tela cheia de verdade (sem a faixa da barra de tarefas). Nao e
    // always-on-top: se as duas janelas cairem no mesmo monitor, uma janela
    // sempre no topo esconderia os controles.
    setBounds(display.totalArea);
    setVisible(true);
    toFront(true);
}

void PerformanceWindow::closeButtonPressed() {
    setVisible(false);
    if (onHidden) {
        onHidden();
    }
}

bool PerformanceWindow::keyPressed(const juce::KeyPress& key) {
    // Sem barra de titulo, ESC e a unica saida a partir da propria janela.
    if (key == juce::KeyPress::escapeKey) {
        closeButtonPressed();
        return true;
    }
    if (onKeyPressed && onKeyPressed(key)) {
        return true;
    }
    return DocumentWindow::keyPressed(key);
}
