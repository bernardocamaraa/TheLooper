// Configuracao de hardware do firmware. Pinos extraidos do firmware MIDI
// anterior do pedal (codigopedalV9.ino, que o usuario confirmou refletir a
// fiacao real) - ver mapeamento de nomes abaixo.
#pragma once

#include <Arduino.h>

namespace config {

// ---------------------------------------------------------------------------
// PINOS DOS FOOTSWITCHES (entrada digital, INPUT_PULLUP - botao liga o pino
// ao GND quando pressionado).
//
// Mapeamento para os nomes do firmware antigo: kPinRecPlay=BTN_PLAY,
// kPinPause=BTN_PAUSE (agora com funcao de STOP simples, sem hold - ver
// docs/CONTROL_MODEL.md), kPinUndo=BTN_CLEAR (o botao fisico e rotulado/fiado
// como "CLEAR": short=remove ultima camada, long=Clear All/reseta tudo).
// BTN_RESET (pino 6) do firmware antigo nunca existiu de fato no hardware
// (era so uma ideia) - nao esta definido aqui, o pino 6 esta livre.
// ---------------------------------------------------------------------------
constexpr uint8_t kPinRecPlay = 3;
constexpr uint8_t kPinPause = 5;
constexpr uint8_t kPinUndo = 4;   // fiado/rotulado como "CLEAR" no pedal
constexpr uint8_t kPinMode = 11;
constexpr uint8_t kPinTrack1 = 7;
constexpr uint8_t kPinTrack2 = 8;
constexpr uint8_t kPinTrack3 = 9;
constexpr uint8_t kPinTrack4 = 10;

// ---------------------------------------------------------------------------
// PINOS DOS LEDS BICOLORES (saida digital - um pino GREEN e um pino RED por
// track; acender os dois simultaneamente produz laranja no hardware, mas a
// FSM atual so usa RED puro ou GREEN puro - ver docs/CONTROL_MODEL.md).
//
// LED_REC_RED e LED_VOLUME_BLUE do firmware antigo (pinos 12/13) tambem
// nunca existiram de fato no hardware - nao tem equivalente aqui, pinos
// 12/13 estao livres.
// ---------------------------------------------------------------------------
constexpr uint8_t kPinLedGreen[4] = {30, 32, 34, 36};
constexpr uint8_t kPinLedRed[4] = {31, 33, 35, 37};

// ---------------------------------------------------------------------------
// TEMPOS
// ---------------------------------------------------------------------------
constexpr uint16_t kDebounceMs = 20;

// UNDO (fisicamente "CLEAR"): hold = Clear All (limpa o pedal inteiro,
// incluindo o comprimento do loop). Acao mais destrutiva do pedal, por isso
// o aviso piscando antes de disparar.
constexpr uint16_t kClearAllHoldMs = 3000;
constexpr uint16_t kClearAllArmWarningMs = 1500; // a partir daqui, LEDs piscam vermelho avisando o Clear All iminente

constexpr uint16_t kLedBlinkIntervalMs = 300; // piscar do aviso de Clear All

} // namespace config
