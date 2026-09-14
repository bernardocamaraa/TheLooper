// Driver nao-bloqueante dos 4 LEDs bicolores. Mantem o estado "desejado" por
// track (definido remotamente pelo PC via MSG_LED_SET/MSG_LED_SET_ALL) e um
// modo de "aviso de reset" local (todos os LEDs piscando vermelho enquanto o
// UNDO esta armado para o reset geral) que sobrepoe o estado normal.
#pragma once

#include <Arduino.h>
#include "protocol.h"

class LedController {
public:
    void begin();

    // Atualiza o estado desejado de uma track (vindo do PC).
    void setTrack(uint8_t track, protocol::LedColor color, bool blink);

    // Enquanto ativo, os 4 LEDs piscam vermelho, ignorando o estado normal.
    void setResetWarning(bool active);

    // Chamar a cada iteracao de loop(); nunca bloqueia (usa millis()).
    void update(uint32_t nowMs);

private:
    struct TrackLedState {
        protocol::LedColor color = protocol::kLedOff;
        bool blink = false;
    };

    void writePins(uint8_t track, bool red, bool green);

    TrackLedState tracks_[4];
    bool resetWarningActive_ = false;
    bool blinkPhaseOn_ = false;
    uint32_t lastBlinkToggleMs_ = 0;
};
