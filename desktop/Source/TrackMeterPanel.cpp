#include "TrackMeterPanel.h"

#include "ModeFrame.h"
#include "PedalLookAndFeel.h"
#include "UiText.h"

namespace {
juce::String stateLabelText(TrackState state) {
    switch (state) {
        case TrackState::EMPTY: return "VAZIA";
        case TrackState::RECORDING: return "GRAVANDO";
        case TrackState::PLAYING: return "TOCANDO";
        case TrackState::MUTED: return "MUTADA";
    }
    return {};
}

juce::Colour stateColour(TrackState state) {
    switch (state) {
        case TrackState::EMPTY: return theme::inkFaint;
        case TrackState::RECORDING: return theme::ledRed;
        case TrackState::PLAYING: return theme::ledGreen;
        case TrackState::MUTED: return theme::inkDim;
    }
    return theme::inkFaint;
}
} // namespace

TrackMeterPanel::TrackMeterPanel() {
    // Nome em corpo grande e leve: esta janela e lida a distancia, e peso
    // demais em caixa alta vira mancha. O espacamento e que da a legibilidade.
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setFont(theme::display(24.0f));
    nameLabel_.setColour(juce::Label::textColourId, theme::ink);
    addAndMakeVisible(nameLabel_);

    stateLabel_.setJustificationType(juce::Justification::centred);
    stateLabel_.setFont(theme::legend(13.0f, true));
    addAndMakeVisible(stateLabel_);

    inputLabel_.setJustificationType(juce::Justification::centred);
    inputLabel_.setFont(theme::legend(11.5f));
    addAndMakeVisible(inputLabel_);

    addAndMakeVisible(vuMeter_);
}

void TrackMeterPanel::update(const juce::String& name, TrackState state, bool selectedInRecMode,
                              int layerCount, uint32_t inputMask) {
    nameLabel_.setText(name, juce::dontSendNotification);

    // Na track selecionada o VU esta medindo a ENTRADA, nao a reproducao -
    // sem dizer isso, um medidor mexendo numa track vazia parece defeito.
    // Nas demais o rotulo fica apagado, so como referencia do roteamento.
    const juce::String inputName = ui::inputMaskName(inputMask);
    inputLabel_.setText(selectedInRecMode ? "ENTRADA: " + inputName : inputName,
                         juce::dontSendNotification);
    inputLabel_.setColour(juce::Label::textColourId, selectedInRecMode
                                                          ? ui::frameColour(ui::FrameMood::Rec)
                                                          : theme::inkFaint);

    // Mostrar o numero de camadas e o unico jeito de conferir se o UNDO/CLEAR
    // realmente removeu um passe.
    juce::String text = stateLabelText(state);
    if (layerCount > 0) {
        text += juce::String(layerCount == 1 ? "  (1 camada)" : "  (" + juce::String(layerCount) + " camadas)");
    }
    stateLabel_.setText(text, juce::dontSendNotification);
    stateLabel_.setColour(juce::Label::textColourId, stateColour(state));
    vuMeter_.setMuted(state == TrackState::MUTED);

    if (selectedInRecMode != selectedInRecMode_) {
        selectedInRecMode_ = selectedInRecMode;
        repaint();
    }
}

void TrackMeterPanel::resized() {
    // Mesma regra da janela de controles: as medidas escalam com a largura do
    // painel (215 px = a largura na janela padrao de VUs), senao maximizar a
    // janela no monitor so aumenta o vazio em volta de um texto miudo.
    const float scale = juce::jlimit(1.0f, 2.2f, static_cast<float>(getWidth()) / 215.0f);
    const auto sc = [scale](int value) {
        return juce::roundToInt(static_cast<float>(value) * scale);
    };

    if (std::abs(scale - scale_) > 0.01f) {
        scale_ = scale;
        nameLabel_.setFont(theme::display(24.0f * scale));
        stateLabel_.setFont(theme::legend(13.0f * scale, true));
        inputLabel_.setFont(theme::legend(11.5f * scale));
    }

    auto area = getLocalBounds().reduced(sc(10));
    nameLabel_.setBounds(area.removeFromTop(sc(30)));
    stateLabel_.setBounds(area.removeFromTop(sc(22)));
    inputLabel_.setBounds(area.removeFromTop(sc(18)));
    area.removeFromTop(sc(10));

    // Largura cheia: este e o medidor grande, para ser lido a distancia. A
    // barra continua (VuMeter::Style::Bar, o padrao) nao tem o problema que
    // os segmentos teriam aqui - numa coluna larga cada segmento ficaria mais
    // largo que alto e a regua leria como tecido listrado.
    vuMeter_.setBounds(area);
}

void TrackMeterPanel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().reduced(2);
    theme::paintPanel(g, bounds, selectedInRecMode_);

    if (selectedInRecMode_) {
        // Track selecionada: em vez de um contorno grosso, a aresta ACENDE -
        // um halo curto para dentro mais um fio vivo. Le como luz de LED
        // batendo na peca, e nao briga com a moldura de modo da janela.
        auto r = bounds.toFloat();
        const juce::Colour led = theme::ledRed;
        for (int i = 0; i < 5; ++i) {
            const float t = static_cast<float>(i) / 4.0f;
            g.setColour(led.withAlpha(0.16f * (1.0f - t)));
            g.drawRoundedRectangle(r.reduced(1.0f + t * 5.0f), 5.0f, 1.6f);
        }
        g.setColour(led);
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.4f);
    }
}
