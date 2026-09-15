#include "Widgets.h"

#include <cmath>

#include <juce_gui_extra/juce_gui_extra.h>

#include "PedalLookAndFeel.h"
#include "UiText.h"

namespace ui {

// --- Estado -----------------------------------------------------------------

juce::String stateText(TrackState state) {
    switch (state) {
        case TrackState::EMPTY: return "VAZIA";
        case TrackState::RECORDING: return "GRAVANDO";
        case TrackState::PLAYING: return "TOCANDO";
        case TrackState::MUTED: return "MUTADA";
    }
    return {};
}

juce::Colour stateColour(TrackState state) {
    const auto& p = theme::palette();
    switch (state) {
        case TrackState::EMPTY: return p.t3;
        case TrackState::RECORDING: return p.rec;
        case TrackState::PLAYING: return p.play;
        case TrackState::MUTED: return p.t2;
    }
    return p.t3;
}

juce::String layersText(int layers) {
    if (layers <= 0) {
        return "sem camadas";
    }
    return layers == 1 ? juce::String("1 camada") : juce::String(layers) + " camadas";
}

float paintStateChip(juce::Graphics& g, juce::Rectangle<float> area, TrackState state) {
    const auto& p = theme::palette();
    const juce::String label = stateText(state);
    const juce::Font font = theme::caps(area.getHeight() * 0.5f);
    const float width = juce::GlyphArrangement::getStringWidth(font, label) + area.getHeight() * 0.9f;
    const auto chip = area.withWidth(juce::jmin(width, area.getWidth()));
    const float radius = area.getHeight() * 0.28f;

    juce::Colour fill = juce::Colours::transparentBlack;
    juce::Colour text = p.t3;
    switch (state) {
        case TrackState::RECORDING: fill = p.rec; text = juce::Colours::white; break;
        case TrackState::PLAYING: fill = p.play.withAlpha(0.16f); text = p.play; break;
        case TrackState::MUTED: fill = p.s3; text = p.t2; break;
        case TrackState::EMPTY: break;
    }
    if (fill.isTransparent()) {
        g.setColour(p.line2);
        g.drawRoundedRectangle(chip.reduced(0.5f), radius, 1.0f);
    } else {
        g.setColour(fill);
        g.fillRoundedRectangle(chip, radius);
    }
    g.setColour(text);
    g.setFont(font);
    g.drawText(label, chip, juce::Justification::centred, false);
    return chip.getWidth();
}

// --- Anel do loop -----------------------------------------------------------

void paintLoopRings(juce::Graphics& g, juce::Rectangle<float> area, const RingState& ring) {
    const auto& p = theme::palette();
    const float size = juce::jmin(area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const float thickness = size * 0.052f;
    const float step = thickness * 1.45f;
    const float outer = size * 0.5f - thickness;
    const float angle = static_cast<float>(ring.progress) * juce::MathConstants<float>::twoPi;

    for (int i = 0; i < config::kNumTracks; ++i) {
        const float r = outer - step * static_cast<float>(i);
        g.setColour(p.s3);
        g.drawEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, thickness);

        const TrackState state = ring.states[static_cast<size_t>(i)];
        if (state == TrackState::EMPTY) {
            continue;
        }
        juce::Colour colour = theme::track(i);
        if (state == TrackState::MUTED) {
            colour = colour.withAlpha(0.28f);
        }
        juce::Path arc;
        if (state == TrackState::RECORDING && ring.defined) {
            arc.addCentredArc(centre.x, centre.y, r, r, 0.0f, 0.0f, juce::jmax(0.001f, angle), true);
        } else {
            arc.addEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        }
        g.setColour(colour);
        g.strokePath(arc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float inner = outer - step * static_cast<float>(config::kNumTracks - 1) - thickness * 1.3f;
    if (ring.defined) {
        const float from = inner + thickness * 0.2f;
        const float to = outer + thickness * 1.2f;
        const float sx = std::sin(angle);
        const float cy = -std::cos(angle);
        g.setColour(p.t1);
        g.drawLine(centre.x + sx * from, centre.y + cy * from, centre.x + sx * to, centre.y + cy * to,
                   juce::jmax(1.5f, thickness * 0.22f));
        const float dot = thickness * 0.42f;
        g.fillEllipse(centre.x + sx * to - dot, centre.y + cy * to - dot, dot * 2.0f, dot * 2.0f);
    }

    // Miolo: o tamanho do loop, em segundos.
    g.setColour(p.s1);
    g.fillEllipse(centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
    const auto textArea = juce::Rectangle<float>(inner * 2.0f, inner * 1.2f).withCentre(centre);
    g.setColour(p.t1);
    g.setFont(theme::numbers(inner * 0.5f, true));
    g.drawText(ring.defined ? juce::String(ring.lengthSeconds, 1) : juce::String("--"),
               textArea.withTrimmedBottom(textArea.getHeight() * 0.35f), juce::Justification::centredBottom, false);
    g.setColour(p.t3);
    g.setFont(theme::caps(inner * 0.2f));
    g.drawText(ring.defined ? "SEGUNDOS" : "SEM LOOP", textArea.withTrimmedTop(textArea.getHeight() * 0.68f),
               juce::Justification::centredTop, false);
}

// --- Controle segmentado ------------------------------------------------------

void SegmentedControl::setOptions(const juce::StringArray& options) {
    options_ = options;
    selected_ = juce::jlimit(0, juce::jmax(0, options_.size() - 1), selected_);
    repaint();
}

void SegmentedControl::setSelected(int index, bool notify) {
    const int clamped = juce::jlimit(0, juce::jmax(0, options_.size() - 1), index);
    if (clamped == selected_) {
        return;
    }
    selected_ = clamped;
    repaint();
    if (notify && onChange) {
        onChange(selected_);
    }
}

juce::Rectangle<float> SegmentedControl::segment(int index) const {
    auto r = getLocalBounds().toFloat().reduced(3.0f);
    const float w = r.getWidth() / static_cast<float>(juce::jmax(1, options_.size()));
    return {r.getX() + w * static_cast<float>(index), r.getY(), w, r.getHeight()};
}

void SegmentedControl::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    auto r = getLocalBounds().toFloat();
    const float radius = juce::jlimit(6.0f, 12.0f, r.getHeight() * 0.28f);
    g.setColour(p.s2);
    g.fillRoundedRectangle(r, radius);
    g.setColour(p.line);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);

    const juce::Font font = theme::text(juce::jlimit(11.0f, 20.0f, r.getHeight() * 0.42f), theme::Weight::Semibold);
    for (int i = 0; i < options_.size(); ++i) {
        const auto seg = segment(i);
        if (i == selected_) {
            g.setColour(juce::Colours::black.withAlpha(theme::isDark() ? 0.35f : 0.1f));
            g.fillRoundedRectangle(seg.translated(0.0f, 1.0f), radius - 2.0f);
            g.setColour(p.s1);
            g.fillRoundedRectangle(seg, radius - 2.0f);
        }
        g.setColour(i == selected_ ? p.t1 : p.t2);
        g.setFont(font);
        g.drawFittedText(options_[i], seg.toNearestInt().reduced(4, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void SegmentedControl::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < options_.size(); ++i) {
        if (segment(i).contains(e.position)) {
            setSelected(i, true);
            return;
        }
    }
}

// --- Cartao de track ----------------------------------------------------------

TrackCard::TrackCard(int index) : index_(index) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TrackCard::setData(const Data& data) {
    const bool onlyLevel = data.name == data_.name && data.state == data_.state && data.layers == data_.layers &&
                           data.input == data_.input && data.selected == data_.selected;
    const bool levelChanged = std::abs(data.level - data_.level) > 0.002f;
    data_ = data;
    if (!onlyLevel) {
        repaint();
    } else if (levelChanged) {
        repaint(meterArea().getSmallestIntegerContainer().expanded(2));
    }
}

juce::Rectangle<float> TrackCard::meterArea() const {
    const float pad = theme::pxf(15.0f);
    auto r = getLocalBounds().toFloat().reduced(pad);
    return r.removeFromBottom(juce::jmax(4.0f, theme::pxf(7.0f)));
}

void TrackCard::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    const float radius = juce::jlimit(10.0f, 20.0f, theme::pxf(14.0f));
    theme::paintCard(g, bounds, radius);
    if (data_.selected) {
        theme::paintSelection(g, bounds, radius, p.rec);
    }

    const float pad = theme::pxf(15.0f);
    auto r = bounds.reduced(pad - 2.0f);
    const float nameH = juce::jlimit(14.0f, 30.0f, theme::pxf(19.0f));

    // Linha 1: ponto de cor, nome, numero.
    auto top = r.removeFromTop(nameH * 1.3f);
    const float dot = nameH * 0.62f;
    g.setColour(theme::track(index_));
    g.fillEllipse(top.getX(), top.getCentreY() - dot * 0.5f, dot, dot);
    g.setColour(p.t3);
    g.setFont(theme::numbers(nameH * 0.7f));
    g.drawText(juce::String(index_ + 1), top, juce::Justification::centredRight, false);
    g.setColour(p.t1);
    g.setFont(theme::text(nameH, theme::Weight::Semibold));
    g.drawFittedText(data_.name, top.withTrimmedLeft(dot * 1.6f).withTrimmedRight(nameH).toNearestInt(),
                     juce::Justification::centredLeft, 1, 0.85f);

    // Linha 2: estado + camadas.
    r.removeFromTop(theme::pxf(10.0f));
    auto row = r.removeFromTop(nameH * 1.15f);
    paintStateChip(g, row, data_.state);
    g.setColour(p.t2);
    g.setFont(theme::text(nameH * 0.72f, theme::Weight::Semibold));
    g.drawText(layersText(data_.layers), row, juce::Justification::centredRight, false);

    // Linha 3: entrada + selecao.
    r.removeFromTop(theme::pxf(8.0f));
    row = r.removeFromTop(nameH * 1.0f);
    g.setColour(p.t2);
    g.setFont(theme::text(nameH * 0.7f));
    g.drawText("Entrada: " + data_.input, row, juce::Justification::centredLeft, false);
    if (data_.selected) {
        g.setColour(p.rec);
        g.setFont(theme::text(nameH * 0.7f, theme::Weight::Semibold));
        g.drawText("Selecionada", row, juce::Justification::centredRight, false);
    }

    // Medidor: barra continua na cor da track (cinza quando mutada).
    const auto meter = meterArea();
    theme::paintGroove(g, meter, meter.getHeight() * 0.5f);
    const float level = juce::jlimit(0.0f, 1.0f, data_.level);
    if (level > 0.001f) {
        g.setColour(data_.state == TrackState::MUTED ? p.t3 : theme::track(index_));
        g.fillRoundedRectangle(meter.withWidth(juce::jmax(meter.getHeight(), meter.getWidth() * level)),
                               meter.getHeight() * 0.5f);
    }
}

void TrackCard::mouseUp(const juce::MouseEvent& e) {
    if (e.mouseWasClicked() && onClick) {
        onClick();
    }
}

// --- Seletor de cor -------------------------------------------------------------

namespace {

class CustomColourSelector : public juce::ColourSelector, private juce::ChangeListener {
public:
    CustomColourSelector(juce::Colour initial, std::function<void(juce::Colour)> onChange)
        : juce::ColourSelector(juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders |
                               juce::ColourSelector::showColourAtTop),
          onChange_(std::move(onChange)) {
        setCurrentColour(initial, juce::dontSendNotification);
        addChangeListener(this);
        setSize(theme::px(280), theme::px(260));
    }
    ~CustomColourSelector() override { removeChangeListener(this); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        if (onChange_) {
            onChange_(getCurrentColour().withAlpha(1.0f));
        }
    }
    std::function<void(juce::Colour)> onChange_;
};

class TrackColourPicker : public juce::Component {
public:
    TrackColourPicker(int track, std::function<void(juce::Colour)> onPicked)
        : track_(track), onPicked_(std::move(onPicked)) {
        custom_.setButtonText(ui::utf8("Outra cor…"));
        custom_.onClick = [this] {
            juce::Component::SafePointer<TrackColourPicker> safe(this);
            auto selector = std::make_unique<CustomColourSelector>(theme::track(track_), [safe](juce::Colour c) {
                if (safe != nullptr) {
                    safe->pick(c);
                }
            });
            juce::CallOutBox::launchAsynchronously(std::move(selector), custom_.getScreenBounds(), nullptr);
        };
        addAndMakeVisible(custom_);

        reset_.setButtonText(ui::utf8("Voltar ao padrão"));
        reset_.onClick = [this] { pick(juce::Colours::transparentBlack); };
        addAndMakeVisible(reset_);

        warning_.setJustificationType(juce::Justification::topLeft);
        warning_.setColour(juce::Label::textColourId, theme::palette().amber);
        warning_.setFont(theme::text(theme::pxf(13.0f)));
        addAndMakeVisible(warning_);

        refreshWarning();
        setSize(theme::px(300), theme::px(268));
    }

    void paint(juce::Graphics& g) override {
        const auto& p = theme::palette();
        g.setColour(p.t1);
        g.setFont(theme::text(theme::pxf(16.0f), theme::Weight::Semibold));
        g.drawText("Cor da track " + juce::String(track_ + 1), titleArea(), juce::Justification::centredLeft, false);

        const juce::Colour current = theme::track(track_);
        const auto& swatches = theme::trackSwatches();
        for (size_t i = 0; i < swatches.size(); ++i) {
            const auto r = swatchArea(static_cast<int>(i));
            const juce::Colour c(swatches[i].argb);
            if (c == current) {
                g.setColour(p.t1);
                g.drawEllipse(r.expanded(3.0f), 2.0f);
            }
            g.setColour(c);
            g.fillEllipse(r);
        }
    }

    void resized() override {
        auto r = getLocalBounds().reduced(theme::px(14));
        r.removeFromTop(titleArea().getHeight() + theme::px(8));
        r.removeFromTop(swatchBlockHeight() + theme::px(10));
        const int rowH = theme::px(32);
        auto buttons = r.removeFromBottom(rowH);
        custom_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - theme::px(4)));
        buttons.removeFromLeft(theme::px(8));
        reset_.setBounds(buttons);
        r.removeFromBottom(theme::px(6));
        warning_.setBounds(r);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        const auto& swatches = theme::trackSwatches();
        for (size_t i = 0; i < swatches.size(); ++i) {
            if (swatchArea(static_cast<int>(i)).expanded(3.0f).contains(e.position)) {
                pick(juce::Colour(swatches[i].argb));
                return;
            }
        }
    }

    void pick(juce::Colour colour) {
        if (onPicked_) {
            onPicked_(colour);
        }
        refreshWarning();
        repaint();
    }

private:
    juce::Rectangle<int> titleArea() const {
        return getLocalBounds().reduced(theme::px(14)).removeFromTop(theme::px(24));
    }
    int swatchBlockHeight() const { return theme::px(34) * 2 + theme::px(10); }

    juce::Rectangle<float> swatchArea(int index) const {
        auto r = getLocalBounds().reduced(theme::px(14));
        r.removeFromTop(titleArea().getHeight() + theme::px(8));
        const float cell = static_cast<float>(r.getWidth()) / 6.0f;
        const float d = juce::jmin(cell - 8.0f, static_cast<float>(theme::px(34)));
        const int col = index % 6;
        const int row = index / 6;
        const float cx = static_cast<float>(r.getX()) + cell * (static_cast<float>(col) + 0.5f);
        const float cy = static_cast<float>(r.getY()) + (static_cast<float>(theme::px(34)) + theme::pxf(10.0f)) *
                                                            static_cast<float>(row) + static_cast<float>(theme::px(34)) * 0.5f;
        return {cx - d * 0.5f, cy - d * 0.5f, d, d};
    }

    void refreshWarning() {
        warning_.setText(theme::trackColourWarning(theme::track(track_)), juce::dontSendNotification);
    }

    int track_;
    std::function<void(juce::Colour)> onPicked_;
    juce::TextButton custom_;
    juce::TextButton reset_;
    juce::Label warning_;
};

} // namespace

void showTrackColourPicker(juce::Component& anchor, int track, std::function<void(juce::Colour)> onPicked) {
    juce::CallOutBox::launchAsynchronously(std::make_unique<TrackColourPicker>(track, std::move(onPicked)),
                                           anchor.getScreenBounds(), nullptr);
}

// --- Dialogos ----------------------------------------------------------------

void confirm(const juce::String& title, const juce::String& message, const juce::String& confirmText,
             std::function<void()> onConfirm, bool destructive) {
    auto* window = new juce::AlertWindow(title, message, juce::MessageBoxIconType::NoIcon);
    window->addButton(ui::utf8("Cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->addButton(confirmText, 1, juce::KeyPress(juce::KeyPress::returnKey));
    if (auto* button = window->getButton(confirmText)) {
        ui::setButtonStyle(*button, destructive ? "danger" : "primary");
    }
    window->enterModalState(true, juce::ModalCallbackFunction::create([onConfirm](int result) {
                                if (result == 1 && onConfirm) {
                                    onConfirm();
                                }
                            }),
                            true);
}

void askText(const juce::String& title, const juce::String& message, const juce::String& initial,
             const juce::String& okText, std::function<void(juce::String)> onOk) {
    auto window = std::make_shared<juce::AlertWindow>(title, message, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("text", initial);
    window->addButton(ui::utf8("Cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->addButton(okText, 1, juce::KeyPress(juce::KeyPress::returnKey));
    if (auto* button = window->getButton(okText)) {
        ui::setButtonStyle(*button, "primary");
    }
    // O callback guarda o proprio dialogo (shared_ptr) para ler o texto depois
    // do clique; quando o callback e descartado, o dialogo vai junto.
    window->enterModalState(true, juce::ModalCallbackFunction::create([window, onOk](int result) {
                                const juce::String text = window->getTextEditorContents("text").trim();
                                window->setVisible(false);
                                if (result == 1 && onOk) {
                                    onOk(text);
                                }
                            }),
                            false);
}

void notify(const juce::String& title, const juce::String& message) {
    auto* window = new juce::AlertWindow(title, message, juce::MessageBoxIconType::NoIcon);
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->enterModalState(true, nullptr, true);
}

// --- Cabecalho ---------------------------------------------------------------

void paintPageTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                    const juce::String& subtitle) {
    const auto& p = theme::palette();
    auto r = area.toFloat();
    const float titleH = juce::jlimit(18.0f, 40.0f, theme::pxf(26.0f));
    g.setColour(p.t1);
    g.setFont(theme::text(titleH, theme::Weight::Semibold));
    g.drawText(title, r.removeFromTop(titleH * 1.25f), juce::Justification::centredLeft, false);
    if (subtitle.isNotEmpty()) {
        g.setColour(p.t3);
        g.setFont(theme::text(titleH * 0.52f));
        g.drawFittedText(subtitle, r.removeFromTop(titleH * 0.8f).toNearestInt(), juce::Justification::centredLeft, 1,
                         0.9f);
    }
}

} // namespace ui
