#include "Button.h"
#include "Config.h"

void Button::begin(uint8_t pin, uint16_t longPressMs) {
    pin_ = pin;
    longPressMs_ = longPressMs;
    pinMode(pin_, INPUT_PULLUP);
    lastRaw_ = false;
    stableState_ = false;
    lastChangeMs_ = millis();
    pressStartMs_ = 0;
    longPressFired_ = false;
}

ButtonEvent Button::update(uint32_t nowMs) {
    // INPUT_PULLUP: o pino fica LOW quando o footswitch fecha o circuito para GND.
    bool raw = (digitalRead(pin_) == LOW);

    if (raw != lastRaw_) {
        lastChangeMs_ = nowMs;
        lastRaw_ = raw;
    }

    bool supportsLongPress = (longPressMs_ > 0);

    if ((nowMs - lastChangeMs_) >= config::kDebounceMs && raw != stableState_) {
        stableState_ = raw;

        if (stableState_) {
            // Borda de press-down (debounced).
            pressStartMs_ = nowMs;
            longPressFired_ = false;
            if (!supportsLongPress) {
                return ButtonEvent::kEventPress;
            }
        } else {
            // Borda de release (debounced).
            if (supportsLongPress && !longPressFired_) {
                return ButtonEvent::kEventPress;
            }
        }
    }

    if (supportsLongPress && stableState_ && !longPressFired_) {
        if ((nowMs - pressStartMs_) >= longPressMs_) {
            longPressFired_ = true;
            return ButtonEvent::kEventLongPress;
        }
    }

    return ButtonEvent::kEventNone;
}

bool Button::isArmed(uint32_t nowMs, uint16_t armThresholdMs) const {
    if (longPressMs_ == 0 || !stableState_ || longPressFired_) {
        return false;
    }
    return (nowMs - pressStartMs_) >= armThresholdMs;
}
