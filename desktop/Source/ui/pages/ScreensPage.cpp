#include "ui/Pages.h"

#include "UiText.h"

// Desenho dos monitores conectados, na posicao em que o Windows os arruma.
class ScreensPage::MonitorMap : public juce::Component {
public:
    explicit MonitorMap(ScreensPage& owner) : owner_(owner) {}

    std::vector<juce::Rectangle<float>> layout() const {
        std::vector<juce::Rectangle<float>> result;
        const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
        if (displays.isEmpty()) {
            return result;
        }
        juce::Rectangle<int> total = displays[0].logicalBounds.toNearestInt();
        for (const auto& d : displays) {
            total = total.getUnion(d.logicalBounds.toNearestInt());
        }
        const auto area = getLocalBounds().toFloat().reduced(theme::pxf(24.0f));
        const float s = juce::jmin(area.getWidth() / static_cast<float>(total.getWidth()),
                                   area.getHeight() / static_cast<float>(total.getHeight()));
        const float offX = area.getX() + (area.getWidth() - static_cast<float>(total.getWidth()) * s) * 0.5f;
        const float offY = area.getY() + (area.getHeight() - static_cast<float>(total.getHeight()) * s) * 0.5f;
        for (const auto& d : displays) {
            const auto r = d.logicalBounds.toNearestInt();
            result.push_back(juce::Rectangle<float>(offX + static_cast<float>(r.getX() - total.getX()) * s,
                                                    offY + static_cast<float>(r.getY() - total.getY()) * s,
                                                    static_cast<float>(r.getWidth()) * s,
                                                    static_cast<float>(r.getHeight()) * s)
                                 .reduced(theme::pxf(6.0f)));
        }
        return result;
    }

    void paint(juce::Graphics& g) override {
        const auto& p = theme::palette();
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(p.s2);
        g.fillRoundedRectangle(bounds, theme::pxf(16.0f));
        g.setColour(p.line2);
        const float dashes[] = {6.0f, 5.0f};
        juce::Path outline;
        outline.addRoundedRectangle(bounds, theme::pxf(16.0f));
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);

        const auto rects = layout();
        const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
        const int controls = owner_.ctx_.controlsDisplay();
        const bool metersOn = owner_.ctx_.metersVisible();
        const int meters = owner_.ctx_.metersDisplay();
        for (size_t i = 0; i < rects.size(); ++i) {
            const auto r = rects[i];
            const bool selected = static_cast<int>(i) == owner_.selected_;
            g.setColour(p.s1);
            g.fillRoundedRectangle(r, theme::pxf(10.0f));
            g.setColour(selected ? p.t1 : p.line2);
            g.drawRoundedRectangle(r, theme::pxf(10.0f), selected ? 2.5f : 1.5f);

            juce::StringArray roles;
            if (static_cast<int>(i) == controls) {
                roles.add("Controles");
            }
            const bool hasMeters = metersOn && static_cast<int>(i) == meters;
            if (hasMeters) {
                roles.add("Medidores");
            }
            auto text = r.reduced(theme::pxf(8.0f));
            g.setColour(p.t3);
            g.setFont(theme::text(juce::jmin(theme::pxf(13.0f), r.getHeight() * 0.14f), theme::Weight::Semibold));
            g.drawFittedText("Monitor " + juce::String(static_cast<int>(i) + 1) +
                                 (displays[static_cast<int>(i)].isMain ? ui::utf8("  ·  principal") : juce::String()),
                             text.removeFromTop(text.getHeight() * 0.3f).toNearestInt(), juce::Justification::centredBottom,
                             1, 0.8f);
            g.setColour(p.t1);
            g.setFont(theme::text(juce::jmin(theme::pxf(16.0f), r.getHeight() * 0.17f), theme::Weight::Semibold));
            g.drawFittedText(roles.isEmpty() ? ui::utf8("—") : roles.joinIntoString(" + "),
                             text.removeFromTop(text.getHeight() * 0.35f).toNearestInt(), juce::Justification::centred, 1,
                             0.8f);
            if (hasMeters) {
                // Miniatura: as 4 barras verdes da tela de performance.
                auto thumb = text.withSizeKeepingCentre(juce::jmin(text.getWidth() * 0.7f, theme::pxf(120.0f)),
                                                        juce::jmin(text.getHeight() * 0.7f, theme::pxf(40.0f)));
                const float w = thumb.getWidth() / 4.0f;
                const float heights[] = {0.6f, 0.85f, 0.4f, 0.7f};
                for (int b = 0; b < 4; ++b) {
                    auto bar = juce::Rectangle<float>(thumb.getX() + w * static_cast<float>(b) + 2.0f, thumb.getY(),
                                                      w - 4.0f, thumb.getHeight());
                    g.setColour(p.s3);
                    g.fillRoundedRectangle(bar, 3.0f);
                    g.setColour(p.play);
                    g.fillRoundedRectangle(bar.withTop(bar.getBottom() - bar.getHeight() * heights[b]), 3.0f);
                }
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        const auto rects = layout();
        for (size_t i = 0; i < rects.size(); ++i) {
            if (rects[i].contains(e.position)) {
                owner_.selected_ = static_cast<int>(i);
                owner_.updateAssign();
                repaint();
                return;
            }
        }
    }

private:
    ScreensPage& owner_;
};

ScreensPage::ScreensPage(AppContext& context) : Page(context) {
    map_ = std::make_unique<MonitorMap>(*this);
    addAndMakeVisible(*map_);

    role_.setOptions({"Nada", "Medidores"});
    role_.onChange = [this](int index) {
        if (index == 1) {
            ctx_.setMetersDisplay(selected_);
            ctx_.setMetersVisible(true);
        } else if (ctx_.metersDisplay() == selected_) {
            ctx_.setMetersVisible(false);
        }
        updateAssign();
    };
    addAndMakeVisible(role_);

    ui::setButtonStyle(metersToggle_, "primary");
    ui::setButtonIcon(metersToggle_, "screens");
    metersToggle_.setWantsKeyboardFocus(false);
    metersToggle_.onClick = [this] {
        ctx_.setMetersVisible(!ctx_.metersVisible());
        updateAssign();
    };
    addAndMakeVisible(metersToggle_);
}

ScreensPage::~ScreensPage() = default;

void ScreensPage::pageShown() {
    const int count = juce::Desktop::getInstance().getDisplays().displays.size();
    selected_ = juce::jlimit(0, juce::jmax(0, count - 1), ctx_.metersDisplay());
    updateAssign();
}

void ScreensPage::refresh() {
    // A tela de performance pode ter sido escondida com ESC.
    const bool wantMeters = ctx_.metersVisible() && ctx_.metersDisplay() == selected_;
    const juce::String toggleText = ctx_.metersVisible() ? ui::utf8("Esconder a tela de performance")
                                                         : ui::utf8("Mostrar a tela de performance");
    if (role_.selected() != (wantMeters ? 1 : 0) || metersToggle_.getButtonText() != toggleText) {
        updateAssign();
    }
}

void ScreensPage::updateAssign() {
    const bool meters = ctx_.metersVisible() && ctx_.metersDisplay() == selected_;
    role_.setSelected(meters ? 1 : 0);
    metersToggle_.setButtonText(ctx_.metersVisible() ? ui::utf8("Esconder a tela de performance")
                                                     : ui::utf8("Mostrar a tela de performance"));
    map_->repaint();
    repaint(assignCard_);
}

void ScreensPage::resized() {
    auto area = getLocalBounds();
    header_ = takeHeader(area);
    const bool compact = getWidth() < theme::px(700);

    assignCard_ = area.removeFromBottom(theme::px(compact ? 190.0f : 110.0f));
    area.removeFromBottom(theme::px(16));
    map_->setBounds(area);

    auto card = assignCard_.reduced(theme::px(20), theme::px(16));
    if (compact) {
        metersToggle_.setBounds(card.removeFromBottom(theme::px(42)));
        card.removeFromBottom(theme::px(10));
        role_.setBounds(card.removeFromBottom(theme::px(40)));
        card.removeFromBottom(theme::px(8));
        assignText_ = card;
    } else {
        metersToggle_.setBounds(card.removeFromRight(theme::px(300)).withSizeKeepingCentre(theme::px(300), theme::px(42)));
        card.removeFromRight(theme::px(14));
        role_.setBounds(card.removeFromRight(theme::px(240)).withSizeKeepingCentre(theme::px(240), theme::px(40)));
        card.removeFromRight(theme::px(14));
        assignText_ = card;
    }
}

void ScreensPage::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    const int count = juce::Desktop::getInstance().getDisplays().displays.size();
    ui::paintPageTitle(g, header_, "Telas",
                       juce::String(count) + (count == 1 ? " monitor conectado" : " monitores conectados") +
                           ui::utf8(". Toque num monitor para escolher o que ele mostra."));

    theme::paintCard(g, assignCard_.toFloat().reduced(1.0f), theme::pxf(16.0f));
    auto r = assignText_.toFloat();
    g.setColour(p.t3);
    g.setFont(theme::caps(theme::pxf(11.0f)));
    g.drawText("MONITOR " + juce::String(selected_ + 1), r.removeFromTop(r.getHeight() * 0.4f),
               juce::Justification::bottomLeft, false);
    g.setColour(p.t1);
    g.setFont(theme::text(theme::pxf(15.0f), theme::Weight::Semibold));
    const juce::String text = selected_ == ctx_.controlsDisplay()
                                  ? ui::utf8("Aqui fica esta janela (controles).")
                                  : ui::utf8("Tela de performance: tela cheia, sem barra de título  ·  ESC esconde");
    g.drawFittedText(text, r.toNearestInt(), juce::Justification::topLeft, 2, 0.85f);
}
