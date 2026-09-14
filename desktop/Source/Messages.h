// Tipos de mensagem trocados entre threads via SpscQueue (ver SpscQueue.h).
// Mantidos deliberadamente pequenos e trivialmente copiaveis (sem alocacao).
#pragma once

#include <array>

#include "Config.h"
#include "protocol.h"

// Um frame de audio (kNumChannels amostras), usado no ring buffer entre o
// callback ASIO principal e a stream de saida do microfone virtual.
using AudioFrame = std::array<float, config::kNumChannels>;

// SerialLink (produtor) -> AudioEngine/LooperEngine (consumidor, dentro do
// callback de audio).
struct ButtonEventMsg {
    protocol::ButtonId buttonId = protocol::kButtonRecPlay;
    protocol::Gesture gesture = protocol::kGesturePress;
};

// LooperEngine (produtor, dentro do callback de audio) -> SerialLink
// (consumidor).
struct LedCommand {
    int trackId = 0;
    protocol::LedColor color = protocol::kLedOff;
    bool blink = false;
};
