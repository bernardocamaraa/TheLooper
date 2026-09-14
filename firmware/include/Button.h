// Debounce + classificacao de gestos (press / long-press) para um footswitch.
//
// Botoes sem suporte a long-press (todos exceto UNDO) disparam kEventPress no
// exato instante (debounced) do press-down, para latencia minima em uso
// musical. O UNDO precisa distinguir short-press de long-press (3s = reset
// geral): nesse caso o evento so e emitido no release (curto) ou no instante
// em que o hold cruza o threshold (longo, ainda pressionado) - nunca os dois.
#pragma once

#include <Arduino.h>

enum class ButtonEvent : uint8_t { kEventNone, kEventPress, kEventLongPress };

class Button {
public:
    // longPressMs = 0 desativa a classificacao de long-press para este botao.
    void begin(uint8_t pin, uint16_t longPressMs = 0);

    // Chamar a cada iteracao de loop(). Retorna no maximo um evento por chamada.
    ButtonEvent update(uint32_t nowMs);

    // true enquanto o botao esta segurado alem de armThresholdMs mas ainda
    // nao cruzou o proprio longPressMs (usado para o aviso visual de reset).
    bool isArmed(uint32_t nowMs, uint16_t armThresholdMs) const;

    bool isPressed() const { return stableState_; }

private:
    uint8_t pin_ = 0;
    uint16_t longPressMs_ = 0;

    bool lastRaw_ = false;
    bool stableState_ = false;
    uint32_t lastChangeMs_ = 0;
    uint32_t pressStartMs_ = 0;
    bool longPressFired_ = false;
};
