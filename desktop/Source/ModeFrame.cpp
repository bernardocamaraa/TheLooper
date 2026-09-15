#include "ModeFrame.h"

#include "PedalLookAndFeel.h"

namespace ui {


FrameMood moodFor(bool recMode, bool transportPlaying) {
    if (!transportPlaying) {
        return FrameMood::Stopped;
    }
    return recMode ? FrameMood::Rec : FrameMood::Play;
}

juce::Colour frameColour(FrameMood mood) {
    switch (mood) {
        case FrameMood::Rec: return theme::ledRed;
        case FrameMood::Play: return theme::ledGreen;
        case FrameMood::Stopped: return theme::inkFaint;
    }
    return theme::inkFaint;
}

float frameThickness() {
    // Fina e suave no visual novo: e luz na borda, nao moldura.
    return juce::jlimit(6.0f, 22.0f, theme::pxf(12.0f));
}

void paintModeFrame(juce::Graphics& g, juce::Rectangle<int> bounds, FrameMood mood) {
    const juce::Colour base = frameColour(mood);
    const auto r = bounds.toFloat();
    const float kThickness = frameThickness();

    // Quatro gradientes lineares, um por aresta, indo da cor para
    // transparente. A versao anterior empilhava contornos concentricos e o
    // resultado nao lia como degrade: as voltas de fora se somavam e viravam
    // uma tarja vermelha dura em volta da janela.
    //
    // A intensidade e baixa de proposito. Isto e luz de LED refletindo na
    // borda da caixa, nao uma moldura pintada - tem que dar para saber o modo
    // pelo canto do olho sem que a moldura dispute atencao com o conteudo.
    const float alpha = (mood == FrameMood::Stopped) ? 0.12f : 0.34f;
    const juce::Colour edge = base.withAlpha(alpha);
    const juce::Colour fade = base.withAlpha(0.0f);

    struct Edge {
        juce::Rectangle<float> area;
        juce::Point<float> from, to;
    };

    const Edge edges[] = {
        {r.withHeight(kThickness), {r.getX(), r.getY()}, {r.getX(), r.getY() + kThickness}},
        {r.withTop(r.getBottom() - kThickness),
         {r.getX(), r.getBottom()},
         {r.getX(), r.getBottom() - kThickness}},
        {r.withWidth(kThickness), {r.getX(), r.getY()}, {r.getX() + kThickness, r.getY()}},
        {r.withLeft(r.getRight() - kThickness),
         {r.getRight(), r.getY()},
         {r.getRight() - kThickness, r.getY()}},
    };

    for (const auto& e : edges) {
        g.setGradientFill(juce::ColourGradient(edge, e.from, fade, e.to, false));
        g.fillRect(e.area);
    }

    // Fio vivo rente a borda: da a definicao que o degrade sozinho nao tem.
    g.setColour(base.withAlpha(alpha * 1.5f));
    g.drawRect(r, 1.0f);
}

} // namespace ui
