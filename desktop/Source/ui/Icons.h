// Icones de traco da interface, desenhados em codigo (juce::Path) num quadro
// de 24 x 24 e escalados para qualquer tamanho - nitidos em qualquer escala e
// na cor que o chamador pedir (acompanham o tema).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

enum class Icon {
    Play, Rec, Stop, Undo, Mode, Trash, Plus, Search, Mixer, Sessions, Setlist, Screens, Settings,
    Pedal, Cards, Export, Folder, Next, Prev, Sun, Moon, Check, Close, Edit, Up, Down, Copy, Save,
    Record,
};

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);

// Para a propriedade "icon" dos botoes (ver PedalLookAndFeel::drawButtonText).
bool iconFromName(const juce::String& name, Icon& out);
void setButtonIcon(juce::Button& button, const juce::String& name);

} // namespace ui
