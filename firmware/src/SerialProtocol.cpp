#include "SerialProtocol.h"

void SerialProtocol::begin(LedController* ledController) {
    ledController_ = ledController;
    // Serial1 (pinos 18/19), nao Serial0/USB: o HC-05 fica nesses pinos para
    // nao brigar com o conversor USB da placa, que tambem vive na Serial0.
    Serial1.begin(protocol::kBaudRate);
    parser_.setHandler(&SerialProtocol::onFrame, this);
    lastHeartbeatMs_ = millis();
}

void SerialProtocol::poll() {
    while (Serial1.available() > 0) {
        parser_.feed(static_cast<uint8_t>(Serial1.read()));
    }
}

void SerialProtocol::sendButtonEvent(uint8_t buttonId, uint8_t gesture) {
    protocol::ButtonEventPayload payload{buttonId, gesture};
    uint8_t frame[5 + sizeof(payload)];
    uint8_t n = protocol::encodeFrame(protocol::kMsgButtonEvent,
                                       reinterpret_cast<const uint8_t*>(&payload),
                                       sizeof(payload), frame);
    Serial1.write(frame, n);
}

void SerialProtocol::sendHeartbeatIfDue(uint32_t nowMs) {
    if ((nowMs - lastHeartbeatMs_) < protocol::kHeartbeatIntervalMs) {
        return;
    }
    lastHeartbeatMs_ = nowMs;
    uint8_t frame[5];
    uint8_t n = protocol::encodeFrame(protocol::kMsgHeartbeat, nullptr, 0, frame);
    Serial1.write(frame, n);
}

void SerialProtocol::sendHello() {
    protocol::FirmwareHelloPayload payload{protocol::kFirmwareVersion};
    uint8_t frame[5 + sizeof(payload)];
    uint8_t n = protocol::encodeFrame(protocol::kMsgFirmwareHello,
                                       reinterpret_cast<const uint8_t*>(&payload),
                                       sizeof(payload), frame);
    Serial1.write(frame, n);
}

void SerialProtocol::onFrame(void* userData, uint8_t type, const uint8_t* payload, uint8_t len) {
    static_cast<SerialProtocol*>(userData)->handleFrame(type, payload, len);
}

void SerialProtocol::handleFrame(uint8_t type, const uint8_t* payload, uint8_t len) {
    if (ledController_ == nullptr) {
        return;
    }

    if (type == protocol::kMsgLedSet && len >= sizeof(protocol::LedSetPayload)) {
        const auto* p = reinterpret_cast<const protocol::LedSetPayload*>(payload);
        ledController_->setTrack(p->trackId, static_cast<protocol::LedColor>(p->color), p->blink != 0);
    } else if (type == protocol::kMsgLedSetAll && len >= sizeof(protocol::LedSetAllPayload)) {
        const auto* p = reinterpret_cast<const protocol::LedSetAllPayload*>(payload);
        for (uint8_t i = 0; i < 4; ++i) {
            ledController_->setTrack(i, static_cast<protocol::LedColor>(p->color[i]), p->blink[i] != 0);
        }
    }
}
