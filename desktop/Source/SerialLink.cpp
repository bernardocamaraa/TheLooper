#include "SerialLink.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include <windows.h>
#include <setupapi.h>

#include <juce_core/juce_core.h>

#pragma comment(lib, "setupapi.lib")

namespace {

// Procura, entre as portas COM presentes, aquela cujo hardware ID contenha
// o VID:PID informado (formato tipico: "VID_2341&PID_0042"). Retorna string
// vazia se nao encontrar - o chamador cai de volta para kComPortNameOverride.
juce::String findComPortByVidPid(uint16_t vid, uint16_t pid) {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(nullptr, "USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return {};
    }

    char needle[32];
    std::snprintf(needle, sizeof(needle), "VID_%04X&PID_%04X", vid, pid);

    juce::String result;
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &devInfoData); ++i) {
        char hardwareId[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &devInfoData, SPDRP_HARDWAREID, nullptr,
                                                reinterpret_cast<PBYTE>(hardwareId), sizeof(hardwareId) - 1,
                                                nullptr)) {
            continue;
        }
        if (!juce::String(hardwareId).containsIgnoreCase(needle)) {
            continue;
        }

        char friendlyName[512] = {};
        if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &devInfoData, SPDRP_FRIENDLYNAME, nullptr,
                                               reinterpret_cast<PBYTE>(friendlyName), sizeof(friendlyName) - 1,
                                               nullptr)) {
            juce::String fname(friendlyName);
            int openParen = fname.lastIndexOfChar('(');
            int closeParen = fname.lastIndexOfChar(')');
            if (openParen >= 0 && closeParen > openParen) {
                result = fname.substring(openParen + 1, closeParen); // ex: "COM5"
                break;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return result;
}

} // namespace

// Detalhes de Win32 escondidos do header (evita vazar <windows.h> para quem
// inclui SerialLink.h).
struct SerialLink::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;

    bool isOpen() const { return handle != INVALID_HANDLE_VALUE; }

    bool open(const juce::String& portName, uint32_t baudRate) {
        close();
        juce::String path = "\\\\.\\" + portName;
        handle = CreateFileA(path.toRawUTF8(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }

        DCB dcb = {};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(handle, &dcb)) {
            close();
            return false;
        }
        dcb.BaudRate = baudRate;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        if (!SetCommState(handle, &dcb)) {
            close();
            return false;
        }

        // Timeouts configurados para leitura "nao-bloqueante": ReadFile
        // retorna imediatamente com o que estiver disponivel (mesmo 0 bytes).
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        SetCommTimeouts(handle, &timeouts);

        return true;
    }

    void close() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }

    int readAvailable(uint8_t* buffer, DWORD maxLen) {
        if (!isOpen()) {
            return 0;
        }
        DWORD bytesRead = 0;
        if (!ReadFile(handle, buffer, maxLen, &bytesRead, nullptr)) {
            close(); // erro de I/O (ex: cabo desconectado) - forca reconexao
            return 0;
        }
        return static_cast<int>(bytesRead);
    }

    bool write(const uint8_t* data, DWORD len) {
        if (!isOpen()) {
            return false;
        }
        DWORD bytesWritten = 0;
        if (!WriteFile(handle, data, len, &bytesWritten, nullptr)) {
            close();
            return false;
        }
        return bytesWritten == len;
    }
};

SerialLink::SerialLink(SpscQueue<ButtonEventMsg, 64>& outgoingButtonEvents,
                        SpscQueue<LedCommand, 64>& incomingLedCommands)
    : outgoingButtonEvents_(outgoingButtonEvents), incomingLedCommands_(incomingLedCommands), impl_(new Impl()) {
    parser_.setHandler(&SerialLink::onFrameStatic, this);
}

SerialLink::~SerialLink() {
    stop();
    delete impl_;
}

void SerialLink::start(const juce::String& portOverride) {
    if (running_.exchange(true)) {
        return;
    }
    // Copiado ANTES da thread comecar: dai em diante so a thread serial le
    // este campo, entao nao ha corrida.
    portOverride_ = portOverride.trim();
    thread_ = std::thread(&SerialLink::threadMain, this);
}

void SerialLink::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    impl_->close();
}

void SerialLink::tryConnect() {
    juce::String portName =
        portOverride_.isNotEmpty()
            ? portOverride_
            : findComPortByVidPid(config::kArduinoMegaVendorId, config::kArduinoMegaProductId);
    if (portName.isEmpty()) {
        return;
    }
    if (impl_->open(portName, protocol::kBaudRate)) {
        lastHeartbeatSeenMs_ = juce::Time::getMillisecondCounter(); // evita timeout imediato pos-conexao
    }
}

void SerialLink::threadMain() {
    while (running_.load(std::memory_order_relaxed)) {
        if (!impl_->isOpen()) {
            tryConnect();
            if (!impl_->isOpen()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        uint8_t buf[256];
        int n = impl_->readAvailable(buf, sizeof(buf));
        for (int i = 0; i < n; ++i) {
            parser_.feed(buf[i]);
        }

        const uint32_t now = juce::Time::getMillisecondCounter();
        if (connected_.load(std::memory_order_relaxed) && (now - lastHeartbeatSeenMs_) > protocol::kHeartbeatTimeoutMs) {
            connected_.store(false, std::memory_order_relaxed);
            impl_->close(); // forca uma nova tentativa de conexao
        }

        LedCommand cmd;
        while (incomingLedCommands_.pop(cmd)) {
            lastKnownColor_[cmd.trackId] = cmd.color;
            lastKnownBlink_[cmd.trackId] = cmd.blink;
            sendLedSet(cmd);
        }

        if ((now - lastLedResyncMs_) >= protocol::kLedResyncIntervalMs) {
            lastLedResyncMs_ = now;
            sendLedSetAll();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void SerialLink::onFrameStatic(void* userData, uint8_t type, const uint8_t* payload, uint8_t len) {
    static_cast<SerialLink*>(userData)->handleFrame(type, payload, len);
}

void SerialLink::handleFrame(uint8_t type, const uint8_t* payload, uint8_t len) {
    const uint32_t now = juce::Time::getMillisecondCounter();

    if (type == protocol::kMsgHeartbeat) {
        connected_.store(true, std::memory_order_relaxed);
        lastHeartbeatSeenMs_ = now;
    } else if (type == protocol::kMsgFirmwareHello) {
        connected_.store(true, std::memory_order_relaxed);
        lastHeartbeatSeenMs_ = now;
        sendLedSetAll(); // resync imediato apos boot/reset do firmware
    } else if (type == protocol::kMsgButtonEvent && len >= sizeof(protocol::ButtonEventPayload)) {
        const auto* p = reinterpret_cast<const protocol::ButtonEventPayload*>(payload);
        ButtonEventMsg msg;
        msg.buttonId = static_cast<protocol::ButtonId>(p->buttonId);
        msg.gesture = static_cast<protocol::Gesture>(p->gesture);
        outgoingButtonEvents_.push(msg); // best-effort (fila cheia = evento raro perdido)
    }
}

void SerialLink::sendLedSet(const LedCommand& cmd) {
    protocol::LedSetPayload payload{static_cast<uint8_t>(cmd.trackId), static_cast<uint8_t>(cmd.color),
                                     static_cast<uint8_t>(cmd.blink ? 1 : 0)};
    uint8_t frame[5 + sizeof(payload)];
    uint8_t n = protocol::encodeFrame(protocol::kMsgLedSet, reinterpret_cast<const uint8_t*>(&payload),
                                       sizeof(payload), frame);
    impl_->write(frame, n);
}

void SerialLink::sendLedSetAll() {
    protocol::LedSetAllPayload payload{};
    for (int i = 0; i < config::kNumTracks; ++i) {
        payload.color[i] = static_cast<uint8_t>(lastKnownColor_[i]);
        payload.blink[i] = static_cast<uint8_t>(lastKnownBlink_[i] ? 1 : 0);
    }
    uint8_t frame[5 + sizeof(payload)];
    uint8_t n = protocol::encodeFrame(protocol::kMsgLedSetAll, reinterpret_cast<const uint8_t*>(&payload),
                                       sizeof(payload), frame);
    impl_->write(frame, n);
}
