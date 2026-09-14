// Camada fina entre o protocolo compartilhado (shared/protocol.h) e a
// HardwareSerial do Mega. Recebe MSG_LED_SET/MSG_LED_SET_ALL do PC e
// repassa para o LedController; envia MSG_BUTTON_EVENT, MSG_HEARTBEAT e
// MSG_FIRMWARE_HELLO para o PC.
#pragma once

#include <Arduino.h>
#include "protocol.h"
#include "LedController.h"

class SerialProtocol {
public:
    void begin(LedController* ledController);

    // Le todos os bytes disponiveis na porta serial e alimenta o parser.
    void poll();

    void sendButtonEvent(uint8_t buttonId, uint8_t gesture);
    void sendHeartbeatIfDue(uint32_t nowMs);
    void sendHello();

private:
    static void onFrame(void* userData, uint8_t type, const uint8_t* payload, uint8_t len);
    void handleFrame(uint8_t type, const uint8_t* payload, uint8_t len);

    protocol::FrameParser parser_;
    LedController* ledController_ = nullptr;
    uint32_t lastHeartbeatMs_ = 0;
};
