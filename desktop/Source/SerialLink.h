// Thread dedicada de comunicacao com o Arduino Mega via porta serial (COM).
// Le MSG_BUTTON_EVENT / MSG_HEARTBEAT / MSG_FIRMWARE_HELLO do firmware e
// alimenta a fila SPSC consumida pelo AudioEngine; drena a fila de
// LedCommand produzida pelo AudioEngine e envia MSG_LED_SET, alem de reenviar
// periodicamente um MSG_LED_SET_ALL completo (resync) e imediatamente apos
// reconectar - ver software/docs/PROTOCOL.md.
//
// Implementacao especifica de Windows (Win32 API para a porta COM e SetupAPI
// para auto-detectar o Mega por VID:PID) - ver SerialLink.cpp.
#pragma once

#include <atomic>

#include <juce_core/juce_core.h>
#include <cstdint>
#include <thread>

#include "Config.h"
#include "Messages.h"
#include "SpscQueue.h"
#include "protocol.h"

class SerialLink {
public:
    SerialLink(SpscQueue<ButtonEventMsg, 64>& outgoingButtonEvents,
               SpscQueue<LedCommand, 64>& incomingLedCommands);
    ~SerialLink();

    // portOverride vazio = auto-detectar por VID:PID. Lido UMA vez aqui, e
    // nao guardado como estado mutavel: trocar a porta exige stop()+start(),
    // o que evita uma string sendo alterada enquanto a thread serial a le.
    void start(const juce::String& portOverride = {});
    void stop();

    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }

private:
    void threadMain();
    void tryConnect();
    void handleFrame(uint8_t type, const uint8_t* payload, uint8_t len);
    static void onFrameStatic(void* userData, uint8_t type, const uint8_t* payload, uint8_t len);
    void sendLedSet(const LedCommand& cmd);
    void sendLedSetAll();

    SpscQueue<ButtonEventMsg, 64>& outgoingButtonEvents_;
    SpscQueue<LedCommand, 64>& incomingLedCommands_;

    struct Impl; // esconde os detalhes de Win32 (HANDLE etc.) deste header
    Impl* impl_;

    protocol::FrameParser parser_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread thread_;

    juce::String portOverride_;

    uint32_t lastHeartbeatSeenMs_ = 0;
    uint32_t lastLedResyncMs_ = 0;
    protocol::LedColor lastKnownColor_[config::kNumTracks] = {};
    bool lastKnownBlink_[config::kNumTracks] = {};
};
