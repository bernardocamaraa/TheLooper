#include "AppShell.h"

#include "Brand.h"
#include "LoopProgressBar.h"
#include "PedalLookAndFeel.h"
#include "UiText.h"

namespace {
juce::String clockText(double seconds) {
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return juce::String(minutes) + ":" + juce::String(rest, 1).paddedLeft('0', 4);
}
} // namespace

// ============================================================================
// Barra de navegacao
// ============================================================================

class AppShell::NavRail : public juce::Component {
public:
    explicit NavRail(AppShell& shell) : shell_(shell) {}

    bool horizontal = false;
    bool pedalConnected = false;

    void setPedalConnected(bool connected) {
        if (connected != pedalConnected) {
            pedalConnected = connected;
            repaint();
        }
    }

    juce::Rectangle<float> itemArea(int index) const {
        const int count = static_cast<int>(shell_.pages_.size());
        auto r = getLocalBounds().toFloat();
        if (horizontal) {
            r = r.reduced(theme::pxf(6.0f), theme::pxf(6.0f));
            const float w = r.getWidth() / static_cast<float>(juce::jmax(1, count));
            return {r.getX() + w * static_cast<float>(index), r.getY(), w, r.getHeight()};
        }
        r = r.reduced(theme::pxf(10.0f));
        r.removeFromTop(theme::pxf(64.0f)); // marca
        const float h = theme::pxf(66.0f);
        return {r.getX(), r.getY() + (h + theme::pxf(4.0f)) * static_cast<float>(index), r.getWidth(), h};
    }

    void paint(juce::Graphics& g) override {
        const auto& p = theme::palette();
        g.fillAll(p.s1);
        g.setColour(p.line);
        if (horizontal) {
            g.fillRect(0.0f, 0.0f, static_cast<float>(getWidth()), 1.0f);
        } else {
            g.fillRect(static_cast<float>(getWidth()) - 1.0f, 0.0f, 1.0f, static_cast<float>(getHeight()));
            const auto markArea = getLocalBounds().toFloat().reduced(theme::pxf(10.0f)).removeFromTop(theme::pxf(54.0f));
            brand::draw(g, brand::mark(), markArea.reduced(theme::pxf(6.0f)), p.t1);
        }

        for (size_t i = 0; i < shell_.pages_.size(); ++i) {
            const auto& entry = shell_.pages_[i];
            const auto r = itemArea(static_cast<int>(i));
            const bool active = entry.id == shell_.current_;
            const bool hover = static_cast<int>(i) == hovered_;
            if (active || hover) {
                g.setColour(active ? p.s3 : p.s2);
                g.fillRoundedRectangle(r, theme::pxf(12.0f));
            }
            const juce::Colour colour = active ? p.t1 : (hover ? p.t2 : p.t3);
            const float iconSize = horizontal ? r.getHeight() * 0.4f : theme::pxf(24.0f);
            const float labelH = horizontal ? r.getHeight() * 0.22f : theme::pxf(13.0f);
            const float blockH = iconSize + labelH * 1.5f;
            const float top = r.getCentreY() - blockH * 0.5f;
            ui::drawIcon(g, entry.icon, {r.getCentreX() - iconSize * 0.5f, top, iconSize, iconSize}, colour);
            g.setColour(colour);
            g.setFont(theme::text(labelH, theme::Weight::Semibold));
            g.drawText(entry.label, juce::Rectangle<float>(r.getX(), top + iconSize + labelH * 0.3f, r.getWidth(), labelH * 1.2f),
                       juce::Justification::centred, false);
        }

        if (!horizontal) {
            // Estado do pedal, no pe da barra.
            auto foot = getLocalBounds().toFloat().reduced(theme::pxf(10.0f)).removeFromBottom(theme::pxf(52.0f));
            const float dot = theme::pxf(9.0f);
            const juce::Colour colour = pedalConnected ? p.play : p.amber;
            g.setColour(colour.withAlpha(0.2f));
            g.fillEllipse(foot.getCentreX() - dot, foot.getY(), dot * 2.0f, dot * 2.0f);
            g.setColour(colour);
            g.fillEllipse(foot.getCentreX() - dot * 0.5f, foot.getY() + dot * 0.5f, dot, dot);
            g.setColour(p.t3);
            g.setFont(theme::text(theme::pxf(10.5f)));
            g.drawFittedText(pedalConnected ? "Pedal\nconectado" : "Pedal\ndesconectado",
                             foot.withTrimmedTop(dot * 2.5f).toNearestInt(), juce::Justification::centredTop, 2, 0.8f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(hitTest(e.position)); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }
    void mouseUp(const juce::MouseEvent& e) override {
        const int index = hitTest(e.position);
        if (index >= 0 && e.mouseWasClicked()) {
            shell_.showPageIndex(index);
        }
    }

private:
    int hitTest(juce::Point<float> position) const {
        for (size_t i = 0; i < shell_.pages_.size(); ++i) {
            if (itemArea(static_cast<int>(i)).contains(position)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void setHovered(int index) {
        if (index != hovered_) {
            hovered_ = index;
            repaint();
        }
    }

    AppShell& shell_;
    int hovered_ = -1;
};

// ============================================================================
// HUD: o que o tecnico le a 5 metros
// ============================================================================

class AppShell::StatusHud : public juce::Component {
public:
    explicit StatusHud(AppContext& context) : ctx_(context) {
        addAndMakeVisible(progress_);

        record_.setButtonText("Gravar WAV");
        ui::setButtonStyle(record_, "record");
        ui::setButtonIcon(record_, "record");
        record_.setClickingTogglesState(false);
        record_.setWantsKeyboardFocus(false);
        record_.setTooltip(ui::utf8("Grava em WAV 24 bits o que sai pelos alto-falantes, já mixado."));
        record_.onClick = [this] { ctx_.setRecordingToDisk(!ctx_.recordingToDisk()); };
        addAndMakeVisible(record_);
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    bool compact = false;

    void refresh() {
        LooperEngine& engine = ctx_.engine();
        const bool recMode = engine.mode() == GlobalMode::REC_MODE;
        const bool playing = engine.transportPlaying();
        bool recording = false;
        for (int t = 0; t < config::kNumTracks; ++t) {
            recording = recording || engine.trackState(t) == TrackState::RECORDING;
        }

        juce::String pill;
        if (!playing) {
            pill = "PARADO";
        } else if (recMode) {
            pill = recording ? "GRAVANDO" : "REC";
        } else {
            pill = "PLAY";
        }
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const bool blinkOn = !recording || ((now / 500) % 2 == 0);

        const bool defined = engine.loopDefined();
        const juce::String clock = defined ? clockText(engine.loopPositionSeconds()) : juce::String("--");
        const juce::String length = defined ? "/ " + juce::String(engine.loopLengthSeconds(), 1) + " s" : juce::String();
        const juce::String song = ctx_.setlist().statusText();
        const juce::String audio = ctx_.audioSummary();

        mood_ = ui::moodFor(recMode, playing);
        progress_.setProgress(engine.loopProgress(), defined, mood_);

        const bool rec = ctx_.recordingToDisk();
        if (rec != record_.getToggleState()) {
            record_.setToggleState(rec, juce::dontSendNotification);
        }

        if (pill != pill_ || blinkOn != blinkOn_ || recMode != recMode_ || playing != playing_) {
            pill_ = pill;
            blinkOn_ = blinkOn;
            recMode_ = recMode;
            playing_ = playing;
            repaint(pillArea_.expanded(4));
        }
        if (clock != clock_ || length != length_) {
            clock_ = clock;
            length_ = length;
            repaint(clockArea_);
        }
        if (song != song_ || audio != audio_) {
            song_ = song;
            audio_ = audio;
            repaint(metaArea_);
        }
    }

    void resized() override {
        auto r = getLocalBounds().reduced(theme::px(compact ? 14.0f : 20.0f), theme::px(10));
        if (compact) {
            auto top = r.removeFromTop(r.getHeight() * 6 / 10);
            pillArea_ = top.removeFromLeft(theme::px(118)).withSizeKeepingCentre(theme::px(118), theme::px(38));
            record_.setBounds(top.removeFromRight(theme::px(44)).withSizeKeepingCentre(theme::px(44), theme::px(38)));
            top.removeFromRight(theme::px(8));
            clockArea_ = top.withTrimmedLeft(theme::px(10));
            metaArea_ = {};
            progress_.setBounds(r.withSizeKeepingCentre(r.getWidth(), theme::px(8)));
            record_.setButtonText({});
            return;
        }
        pillArea_ = r.removeFromLeft(theme::px(138)).withSizeKeepingCentre(theme::px(138), theme::px(42));
        r.removeFromLeft(theme::px(18));
        clockArea_ = r.removeFromLeft(theme::px(230));
        r.removeFromLeft(theme::px(10));
        record_.setBounds(r.removeFromRight(theme::px(148)).withSizeKeepingCentre(theme::px(148), theme::px(40)));
        record_.setButtonText("Gravar WAV");
        r.removeFromRight(theme::px(16));
        metaArea_ = r.removeFromRight(theme::px(250));
        r.removeFromRight(theme::px(18));
        progress_.setBounds(r.withSizeKeepingCentre(r.getWidth(), theme::px(9)));
    }

    void paint(juce::Graphics& g) override {
        const auto& p = theme::palette();
        g.fillAll(p.s1.withAlpha(theme::isDark() ? 0.78f : 0.9f));
        g.setColour(p.line);
        g.fillRect(0.0f, static_cast<float>(getHeight()) - 1.0f, static_cast<float>(getWidth()), 1.0f);

        // Pilula de modo: a leitura mais importante da tela.
        const auto pill = pillArea_.toFloat();
        juce::Colour fill = p.s3;
        juce::Colour text = p.t2;
        if (playing_ && recMode_) {
            fill = p.rec;
            text = juce::Colours::white;
        } else if (playing_) {
            fill = p.play;
            text = juce::Colour(0xff04210f);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
        const float dot = pill.getHeight() * 0.24f;
        if (blinkOn_) {
            g.setColour(text);
            g.fillEllipse(pill.getX() + pill.getHeight() * 0.36f, pill.getCentreY() - dot * 0.5f, dot, dot);
        }
        g.setColour(text);
        g.setFont(theme::text(pill.getHeight() * 0.38f, theme::Weight::Bold).withExtraKerningFactor(0.12f));
        g.drawText(pill_, pill.withTrimmedLeft(pill.getHeight() * 0.5f), juce::Justification::centred, false);

        // Relogio do loop, em numeros tabulares.
        const float clockH = static_cast<float>(clockArea_.getHeight());
        g.setColour(p.t1);
        g.setFont(theme::numbers(juce::jmin(clockH * 0.62f, theme::pxf(32.0f)), true));
        g.drawText(clock_, clockArea_, juce::Justification::centredLeft, false);
        const float clockW = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), clock_);
        g.setColour(p.t3);
        g.setFont(theme::numbers(juce::jmin(clockH * 0.3f, theme::pxf(15.0f))));
        g.drawText(length_, clockArea_.withTrimmedLeft(juce::roundToInt(clockW) + theme::px(8)),
                   juce::Justification::centredLeft, false);

        if (!metaArea_.isEmpty()) {
            auto meta = metaArea_.toFloat();
            const float lineH = meta.getHeight() * 0.36f;
            auto top = meta.withTrimmedBottom(meta.getHeight() * 0.5f);
            auto bottom = meta.withTrimmedTop(meta.getHeight() * 0.5f);
            g.setColour(p.t1);
            g.setFont(theme::text(juce::jmin(lineH, theme::pxf(14.0f)), theme::Weight::Semibold));
            g.drawFittedText(song_.isNotEmpty() ? song_ : ui::utf8("Sem setlist"), top.toNearestInt(),
                             juce::Justification::bottomRight, 1, 0.8f);
            g.setColour(p.t3);
            g.setFont(theme::numbers(juce::jmin(lineH * 0.85f, theme::pxf(12.0f))));
            g.drawFittedText(audio_, bottom.toNearestInt(), juce::Justification::topRight, 1, 0.8f);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        // Tocar na pilula alterna o modo, como o MODE do pedal.
        if (e.mouseWasClicked() && pillArea_.contains(e.getPosition())) {
            ctx_.sendPedalEvent(protocol::kButtonMode, protocol::kGesturePress);
        }
    }

private:
    AppContext& ctx_;
    LoopProgressBar progress_;
    juce::TextButton record_;
    ui::FrameMood mood_ = ui::FrameMood::Stopped;

    juce::String pill_ = "PARADO";
    bool blinkOn_ = true;
    bool recMode_ = true;
    bool playing_ = false;
    juce::String clock_;
    juce::String length_;
    juce::String song_;
    juce::String audio_;

    juce::Rectangle<int> pillArea_;
    juce::Rectangle<int> clockArea_;
    juce::Rectangle<int> metaArea_;
};

// ============================================================================

AppShell::AppShell(AppContext& context) : ctx_(context) {
    rail_ = std::make_unique<NavRail>(*this);
    hud_ = std::make_unique<StatusHud>(context);
    addAndMakeVisible(*rail_);
    addAndMakeVisible(*hud_);
}

AppShell::~AppShell() = default;

void AppShell::addPage(const juce::String& id, const juce::String& label, ui::Icon icon, std::unique_ptr<Page> page) {
    addChildComponent(*page);
    pages_.push_back({id, label, icon, std::move(page)});
    rail_->repaint();
}

void AppShell::showPage(const juce::String& id) {
    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i].id == id) {
            showPageIndex(static_cast<int>(i));
            return;
        }
    }
    if (!pages_.empty()) {
        showPageIndex(0);
    }
}

void AppShell::showPageIndex(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) {
        return;
    }
    const auto& target = pages_[static_cast<size_t>(index)];
    const bool changed = target.id != current_;
    current_ = target.id;
    for (auto& entry : pages_) {
        entry.page->setVisible(entry.id == current_);
    }
    target.page->setBounds(pageArea_);
    target.page->pageShown();
    rail_->repaint();
    if (changed && onPageChanged) {
        onPageChanged(current_);
    }
}

void AppShell::refresh() {
    hud_->refresh();
    rail_->setPedalConnected(ctx_.pedalConnected());

    LooperEngine& engine = ctx_.engine();
    const ui::FrameMood mood = ui::moodFor(engine.mode() == GlobalMode::REC_MODE, engine.transportPlaying());
    if (mood != mood_) {
        mood_ = mood;
        repaint();
    }
    for (auto& entry : pages_) {
        if (entry.page->isVisible()) {
            entry.page->refresh();
        }
    }
}

void AppShell::refreshColours() {
    resized();
    for (auto& entry : pages_) {
        entry.page->refreshColours();
    }
    repaint();
    rail_->repaint();
    hud_->repaint();
}

void AppShell::resized() {
    const int w = getWidth();
    const int h = getHeight();
    // Janela estreita ou em pe (celular): barra de abas embaixo.
    compact_ = w < 720 || w < h * 3 / 4;
    theme::setScale(compact_ ? juce::jlimit(0.85f, 1.35f, static_cast<float>(w) / 430.0f)
                             : juce::jlimit(0.85f, 1.9f, static_cast<float>(w) / 1040.0f));

    auto area = getLocalBounds();
    rail_->horizontal = compact_;
    hud_->compact = compact_;
    if (compact_) {
        rail_->setBounds(area.removeFromBottom(theme::px(68)));
        hud_->setBounds(area.removeFromTop(theme::px(96)));
        pageArea_ = area.reduced(theme::px(14), theme::px(12));
    } else {
        rail_->setBounds(area.removeFromLeft(theme::px(96)));
        hud_->setBounds(area.removeFromTop(theme::px(70)));
        pageArea_ = area.reduced(theme::px(24), theme::px(20));
    }
    for (auto& entry : pages_) {
        if (entry.page->isVisible()) {
            entry.page->setBounds(pageArea_);
        }
    }
    rail_->repaint();
}

void AppShell::paint(juce::Graphics& g) {
    g.fillAll(theme::palette().bg);
}

void AppShell::paintOverChildren(juce::Graphics& g) {
    // Moldura de modo fina por cima de tudo: vermelha gravando, verde tocando,
    // cinza parado - da para saber o modo pelo canto do olho.
    ui::paintModeFrame(g, getLocalBounds(), mood_);
}
