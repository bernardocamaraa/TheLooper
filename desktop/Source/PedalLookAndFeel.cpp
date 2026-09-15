#include "PedalLookAndFeel.h"

#include <cmath>

#include "ui/Icons.h"

namespace theme {

namespace {

// Tokens dos mockups aprovados (escuro e claro). O escuro e o do palco; o
// claro serve para estudio iluminado e dia.
const Palette kDark{
    juce::Colour(0xff0b0c0f), juce::Colour(0xff15171c), juce::Colour(0xff1c1f25), juce::Colour(0xff262a32),
    juce::Colour(0xff262a31), juce::Colour(0xff363b45), juce::Colour(0xfff5f6f8), juce::Colour(0xffa7adb8),
    juce::Colour(0xff767d89), juce::Colour(0xffff3b30), juce::Colour(0xff30d158), juce::Colour(0xffffb020),
};
const Palette kLight{
    juce::Colour(0xfff3f4f7), juce::Colour(0xffffffff), juce::Colour(0xfff0f1f5), juce::Colour(0xffe5e8ee),
    juce::Colour(0xffdde1e8), juce::Colour(0xffc9ced8), juce::Colour(0xff101217), juce::Colour(0xff4c5361),
    juce::Colour(0xff737a88), juce::Colour(0xffe5261b), juce::Colour(0xff0fa958), juce::Colour(0xffc98200),
};

// [tema escuro/claro][paleta][track]. Cada paleta tem uma versao por tema:
// a mesma cor viva que brilha no preto fica gritante demais no branco.
constexpr juce::uint32 kPresets[2][kTrackPresetCount][4] = {
    {
        {0xff4c8dff, 0xffa06cff, 0xff22c3d6, 0xfff2609e}, // Vivas
        {0xff5e8bff, 0xffffcf4a, 0xff2ed3b7, 0xffc77dff}, // Contraste
        {0xff7aa2c9, 0xffb79ad1, 0xff8fc3b8, 0xffd9a3a3}, // Suaves
    },
    {
        {0xff3f7ff5, 0xff9360f0, 0xff119fb3, 0xffe0508e},
        {0xff3d6cf0, 0xffd99a00, 0xff12a88f, 0xffa24fe0},
        {0xff5c86ad, 0xff8f72ad, 0xff4f9a8c, 0xffb77777},
    },
};

Mode gMode = Mode::Dark;
bool gDark = true;
Palette gPalette = kDark;
int gPreset = 0;
std::array<juce::Colour, 4> gCustom{};
float gScale = 1.0f;
int gSizeLevel = 1;

void applyAliases() {
    enclosure = gPalette.bg;
    panel = gPalette.s1;
    panelRaised = gPalette.s2;
    groove = gPalette.s3;
    hairline = gPalette.line2;
    ink = gPalette.t1;
    inkDim = gPalette.t2;
    inkFaint = gPalette.t3;
    ledRed = gPalette.rec;
    ledGreen = gPalette.play;
    amber = gPalette.amber;
}

// Primeira familia instalada da lista. Resolvido uma vez e guardado: varrer
// as fontes a cada repintura seria caro.
juce::String pickTypeface(const juce::StringArray& preferences) {
    const auto available = juce::Font::findAllTypefaceNames();
    for (const auto& name : preferences) {
        if (available.contains(name)) {
            return name;
        }
    }
    return juce::Font::getDefaultSansSerifFontName();
}

const juce::String& uiFace() {
    static const juce::String face = pickTypeface({"Segoe UI Variable Display", "Segoe UI", "Arial"});
    return face;
}

const juce::String& monoFace() {
    static const juce::String face = pickTypeface({"Cascadia Mono", "Consolas"});
    return face;
}

const char* styleName(Weight weight) {
    switch (weight) {
        case Weight::Light: return "Light";
        case Weight::Regular: return "Regular";
        case Weight::Semibold: return "Semibold";
        case Weight::Bold: return "Bold";
    }
    return "Regular";
}

} // namespace

void setMode(Mode mode) {
    gMode = mode;
    gDark = (mode == Mode::Dark) ||
            (mode == Mode::System && juce::Desktop::getInstance().isDarkModeActive());
    gPalette = gDark ? kDark : kLight;
    applyAliases();
}

Mode mode() { return gMode; }
bool isDark() { return gDark; }
const Palette& palette() { return gPalette; }

juce::String trackPresetName(int preset) {
    switch (preset) {
        case 0: return "Vivas";
        case 1: return "Contraste";
        case 2: return "Suaves";
        default: return {};
    }
}

void setTrackPreset(int preset) { gPreset = juce::jlimit(0, kTrackPresetCount - 1, preset); }
int trackPreset() { return gPreset; }

juce::Colour presetTrackColour(int preset, int track) {
    return juce::Colour(kPresets[gDark ? 0 : 1][juce::jlimit(0, kTrackPresetCount - 1, preset)]
                                [juce::jlimit(0, 3, track)]);
}

juce::Colour track(int index) {
    const int i = juce::jlimit(0, 3, index);
    const juce::Colour custom = gCustom[static_cast<size_t>(i)];
    return custom.isTransparent() ? presetTrackColour(gPreset, i) : custom;
}

void setTrackColour(int index, juce::Colour colour) {
    gCustom[static_cast<size_t>(juce::jlimit(0, 3, index))] = colour;
}

juce::Colour customTrackColour(int index) { return gCustom[static_cast<size_t>(juce::jlimit(0, 3, index))]; }

const std::array<Swatch, 12>& trackSwatches() {
    // Nenhuma e vermelha nem verde: essas sao do estado (gravando / tocando).
    static const std::array<Swatch, 12> swatches{{
        {"Azul", 0xff4c8dff},     {"Anil", 0xff6a5cff},     {"Violeta", 0xffa06cff},
        {"Magenta", 0xffd65bd8},  {"Rosa", 0xfff2609e},     {"Laranja", 0xffff8a3d},
        {"Areia", 0xffd9b36c},    {"Turquesa", 0xff22c3d6}, {"C\xc3\xa9u", 0xff6cc6ff},
        {"Petr\xc3\xb3leo", 0xff2e9aa8}, {"Lavanda", 0xffb79ad1}, {"Grafite", 0xff8a93a3},
    }};
    return swatches;
}

juce::String trackColourWarning(juce::Colour colour) {
    if (colour.getSaturation() < 0.35f) {
        return {};
    }
    const float hue = colour.getHue() * 360.0f;
    if (hue < 22.0f || hue > 340.0f) {
        return juce::String(juce::CharPointer_UTF8(
            "Parecida com o vermelho de GRAVANDO. No palco isso pode confundir."));
    }
    if (hue > 95.0f && hue < 165.0f) {
        return juce::String(juce::CharPointer_UTF8(
            "Parecida com o verde de TOCANDO. No palco isso pode confundir."));
    }
    return {};
}

// --- Tipografia -------------------------------------------------------------

juce::Font text(float height, Weight weight) {
    return juce::Font(juce::FontOptions(uiFace(), styleName(weight), height));
}

juce::Font caps(float height) {
    return text(height, Weight::Semibold).withExtraKerningFactor(0.09f);
}

juce::Font numbers(float height, bool bold) {
    return juce::Font(juce::FontOptions(monoFace(), bold ? "Bold" : "Regular", height));
}

juce::Font legend(float height, bool strong) {
    return text(height, strong ? Weight::Semibold : Weight::Regular).withExtraKerningFactor(0.03f);
}

juce::Font display(float height) {
    return text(height, Weight::Semibold);
}

juce::Font data(float height) {
    return numbers(height);
}

// --- Escala -------------------------------------------------------------------

void setScale(float value) { gScale = juce::jlimit(0.6f, 3.0f, value); }
float scale() { return gScale * sizeFactor(); }
int px(float value) { return juce::roundToInt(value * scale()); }
float pxf(float value) { return value * scale(); }
void setSizeLevel(int level) { gSizeLevel = juce::jlimit(0, 2, level); }
int sizeLevel() { return gSizeLevel; }
float sizeFactor() {
    switch (gSizeLevel) {
        case 0: return 0.9f;
        case 2: return 1.15f;
        default: return 1.0f;
    }
}

// --- Pintura ------------------------------------------------------------------

void paintCard(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, bool shadow) {
    if (shadow) {
        // Sombra curta e difusa: duas camadas bastam, e sao baratas o
        // suficiente para repintar a 30 Hz (DropShadow borraria a cada frame).
        const float strength = gDark ? 0.34f : 0.07f;
        g.setColour(juce::Colours::black.withAlpha(strength * 0.5f));
        g.fillRoundedRectangle(bounds.translated(0.0f, 3.0f).expanded(1.0f), radius + 1.0f);
        g.setColour(juce::Colours::black.withAlpha(strength));
        g.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), radius);
    }
    g.setColour(gPalette.s1);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(gPalette.line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

void paintSelection(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour colour) {
    g.setColour(colour.withAlpha(0.22f));
    g.drawRoundedRectangle(bounds.expanded(1.5f), radius + 1.5f, 4.0f);
    g.setColour(colour);
    g.drawRoundedRectangle(bounds.reduced(0.75f), radius, 1.8f);
}

void paintGroove(juce::Graphics& g, juce::Rectangle<float> bounds, float radius) {
    g.setColour(gPalette.s3);
    g.fillRoundedRectangle(bounds, radius);
}

void paintPanel(juce::Graphics& g, juce::Rectangle<int> bounds, bool) {
    paintCard(g, bounds.toFloat().reduced(1.0f), juce::jlimit(8.0f, 18.0f, pxf(14.0f)));
}

void paintSectionRule(juce::Graphics& g, juce::Rectangle<int> row, const juce::String& label) {
    if (row.isEmpty()) {
        return;
    }
    auto r = row.toFloat();
    g.setFont(caps(juce::jlimit(10.0f, 16.0f, r.getHeight() * 0.62f)));
    const float textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), label);
    g.setColour(gPalette.t3);
    g.drawText(label, row, juce::Justification::centredLeft, false);
    g.setColour(gPalette.line);
    g.drawLine(r.getX() + textWidth + 10.0f, r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);
}

} // namespace theme

// ---------------------------------------------------------------------------

namespace ui {
void setButtonStyle(juce::Button& button, const juce::String& style) {
    button.getProperties().set("style", style);
    button.repaint();
}

juce::String buttonStyle(const juce::Button& button) {
    return button.getProperties()["style"].toString();
}
} // namespace ui

namespace {
float buttonRadius(float height) {
    return juce::jlimit(6.0f, 16.0f, height * 0.26f);
}

// Texto de botao que fica legivel sobre a cor de fundo dada.
juce::Colour contrastingText(juce::Colour background) {
    return background.getPerceivedBrightness() > 0.62f ? juce::Colour(0xff0b0c0f) : juce::Colours::white;
}
} // namespace

PedalLookAndFeel::PedalLookAndFeel() {
    setDefaultSansSerifTypefaceName(theme::text(14.0f).getTypefaceName());
    refreshColours();
}

void PedalLookAndFeel::refreshColours() {
    const auto& p = theme::palette();

    setColour(juce::ResizableWindow::backgroundColourId, p.bg);
    setColour(juce::DocumentWindow::backgroundColourId, p.bg);

    setColour(juce::Label::textColourId, p.t1);
    setColour(juce::Label::textWhenEditingColourId, p.t1);
    setColour(juce::Label::backgroundWhenEditingColourId, p.s2);
    setColour(juce::Label::outlineWhenEditingColourId, p.t2);

    setColour(juce::TextEditor::backgroundColourId, p.s2);
    setColour(juce::TextEditor::textColourId, p.t1);
    setColour(juce::TextEditor::outlineColourId, p.line2);
    setColour(juce::TextEditor::focusedOutlineColourId, p.t2);
    setColour(juce::TextEditor::highlightColourId, theme::track(0).withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, p.t1);
    setColour(juce::CaretComponent::caretColourId, p.t1);

    setColour(juce::ComboBox::backgroundColourId, p.s2);
    setColour(juce::ComboBox::textColourId, p.t1);
    setColour(juce::ComboBox::arrowColourId, p.t2);
    setColour(juce::ComboBox::outlineColourId, p.line2);
    setColour(juce::ComboBox::focusedOutlineColourId, p.t2);
    setColour(juce::ComboBox::buttonColourId, p.s2);

    setColour(juce::PopupMenu::backgroundColourId, p.s1);
    setColour(juce::PopupMenu::textColourId, p.t1);
    setColour(juce::PopupMenu::headerTextColourId, p.t3);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, p.s3);
    setColour(juce::PopupMenu::highlightedTextColourId, p.t1);

    setColour(juce::TextButton::buttonColourId, p.s1);
    setColour(juce::TextButton::buttonOnColourId, p.s3);
    setColour(juce::TextButton::textColourOffId, p.t1);
    setColour(juce::TextButton::textColourOnId, p.t1);

    setColour(juce::ToggleButton::textColourId, p.t1);
    setColour(juce::ToggleButton::tickColourId, p.play);
    setColour(juce::ToggleButton::tickDisabledColourId, p.t3);

    setColour(juce::ListBox::backgroundColourId, p.s2);
    setColour(juce::ListBox::outlineColourId, p.line2);
    setColour(juce::ListBox::textColourId, p.t1);

    setColour(juce::ScrollBar::thumbColourId, p.t3);
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour(juce::Slider::textBoxTextColourId, p.t1);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, theme::track(0).withAlpha(0.35f));
    setColour(juce::Slider::thumbColourId, p.s1);
    setColour(juce::Slider::trackColourId, p.t2);
    setColour(juce::Slider::backgroundColourId, p.s3);

    setColour(juce::AlertWindow::backgroundColourId, p.s1);
    setColour(juce::AlertWindow::textColourId, p.t1);
    setColour(juce::AlertWindow::outlineColourId, p.line2);

    setColour(juce::TooltipWindow::backgroundColourId, p.s1);
    setColour(juce::TooltipWindow::textColourId, p.t1);
    setColour(juce::TooltipWindow::outlineColourId, p.line2);

    setColour(juce::GroupComponent::outlineColourId, p.line2);
    setColour(juce::GroupComponent::textColourId, p.t2);
    setColour(juce::HyperlinkButton::textColourId, theme::track(0));

    // LookAndFeel_V4 pinta varios controles pelo esquema de cores dele
    // (dialogo de audio, listas). O esquema acompanha a paleta.
    getCurrentColourScheme().setUIColour(ColourScheme::windowBackground, p.bg);
    getCurrentColourScheme().setUIColour(ColourScheme::widgetBackground, p.s2);
    getCurrentColourScheme().setUIColour(ColourScheme::menuBackground, p.s1);
    getCurrentColourScheme().setUIColour(ColourScheme::outline, p.line2);
    getCurrentColourScheme().setUIColour(ColourScheme::defaultText, p.t1);
    getCurrentColourScheme().setUIColour(ColourScheme::defaultFill, p.t2);
    getCurrentColourScheme().setUIColour(ColourScheme::highlightedText, p.t1);
    getCurrentColourScheme().setUIColour(ColourScheme::highlightedFill, p.s3);
    getCurrentColourScheme().setUIColour(ColourScheme::menuText, p.t1);
}

juce::Font PedalLookAndFeel::getLabelFont(juce::Label& label) {
    return label.getFont();
}

juce::Font PedalLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return theme::text(juce::jlimit(13.0f, 22.0f, static_cast<float>(box.getHeight()) * 0.44f));
}

juce::Font PedalLookAndFeel::getPopupMenuFont() {
    return theme::text(juce::jlimit(14.0f, 20.0f, theme::pxf(15.0f)));
}

juce::Font PedalLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return theme::text(juce::jlimit(12.0f, 22.0f, static_cast<float>(buttonHeight) * 0.38f),
                       theme::Weight::Semibold);
}

juce::Font PedalLookAndFeel::getAlertWindowTitleFont() {
    return theme::text(theme::pxf(19.0f), theme::Weight::Semibold);
}

juce::Font PedalLookAndFeel::getAlertWindowMessageFont() {
    return theme::text(theme::pxf(15.0f));
}

juce::Font PedalLookAndFeel::getAlertWindowFont() {
    return theme::text(theme::pxf(14.0f));
}

void PedalLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down) {
    const auto& p = theme::palette();
    const juce::String style = ui::buttonStyle(button);
    auto r = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = buttonRadius(r.getHeight());
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();

    if (style == "switch") {
        // Interruptor tipo iOS: trilho verde ligado, cinza desligado.
        const float h = juce::jmin(r.getHeight(), r.getWidth() * 0.6f);
        auto track = juce::Rectangle<float>(h * 1.7f, h).withCentre(r.getCentre());
        g.setColour(on ? p.play : p.s3);
        g.fillRoundedRectangle(track, h * 0.5f);
        const float knob = h - 6.0f;
        const float x = on ? track.getRight() - 3.0f - knob : track.getX() + 3.0f;
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillEllipse(x, track.getY() + 4.0f, knob, knob);
        g.setColour(juce::Colours::white);
        g.fillEllipse(x, track.getY() + 3.0f, knob, knob);
        return;
    }

    juce::Colour fill = p.s1;
    juce::Colour border = p.line2;

    if (style == "primary") {
        fill = p.t1;
        border = p.t1;
    } else if (style == "danger") {
        fill = p.s1;
        border = p.rec;
    } else if (style == "record") {
        fill = on ? p.rec.withAlpha(0.16f) : p.s1;
        border = on ? p.rec : p.line2;
    } else if (style == "big") {
        fill = button.findColour(juce::TextButton::buttonColourId);
        border = fill;
    } else if (style == "ghost") {
        fill = on ? p.s3 : juce::Colours::transparentBlack;
        border = juce::Colours::transparentBlack;
    } else if (on) {
        fill = p.s3;
        border = p.t2;
    }

    if (!enabled) {
        // Botao cheio desligado vira superficie apagada: branco a meia opacidade
        // parecia um botao aceso.
        if (style == "big" || style == "primary") {
            fill = p.s2;
            border = p.line;
        } else {
            fill = fill.withMultipliedAlpha(0.5f);
            border = border.withMultipliedAlpha(0.5f);
        }
    } else if (down) {
        fill = fill.isTransparent() ? p.s3 : fill.darker(0.15f);
    } else if (highlighted) {
        fill = fill.isTransparent() ? p.s2 : (theme::isDark() ? fill.brighter(0.08f) : fill.darker(0.04f));
    }

    if (enabled && (style == "big" || style == "primary")) {
        g.setColour(juce::Colours::black.withAlpha(theme::isDark() ? 0.3f : 0.08f));
        g.fillRoundedRectangle(r.translated(0.0f, 2.0f), radius);
    }
    g.setColour(fill);
    g.fillRoundedRectangle(r, radius);
    if (!border.isTransparent()) {
        g.setColour(border);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
}

void PedalLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down) {
    const auto& p = theme::palette();
    const juce::String style = ui::buttonStyle(button);
    if (style == "switch") {
        return;
    }

    juce::Colour colour = p.t1;
    if (style == "primary") {
        colour = p.bg;
    } else if (style == "danger") {
        colour = p.rec;
    } else if (style == "record" && button.getToggleState()) {
        colour = p.rec;
    } else if (style == "big") {
        colour = contrastingText(button.findColour(juce::TextButton::buttonColourId));
    }
    if (!button.isEnabled()) {
        colour = (style == "big" || style == "primary") ? p.t3 : colour.withMultipliedAlpha(0.45f);
    } else if (down) {
        colour = colour.withMultipliedAlpha(0.8f);
    }

    auto area = button.getLocalBounds().toFloat().reduced(button.getHeight() * 0.18f, 0.0f);
    const float h = static_cast<float>(button.getHeight());
    const juce::String subtitle = button.getProperties()["subtitle"].toString();
    juce::Font font = getTextButtonFont(button, button.getHeight());
    juce::Font small = theme::text(font.getHeight() * 0.62f);
    if (style == "big") {
        font = theme::text(juce::jlimit(14.0f, 26.0f, h * 0.30f), theme::Weight::Bold);
        small = theme::text(font.getHeight() * 0.55f, theme::Weight::Semibold);
    }

    // Icone (propriedade "icon") a esquerda do texto, o conjunto centralizado.
    ui::Icon icon{};
    const bool hasIcon = ui::iconFromName(button.getProperties()["icon"].toString(), icon);
    const float iconSize = hasIcon ? juce::jlimit(12.0f, 30.0f, h * (style == "big" ? 0.34f : 0.42f)) : 0.0f;
    const float gap = hasIcon ? iconSize * 0.45f : 0.0f;

    const juce::String label = button.getButtonText();
    const float labelWidth = juce::jmax(juce::GlyphArrangement::getStringWidth(font, label),
                                        subtitle.isEmpty() ? 0.0f
                                                           : juce::GlyphArrangement::getStringWidth(small, subtitle));
    const float total = juce::jmin(area.getWidth(), iconSize + gap + labelWidth);
    float x = area.getCentreX() - total * 0.5f;

    if (hasIcon) {
        ui::drawIcon(g, icon, {x, area.getCentreY() - iconSize * 0.5f, iconSize, iconSize}, colour);
        x += iconSize + gap;
    }
    const juce::Rectangle<float> textArea(x, area.getY(), area.getRight() - x, area.getHeight());

    g.setColour(colour);
    if (subtitle.isEmpty()) {
        g.setFont(font);
        g.drawFittedText(label, textArea.toNearestInt(),
                         hasIcon ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.8f);
        return;
    }
    // Duas linhas: acao em cima, detalhe embaixo (botao grande de transporte).
    const float lineH = font.getHeight() + small.getHeight() * 1.1f;
    auto lines = textArea.withSizeKeepingCentre(textArea.getWidth(), lineH);
    g.setFont(font);
    g.drawFittedText(label, lines.removeFromTop(font.getHeight()).toNearestInt(),
                     hasIcon ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.8f);
    g.setColour(colour.withMultipliedAlpha(0.75f));
    g.setFont(small);
    g.drawFittedText(subtitle, lines.toNearestInt(),
                     hasIcon ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.8f);
}

void PedalLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                    juce::ComboBox& box) {
    const auto& p = theme::palette();
    auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const float radius = juce::jlimit(6.0f, 12.0f, r.getHeight() * 0.28f);
    g.setColour(p.s2);
    g.fillRoundedRectangle(r, radius);
    g.setColour(box.hasKeyboardFocus(true) ? p.t2 : p.line2);
    g.drawRoundedRectangle(r, radius, 1.0f);

    const float cx = static_cast<float>(width) - r.getHeight() * 0.45f;
    const float cy = static_cast<float>(height) * 0.5f;
    const float s = juce::jlimit(3.0f, 6.0f, r.getHeight() * 0.12f);
    juce::Path chevron;
    chevron.startNewSubPath(cx - s, cy - s * 0.5f);
    chevron.lineTo(cx, cy + s * 0.6f);
    chevron.lineTo(cx + s, cy - s * 0.5f);
    g.setColour(p.t2);
    g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void PedalLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    const int pad = juce::jmax(8, box.getHeight() / 3);
    label.setBounds(pad, 0, box.getWidth() - pad - box.getHeight(), box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void PedalLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float, float, juce::Slider::SliderStyle style, juce::Slider& slider) {
    const auto& p = theme::palette();
    const bool vertical = (style == juce::Slider::LinearVertical);
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();

    const float shortSide = vertical ? area.getWidth() : area.getHeight();
    const float thickness = juce::jlimit(4.0f, 9.0f, shortSide * 0.14f);
    const juce::Rectangle<float> track =
        vertical ? juce::Rectangle<float>(area.getCentreX() - thickness * 0.5f, area.getY(), thickness, area.getHeight())
                 : juce::Rectangle<float>(area.getX(), area.getCentreY() - thickness * 0.5f, area.getWidth(), thickness);
    theme::paintGroove(g, track, thickness * 0.5f);

    // Marca de ganho unitario (100%), quando a faixa passa por ela.
    const double unity = 100.0;
    if (slider.getMinimum() < unity && slider.getMaximum() > unity) {
        const double prop = slider.valueToProportionOfLength(unity);
        g.setColour(p.line2);
        if (vertical) {
            const float uy = area.getBottom() - static_cast<float>(prop) * area.getHeight();
            g.fillRect(area.getCentreX() - thickness * 2.2f, uy - 0.5f, thickness * 4.4f, 1.0f);
        } else {
            const float ux = area.getX() + static_cast<float>(prop) * area.getWidth();
            g.fillRect(ux - 0.5f, area.getCentreY() - thickness * 1.8f, 1.0f, thickness * 3.6f);
        }
    }

    // Trecho percorrido na cor do slider (a da track, no mixer).
    g.setColour(slider.findColour(juce::Slider::trackColourId));
    if (vertical) {
        g.fillRoundedRectangle(track.withTop(sliderPos), thickness * 0.5f);
    } else {
        g.fillRoundedRectangle(track.withRight(sliderPos), thickness * 0.5f);
    }

    // Cursor: capa de fader arredondada (vertical) ou botao redondo (horizontal).
    juce::Rectangle<float> cap;
    float capRadius;
    if (vertical) {
        const float w = juce::jlimit(22.0f, 46.0f, area.getWidth() * 0.78f);
        const float h = juce::jlimit(16.0f, 28.0f, w * 0.62f);
        cap = {area.getCentreX() - w * 0.5f, sliderPos - h * 0.5f, w, h};
        capRadius = h * 0.32f;
    } else {
        const float d = juce::jlimit(14.0f, 26.0f, shortSide * 0.62f);
        cap = {sliderPos - d * 0.5f, area.getCentreY() - d * 0.5f, d, d};
        capRadius = d * 0.5f;
    }
    g.setColour(juce::Colours::black.withAlpha(theme::isDark() ? 0.45f : 0.18f));
    g.fillRoundedRectangle(cap.translated(0.0f, 2.0f), capRadius);
    g.setColour(slider.isMouseOverOrDragging() ? p.s2 : p.s1);
    g.fillRoundedRectangle(cap, capRadius);
    g.setColour(p.line2);
    g.drawRoundedRectangle(cap.reduced(0.5f), capRadius, 1.0f);
    if (vertical) {
        g.setColour(p.t3);
        g.fillRoundedRectangle(cap.getX() + cap.getWidth() * 0.22f, cap.getCentreY() - 1.0f,
                               cap.getWidth() * 0.56f, 2.0f, 1.0f);
    }
}

juce::Label* PedalLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(theme::numbers(juce::jlimit(12.0f, 22.0f, static_cast<float>(slider.getTextBoxHeight()) * 0.66f)));
    label->setColour(juce::Label::textColourId, theme::palette().t1);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return label;
}

void PedalLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(r, juce::jlimit(5.0f, 10.0f, r.getHeight() * 0.25f));
}

void PedalLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    if (!editor.isEnabled()) {
        return;
    }
    auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const auto& p = theme::palette();
    g.setColour(editor.hasKeyboardFocus(true) ? p.t2 : p.line2);
    g.drawRoundedRectangle(r, juce::jlimit(5.0f, 10.0f, r.getHeight() * 0.25f), 1.0f);
}

void PedalLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    const auto& p = theme::palette();
    auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(p.s1);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(p.line2);
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
}

void PedalLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path,
                                                juce::Image&) {
    const auto& p = theme::palette();
    g.setColour(juce::Colours::black.withAlpha(theme::isDark() ? 0.45f : 0.15f));
    g.fillPath(path, juce::AffineTransform::translation(0.0f, 4.0f));
    g.setColour(p.s1);
    g.fillPath(path);
    g.setColour(p.line2);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void PedalLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                                    juce::TextLayout& layout) {
    const auto& p = theme::palette();
    auto r = alert.getLocalBounds().toFloat();
    g.setColour(p.s1);
    g.fillRoundedRectangle(r, 14.0f);
    g.setColour(p.line2);
    g.drawRoundedRectangle(r.reduced(0.5f), 14.0f, 1.0f);
    layout.draw(g, textArea.toFloat());
}

void PedalLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool vertical, int thumbStart, int thumbSize, bool over, bool down) {
    if (thumbSize <= 0) {
        return;
    }
    juce::Rectangle<float> thumb =
        vertical ? juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(thumbStart), static_cast<float>(width),
                                          static_cast<float>(thumbSize))
                 : juce::Rectangle<float>(static_cast<float>(thumbStart), static_cast<float>(y),
                                          static_cast<float>(thumbSize), static_cast<float>(height));
    thumb = thumb.reduced(vertical ? width * 0.3f : 0.0f, vertical ? 0.0f : height * 0.3f);
    g.setColour(theme::palette().t3.withAlpha(over || down ? 0.7f : 0.4f));
    g.fillRoundedRectangle(thumb, juce::jmin(thumb.getWidth(), thumb.getHeight()) * 0.5f);
}
