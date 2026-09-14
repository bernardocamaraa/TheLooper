#include "PedalLookAndFeel.h"

namespace theme {

namespace {
// Escolhe a primeira familia instalada da lista de preferencia. Resolver isto
// uma vez e guardar evita varrer a lista de fontes a cada repintura.
juce::String pickTypeface(const juce::StringArray& preferences) {
    const auto available = juce::Font::findAllTypefaceNames();
    for (const auto& name : preferences) {
        if (available.contains(name)) {
            return name;
        }
    }
    return juce::Font::getDefaultSansSerifFontName();
}

const juce::String& legendFace(bool strong) {
    static const juce::String regular =
        pickTypeface({"Bahnschrift SemiCondensed", "Bahnschrift", "Arial Narrow"});
    static const juce::String semibold =
        pickTypeface({"Bahnschrift SemiBold SemiConden", "Bahnschrift SemiBold", "Bahnschrift"});
    return strong ? semibold : regular;
}

const juce::String& displayFace() {
    static const juce::String face =
        pickTypeface({"Bahnschrift Light SemiCondensed", "Bahnschrift Light", "Bahnschrift"});
    return face;
}

const juce::String& dataFace() {
    static const juce::String face = pickTypeface({"Cascadia Mono", "Consolas"});
    return face;
}
} // namespace

juce::Font legend(float height, bool strong) {
    // Legenda serigrafada: caixa alta e bem espacada. O espacamento e o que
    // faz a letra parecer impressa na caixa em vez de digitada num campo.
    return juce::Font(juce::FontOptions(legendFace(strong), height, juce::Font::plain))
        .withExtraKerningFactor(0.13f);
}

juce::Font display(float height) {
    return juce::Font(juce::FontOptions(displayFace(), height, juce::Font::plain))
        .withExtraKerningFactor(0.05f);
}

juce::Font data(float height) {
    return juce::Font(juce::FontOptions(dataFace(), height, juce::Font::plain));
}

void paintPanel(juce::Graphics& g, juce::Rectangle<int> bounds, bool raised) {
    auto r = bounds.toFloat();
    constexpr float radius = 5.0f;

    g.setColour(raised ? panelRaised : panel);
    g.fillRoundedRectangle(r, radius);

    // Aresta de cima pegando luz, aresta de baixo na sombra - e o que da
    // relevo de peca fisica sem precisar de borda desenhada em volta.
    juce::Path top;
    top.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), radius * 2.0f, radius, radius, true, true,
                             false, false);
    g.setColour(juce::Colours::white.withAlpha(0.055f));
    g.strokePath(top, juce::PathStrokeType(1.0f));

    juce::Path bottom;
    bottom.addRoundedRectangle(r.getX(), r.getBottom() - radius * 2.0f, r.getWidth(), radius * 2.0f, radius,
                                radius, false, false, true, true);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.strokePath(bottom, juce::PathStrokeType(1.0f));
}

void paintGroove(juce::Graphics& g, juce::Rectangle<float> bounds, float radius) {
    g.setColour(groove);
    g.fillRoundedRectangle(bounds, radius);
    // Rebaixo: sombra em cima, luz embaixo - o inverso do painel.
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawLine(bounds.getX() + radius, bounds.getY() + 0.5f, bounds.getRight() - radius,
                bounds.getY() + 0.5f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawLine(bounds.getX() + radius, bounds.getBottom() - 0.5f, bounds.getRight() - radius,
                bounds.getBottom() - 0.5f, 1.0f);
}

void paintSectionRule(juce::Graphics& g, juce::Rectangle<int> row, const juce::String& text) {
    // Retangulo vazio = a secao nao esta na pagina aberta (ver
    // MainComponent::setPage): nao ha nada a serigrafar.
    if (row.isEmpty()) {
        return;
    }

    auto r = row.toFloat();
    const float centreY = r.getCentreY();

    // O corpo da legenda sai da ALTURA da regua: quem chama e que escala a
    // linha conforme a janela, e a serigrafia acompanha sem parametro extra.
    g.setFont(legend(juce::jlimit(11.0f, 20.0f, r.getHeight() * 0.68f), true));
    const float textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text);

    g.setColour(inkDim);
    g.drawText(text, row, juce::Justification::centredLeft, false);

    // A regua encosta na legenda em vez de passar por baixo: e serigrafia de
    // painel, nao sublinhado.
    g.setColour(hairline);
    g.drawLine(r.getX() + textWidth + 10.0f, centreY, r.getRight(), centreY, 1.0f);
}

} // namespace theme

// ---------------------------------------------------------------------------

PedalLookAndFeel::PedalLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, theme::enclosure);
    setColour(juce::Label::textColourId, theme::ink);
    setColour(juce::Label::textWhenEditingColourId, theme::ink);
    setColour(juce::Label::backgroundWhenEditingColourId, theme::groove);
    setColour(juce::Label::outlineWhenEditingColourId, theme::ledRed);
    setColour(juce::TextEditor::highlightColourId, theme::ledRed.withAlpha(0.3f));
    setColour(juce::TextEditor::highlightedTextColourId, theme::ink);
    setColour(juce::CaretComponent::caretColourId, theme::ledRed);

    setColour(juce::ComboBox::backgroundColourId, theme::groove);
    setColour(juce::ComboBox::textColourId, theme::ink);
    setColour(juce::ComboBox::arrowColourId, theme::inkDim);
    setColour(juce::ComboBox::outlineColourId, theme::hairline);

    setColour(juce::PopupMenu::backgroundColourId, theme::panelRaised);
    setColour(juce::PopupMenu::textColourId, theme::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::ledRed.withAlpha(0.22f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour(juce::TextButton::buttonColourId, theme::panelRaised);
    setColour(juce::TextButton::textColourOffId, theme::ink);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    setColour(juce::TooltipWindow::backgroundColourId, theme::panelRaised);
    setColour(juce::TooltipWindow::textColourId, theme::ink);
    setColour(juce::TooltipWindow::outlineColourId, theme::hairline);
}

juce::Font PedalLookAndFeel::getLabelFont(juce::Label& label) {
    return label.getFont();
}

// As fontes dos controles saem da ALTURA de cada controle, nao de um numero
// fixo. Assim, quando a janela cresce num monitor touch e os controles crescem
// junto, o texto cresce com eles - antes tudo ficava minusculo perto do
// desenho do pedal, que era o unico que escalava.
juce::Font PedalLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return theme::legend(juce::jlimit(14.0f, 22.0f, static_cast<float>(box.getHeight()) * 0.52f));
}

juce::Font PedalLookAndFeel::getPopupMenuFont() {
    return theme::legend(16.0f);
}

juce::Font PedalLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return theme::legend(juce::jlimit(12.0f, 20.0f, static_cast<float>(buttonHeight) * 0.44f), true);
}

void PedalLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool highlighted, bool down) {
    auto r = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = juce::jlimit(4.0f, 9.0f, r.getHeight() * 0.16f);

    const bool on = button.getToggleState();
    juce::Colour base = on ? theme::ledGreen.withAlpha(0.16f) : theme::panelRaised;
    if (down) {
        base = base.darker(0.4f);
    } else if (highlighted) {
        base = base.brighter(0.12f);
    }

    g.setColour(base);
    g.fillRoundedRectangle(r, radius);

    // Botao apertado perde o fio de luz de cima: parece afundado, como uma
    // tecla de painel.
    if (!down) {
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawLine(r.getX() + radius, r.getY() + 0.5f, r.getRight() - radius, r.getY() + 0.5f, 1.0f);
    }
    g.setColour(on ? theme::ledGreen.withAlpha(0.55f) : theme::hairline);
    g.drawRoundedRectangle(r, radius, 1.0f);
}

void PedalLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down) {
    g.setFont(getTextButtonFont(button, button.getHeight()));
    const juce::Colour colour = button.getToggleState()
                                     ? theme::ledGreen
                                     : (button.isEnabled() ? theme::ink : theme::inkFaint);
    g.setColour(down ? colour.darker(0.2f) : colour);
    // Sem forcar caixa alta: juce::String::toUpperCase nao converte
    // acentuadas, e "Padrao" com til virava "PADRaO" com o til solto no meio.
    // Caixa alta fica so nas LEGENDAS (reguas de secao, captions); o texto de
    // um botao e uma acao, e acao se le melhor em caixa de sentenca.
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void PedalLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                     juce::ComboBox& box) {
    auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
                 .reduced(0.5f);
    theme::paintGroove(g, r, 4.0f);

    g.setColour(box.hasKeyboardFocus(true) ? theme::ledRed.withAlpha(0.6f) : theme::hairline);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    // Seta discreta: um chevron desenhado, nao a seta cheia do JUCE.
    const float cx = static_cast<float>(width) - 14.0f;
    const float cy = static_cast<float>(height) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chevron.lineTo(cx, cy + 2.5f);
    chevron.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(theme::inkDim);
    g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
}

void PedalLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(9, 0, box.getWidth() - 26, box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void PedalLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float, float, juce::Slider::SliderStyle style,
                                         juce::Slider& slider) {
    const bool vertical = (style == juce::Slider::LinearVertical);
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Curso rebaixado. A espessura sai do lado CURTO do slider: numa tela
    // grande o fader inteiro cresce, e um curso de 5 px fixos viraria um fio.
    const float shortSide = vertical ? area.getWidth() : area.getHeight();
    const float trackThickness = juce::jlimit(5.0f, 11.0f, shortSide * 0.13f);
    juce::Rectangle<float> track =
        vertical ? juce::Rectangle<float>(area.getCentreX() - trackThickness * 0.5f, area.getY(),
                                           trackThickness, area.getHeight())
                 : juce::Rectangle<float>(area.getX(), area.getCentreY() - trackThickness * 0.5f,
                                           area.getWidth(), trackThickness);
    theme::paintGroove(g, track, trackThickness * 0.5f);

    // Marca de unidade (100%): num fader de mesa e a risca do ganho neutro.
    // So aparece quando a faixa passa por ela.
    const double unity = 100.0;
    if (slider.getMinimum() < unity && slider.getMaximum() > unity) {
        const double prop = (unity - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum());
        const double skewed = std::pow(prop, slider.getSkewFactor());
        g.setColour(theme::inkFaint);
        if (vertical) {
            const float uy = area.getBottom() - static_cast<float>(skewed) * area.getHeight();
            const float tick = juce::jmax(9.0f, trackThickness * 1.8f);
            g.drawLine(area.getCentreX() - tick, uy, area.getCentreX() + tick, uy, 1.0f);
        } else {
            const float ux = area.getX() + static_cast<float>(skewed) * area.getWidth();
            const float tick = juce::jmax(7.0f, trackThickness * 1.5f);
            g.drawLine(ux, area.getCentreY() - tick, ux, area.getCentreY() + tick, 1.0f);
        }
    }

    // Trecho percorrido, em verde de LED.
    g.setColour(theme::ledGreen.withAlpha(0.55f));
    if (vertical) {
        auto filled = track.withTop(sliderPos);
        g.fillRoundedRectangle(filled, trackThickness * 0.5f);
    } else {
        auto filled = track.withRight(sliderPos);
        g.fillRoundedRectangle(filled, trackThickness * 0.5f);
    }

    // Cursor: bloco retangular com risca central, como o cabecote de um fader
    // de console - nao a bolinha padrao do JUCE. Tambem cresce com o lado
    // curto, para virar um alvo de dedo quando o painel esta grande.
    const float capLong = juce::jlimit(20.0f, 38.0f, shortSide * (vertical ? 0.58f : 0.86f));
    const float capShort = vertical ? juce::jlimit(24.0f, 52.0f, area.getWidth() * 0.85f)
                                     : juce::jlimit(16.0f, 28.0f, shortSide * 0.46f);
    juce::Rectangle<float> cap =
        vertical ? juce::Rectangle<float>(area.getCentreX() - capShort * 0.5f, sliderPos - capLong * 0.5f,
                                           capShort, capLong)
                 : juce::Rectangle<float>(sliderPos - capShort * 0.5f, area.getCentreY() - capLong * 0.5f,
                                           capShort, capLong);

    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), 3.0f);
    g.setColour(slider.isMouseOverOrDragging() ? theme::panelRaised.brighter(0.25f) : theme::panelRaised);
    g.fillRoundedRectangle(cap, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(cap.reduced(0.5f), 3.0f, 1.0f);
    g.setColour(theme::ink.withAlpha(0.85f));
    if (vertical) {
        g.drawLine(cap.getX() + 4.0f, cap.getCentreY(), cap.getRight() - 4.0f, cap.getCentreY(), 1.4f);
    } else {
        g.drawLine(cap.getCentreX(), cap.getY() + 4.0f, cap.getCentreX(), cap.getBottom() - 4.0f, 1.4f);
    }
}

juce::Label* PedalLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    // O corpo sai da altura da caixa de texto, que quem monta o layout escala
    // junto com o resto - ver MainComponent::resized.
    label->setFont(theme::data(
        juce::jlimit(12.0f, 20.0f, static_cast<float>(slider.getTextBoxHeight()) * 0.62f)));
    label->setColour(juce::Label::textColourId, theme::ink);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return label;
}

void PedalLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(theme::panelRaised);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
}
