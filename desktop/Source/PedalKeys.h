// Mapeamento de teclado para os 8 footswitches.
//
// Sem isto o app nao faz absolutamente nada com o pedal desconectado: os
// eventos de botao so entravam pela serial. Com o teclado da para ensaiar,
// testar e usar o looper sem o hardware plugado.
//
// As teclas seguem a posicao no pedal, nao a inicial da palavra: espaco e o
// botao que se aperta o tempo todo (REC/PLAY), e 1-4 sao as tracks.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "protocol.h"

namespace ui {

struct PedalKeyAction {
    bool valid = false;
    protocol::ButtonId button = protocol::kButtonRecPlay;
    protocol::Gesture gesture = protocol::kGesturePress;
};

inline PedalKeyAction pedalActionForKey(const juce::KeyPress& key) {
    PedalKeyAction action;
    const juce::juce_wchar c = key.getTextCharacter();
    const int code = key.getKeyCode();
    const bool shift = key.getModifiers().isShiftDown();

    auto set = [&action](protocol::ButtonId b, protocol::Gesture g) {
        action.valid = true;
        action.button = b;
        action.gesture = g;
    };

    if (code == juce::KeyPress::spaceKey) {
        set(protocol::kButtonRecPlay, protocol::kGesturePress);
        return action;
    }

    switch (juce::CharacterFunctions::toLowerCase(c)) {
        case 's': set(protocol::kButtonPause, protocol::kGesturePress); break;
        // Shift+U = Clear All, o equivalente do hold de 3s no pedal.
        case 'u': set(protocol::kButtonUndo,
                       shift ? protocol::kGestureLongPress : protocol::kGesturePress); break;
        case 'm': set(protocol::kButtonMode, protocol::kGesturePress); break;
        case '1': set(protocol::kButtonTrack1, protocol::kGesturePress); break;
        case '2': set(protocol::kButtonTrack2, protocol::kGesturePress); break;
        case '3': set(protocol::kButtonTrack3, protocol::kGesturePress); break;
        case '4': set(protocol::kButtonTrack4, protocol::kGesturePress); break;
        default: break;
    }
    return action;
}

// Texto do atalho de cada botao, para o rotulo na tela.
inline juce::String pedalKeyHint(protocol::ButtonId button) {
    switch (button) {
        case protocol::kButtonRecPlay: return "espaco";
        case protocol::kButtonPause: return "S";
        case protocol::kButtonUndo: return "U";
        case protocol::kButtonMode: return "M";
        case protocol::kButtonTrack1: return "1";
        case protocol::kButtonTrack2: return "2";
        case protocol::kButtonTrack3: return "3";
        case protocol::kButtonTrack4: return "4";
        default: return {};
    }
}

} // namespace ui
