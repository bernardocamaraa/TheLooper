#include "LedController.h"
#include "Config.h"

void LedController::begin() {
    for (uint8_t i = 0; i < 4; ++i) {
        pinMode(config::kPinLedRed[i], OUTPUT);
        pinMode(config::kPinLedGreen[i], OUTPUT);
        writePins(i, false, false);
    }
    lastBlinkToggleMs_ = millis();
}

void LedController::setTrack(uint8_t track, protocol::LedColor color, bool blink) {
    if (track >= 4) {
        return;
    }
    tracks_[track].color = color;
    tracks_[track].blink = blink;
}

void LedController::setResetWarning(bool active) {
    resetWarningActive_ = active;
}

void LedController::update(uint32_t nowMs) {
    if ((nowMs - lastBlinkToggleMs_) >= config::kLedBlinkIntervalMs) {
        lastBlinkToggleMs_ = nowMs;
        blinkPhaseOn_ = !blinkPhaseOn_;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (resetWarningActive_) {
            writePins(i, blinkPhaseOn_, false);
            continue;
        }

        const TrackLedState& t = tracks_[i];
        bool show = !t.blink || blinkPhaseOn_;
        bool red = show && (t.color == protocol::kLedRed || t.color == protocol::kLedOrange);
        bool green = show && (t.color == protocol::kLedGreen || t.color == protocol::kLedOrange);
        writePins(i, red, green);
    }
}

void LedController::writePins(uint8_t track, bool red, bool green) {
    digitalWrite(config::kPinLedRed[track], red ? HIGH : LOW);
    digitalWrite(config::kPinLedGreen[track], green ? HIGH : LOW);
}
