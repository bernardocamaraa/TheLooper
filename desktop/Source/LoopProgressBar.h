// Barra de progresso do loop mestre: enche conforme o ponteiro de leitura
// avanca e volta a zero quando o loop reinicia. Substitui a linha de texto
// "Tocando | Track selecionada | Loop | Latencia" que ficava no topo - de
// relance ela responde a unica pergunta que importa tocando: o loop esta no
// comeco, no meio ou no fim.
//
// Sem texto de proposito: e para ser lida perifericamente, nao interpretada.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ModeFrame.h"

class LoopProgressBar : public juce::Component {
public:
    // position vai de 0 a 1. loopDefined = false enquanto nenhuma track
    // fechou uma gravacao (estado "primordial"), quando a barra mostra so o
    // trilho vazio.
    void setProgress(double position, bool loopDefined, ui::FrameMood mood);

    void paint(juce::Graphics& g) override;

private:
    double position_ = 0.0;
    bool loopDefined_ = false;
    ui::FrameMood mood_ = ui::FrameMood::Stopped;
};
