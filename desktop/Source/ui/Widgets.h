// Pecas pequenas da interface nova, usadas por varias paginas: rotulos de
// estado, o anel do loop, o controle segmentado, o cartao de track, o seletor
// de cor da track e os dialogos com a cara do app.
#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioTrack.h"
#include "Config.h"
#include "ModeFrame.h"

namespace ui {

// --- Estado de track --------------------------------------------------------

juce::String stateText(TrackState state);
juce::Colour stateColour(TrackState state);
juce::String layersText(int layers); // "sem camadas", "1 camada", "37 camadas"

// Chip de estado alinhado a esquerda na area; devolve a largura usada.
float paintStateChip(juce::Graphics& g, juce::Rectangle<float> area, TrackState state);

// --- Anel do loop -------------------------------------------------------------
//
// Um anel por track (na cor dela) e um ponteiro que da uma volta por loop. A
// track gravando enche o anel conforme o passe avanca; a mutada fica apagada.

struct RingState {
    double progress = 0.0;
    bool defined = false;
    FrameMood mood = FrameMood::Stopped;
    std::array<TrackState, config::kNumTracks> states{};
    double lengthSeconds = 0.0;
};

void paintLoopRings(juce::Graphics& g, juce::Rectangle<float> area, const RingState& ring);

// --- Controle segmentado ---------------------------------------------------------

class SegmentedControl : public juce::Component {
public:
    void setOptions(const juce::StringArray& options);
    void setSelected(int index, bool notify = false);
    int selected() const { return selected_; }

    std::function<void(int)> onChange;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> segment(int index) const;

    juce::StringArray options_;
    int selected_ = 0;
};

// --- Cartao de track (aba Tocar) -------------------------------------------------

class TrackCard : public juce::Component {
public:
    struct Data {
        juce::String name;
        TrackState state = TrackState::EMPTY;
        int layers = 0;
        juce::String input;
        bool selected = false;
        float level = 0.0f;
    };

    explicit TrackCard(int index);

    void setData(const Data& data);
    std::function<void()> onClick;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> meterArea() const;

    int index_;
    Data data_;
};

// --- Seletor de cor da track ---------------------------------------------------------
//
// Abre num balao ao lado do componente: 12 cores prontas, "Outra cor..." e
// "Voltar ao padrao". onPicked(transparentBlack) = voltar a cor da paleta.
void showTrackColourPicker(juce::Component& anchor, int track, std::function<void(juce::Colour)> onPicked);

// --- Dialogos --------------------------------------------------------------------

void confirm(const juce::String& title, const juce::String& message, const juce::String& confirmText,
             std::function<void()> onConfirm, bool destructive = true);
void askText(const juce::String& title, const juce::String& message, const juce::String& initial,
             const juce::String& okText, std::function<void(juce::String)> onOk);
void notify(const juce::String& title, const juce::String& message);

// --- Cabecalho de pagina ---------------------------------------------------------------

void paintPageTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                    const juce::String& subtitle);

} // namespace ui
