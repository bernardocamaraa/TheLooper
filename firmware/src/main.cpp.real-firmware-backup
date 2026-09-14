// Firmware do controlador do pedal (Arduino Mega 2560).
//
// Este firmware NAO conhece a FSM do looper (REC/PLAY, estados de track,
// etc.) - toda essa logica vive no app desktop (ver software/desktop). Aqui
// so existe: debounce + classificacao de gestos dos 8 footswitches, envio
// desses eventos por serial, e controle dos 4 LEDs bicolores conforme
// comandos recebidos do PC. Ver software/docs/PROTOCOL.md e CONTROL_MODEL.md.
#include <Arduino.h>
#include "protocol.h"
#include "Config.h"
#include "Button.h"
#include "LedController.h"
#include "SerialProtocol.h"

namespace {

// Ordem alinhada com protocol::ButtonId (kButtonRecPlay=0 .. kButtonTrack4=7).
Button buttons[protocol::kButtonCount];
LedController ledController;
SerialProtocol serialProtocol;

void setupButtons() {
    buttons[protocol::kButtonRecPlay].begin(config::kPinRecPlay);
    buttons[protocol::kButtonPause].begin(config::kPinPause); // STOP simples, sem hold
    buttons[protocol::kButtonUndo].begin(config::kPinUndo, config::kClearAllHoldMs);
    buttons[protocol::kButtonMode].begin(config::kPinMode);
    buttons[protocol::kButtonTrack1].begin(config::kPinTrack1);
    buttons[protocol::kButtonTrack2].begin(config::kPinTrack2);
    buttons[protocol::kButtonTrack3].begin(config::kPinTrack3);
    buttons[protocol::kButtonTrack4].begin(config::kPinTrack4);
}

} // namespace

void setup() {
    setupButtons();
    ledController.begin();
    serialProtocol.begin(&ledController);
    serialProtocol.sendHello();
}

void loop() {
    uint32_t nowMs = millis();

    serialProtocol.poll();

    for (uint8_t id = 0; id < protocol::kButtonCount; ++id) {
        ButtonEvent event = buttons[id].update(nowMs);
        if (event == ButtonEvent::kEventPress) {
            serialProtocol.sendButtonEvent(id, protocol::kGesturePress);
        } else if (event == ButtonEvent::kEventLongPress) {
            serialProtocol.sendButtonEvent(id, protocol::kGestureLongPress);
        }
    }

    bool clearAllArmed = buttons[protocol::kButtonUndo].isArmed(nowMs, config::kClearAllArmWarningMs);
    ledController.setResetWarning(clearAllArmed);
    ledController.update(nowMs);

    serialProtocol.sendHeartbeatIfDue(nowMs);
}
