// Moldura em degrade que indica o modo global, pintada na borda das DUAS
// janelas (controles e VUs).
//
// Substitui o texto "MODO REC"/"MODO PLAY" que ficava no topo: a informacao
// passa a ser periferica, entao da pra ver de longe e de canto de olho
// durante a apresentacao, sem ocupar espaco util nem exigir leitura.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

enum class FrameMood { Rec, Play, Stopped };

// Parado tem prioridade sobre o modo: com o transporte parado a moldura fica
// cinza, seja em REC_MODE ou em Mute Mode.
FrameMood moodFor(bool recMode, bool transportPlaying);

juce::Colour frameColour(FrameMood mood);

// Pinta a moldura degrade ao longo da borda de bounds (mais saturada na
// beirada, sumindo para dentro).
void paintModeFrame(juce::Graphics& g, juce::Rectangle<int> bounds, FrameMood mood);

// Espessura ocupada pela moldura, para as janelas reservarem essa margem no
// resized() e nao encostarem conteudo nela.
float frameThickness();

} // namespace ui
