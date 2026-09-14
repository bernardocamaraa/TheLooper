// Protocolo serial Mega <-> PC. Fonte unica de verdade: incluido tanto pelo
// firmware (Arduino) quanto pelo app desktop (JUCE), para que os dois lados
// nunca percam sincronia sobre o layout de mensagens.
//
// Framing: [SOF 0xAA][LEN][TYPE][PAYLOAD...LEN bytes][CRC8][EOF 0x55]
// CRC8 calculado sobre TYPE + PAYLOAD (poly 0x07, init 0x00).
#pragma once

#include <stdint.h>

namespace protocol {

constexpr uint8_t kStartOfFrame = 0xAA;
constexpr uint8_t kEndOfFrame = 0x55;
constexpr uint8_t kMaxPayloadSize = 8;
constexpr uint32_t kBaudRate = 115200;
constexpr uint32_t kHeartbeatIntervalMs = 500;
// Folgado o bastante para tolerar heartbeats perdidos no link Bluetooth (SPP
// as vezes derruba 1-2 pacotes isolados) sem disparar reconexao, que numa
// porta COM Bluetooth e bem mais lenta de reabrir que numa USB direta.
constexpr uint32_t kHeartbeatTimeoutMs = 5000;
constexpr uint32_t kLedResyncIntervalMs = 2000;

// Mega -> PC
constexpr uint8_t kMsgButtonEvent = 0x01;
constexpr uint8_t kMsgHeartbeat = 0x02;
constexpr uint8_t kMsgFirmwareHello = 0x03;

// PC -> Mega
constexpr uint8_t kMsgLedSet = 0x10;
constexpr uint8_t kMsgLedSetAll = 0x11;

constexpr uint8_t kFirmwareVersion = 1;

// IDs dos 8 footswitches fisicos.
enum ButtonId : uint8_t {
    kButtonRecPlay = 0,
    kButtonPause = 1,
    kButtonUndo = 2,
    kButtonMode = 3,
    kButtonTrack1 = 4,
    kButtonTrack2 = 5,
    kButtonTrack3 = 6,
    kButtonTrack4 = 7,
    kButtonCount = 8
};

// Gestos classificados pelo firmware. Todos os botoes emitem so kGesturePress
// (disparado no press-down, latencia minima) exceto UNDO, que tambem pode
// emitir kGestureLongPress (hold de 3s, ver kUndoLongPressMs).
enum Gesture : uint8_t {
    kGesturePress = 0,
    kGestureLongPress = 1
};

// Cores de LED. So RED e GREEN sao usados pela FSM (ver docs/CONTROL_MODEL.md);
// ORANGE existe no hardware (vermelho+verde simultaneos) mas nao e usado pela
// logica atual - mantido no protocolo para uso futuro.
enum LedColor : uint8_t {
    kLedOff = 0,
    kLedRed = 1,
    kLedGreen = 2,
    kLedOrange = 3
};

#pragma pack(push, 1)
struct ButtonEventPayload {
    uint8_t buttonId;
    uint8_t gesture;
};

struct LedSetPayload {
    uint8_t trackId;
    uint8_t color;
    uint8_t blink; // 0 = solido, 1 = piscando
};

struct LedSetAllPayload {
    uint8_t color[4];
    uint8_t blink[4];
};

struct FirmwareHelloPayload {
    uint8_t firmwareVersion;
};
#pragma pack(pop)

// CRC-8, poly 0x07, init 0x00. Implementacao simples byte-a-byte (sem tabela)
// pois o volume de dados por frame e minusculo (no maximo ~10 bytes).
inline uint8_t crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x07);
            } else {
                crc = static_cast<uint8_t>(crc << 1);
            }
        }
    }
    return crc;
}

// Parser de frames compartilhado entre firmware e desktop: alimentado byte a
// byte (bytesReceived vindos da UART/porta serial), sem alocacao dinamica,
// sem STL alem de <stdint.h>. Em caso de CRC/EOF invalido, descarta 1 byte e
// volta a procurar o proximo SOF - nunca trava, resincroniza sozinho.
class FrameParser {
public:
    using FrameHandler = void (*)(void* userData, uint8_t type, const uint8_t* payload, uint8_t len);

    void setHandler(FrameHandler handler, void* userData) {
        handler_ = handler;
        userData_ = userData;
    }

    void feed(uint8_t byte) {
        switch (state_) {
            case State::WaitSof:
                if (byte == kStartOfFrame) {
                    state_ = State::WaitLen;
                }
                break;
            case State::WaitLen:
                if (byte > kMaxPayloadSize) {
                    state_ = State::WaitSof;
                    break;
                }
                len_ = byte;
                state_ = State::WaitType;
                break;
            case State::WaitType:
                type_ = byte;
                payloadIndex_ = 0;
                state_ = (len_ == 0) ? State::WaitCrc : State::WaitPayload;
                break;
            case State::WaitPayload:
                payload_[payloadIndex_++] = byte;
                if (payloadIndex_ >= len_) {
                    state_ = State::WaitCrc;
                }
                break;
            case State::WaitCrc: {
                uint8_t computed = computeCrc();
                if (computed != byte) {
                    state_ = State::WaitSof;
                    break;
                }
                state_ = State::WaitEof;
                break;
            }
            case State::WaitEof:
                if (byte == kEndOfFrame && handler_ != nullptr) {
                    handler_(userData_, type_, payload_, len_);
                }
                state_ = State::WaitSof;
                break;
        }
    }

private:
    enum class State { WaitSof, WaitLen, WaitType, WaitPayload, WaitCrc, WaitEof };

    uint8_t computeCrc() const {
        // CRC cobre TYPE + PAYLOAD - monta um buffer temporario pequeno.
        uint8_t buf[1 + kMaxPayloadSize];
        buf[0] = type_;
        for (uint8_t i = 0; i < len_; ++i) {
            buf[1 + i] = payload_[i];
        }
        return crc8(buf, static_cast<uint8_t>(1 + len_));
    }

    State state_ = State::WaitSof;
    uint8_t len_ = 0;
    uint8_t type_ = 0;
    uint8_t payloadIndex_ = 0;
    uint8_t payload_[kMaxPayloadSize] = {0};
    FrameHandler handler_ = nullptr;
    void* userData_ = nullptr;
};

// Monta um frame completo em `out` (deve ter espaco para 5 + len bytes) e
// retorna o numero de bytes escritos. Usado pelos dois lados para enviar.
inline uint8_t encodeFrame(uint8_t type, const uint8_t* payload, uint8_t len, uint8_t* out) {
    uint8_t buf[1 + kMaxPayloadSize];
    buf[0] = type;
    for (uint8_t i = 0; i < len; ++i) {
        buf[1 + i] = payload[i];
    }
    uint8_t crc = crc8(buf, static_cast<uint8_t>(1 + len));

    uint8_t idx = 0;
    out[idx++] = kStartOfFrame;
    out[idx++] = len;
    out[idx++] = type;
    for (uint8_t i = 0; i < len; ++i) {
        out[idx++] = payload[i];
    }
    out[idx++] = crc;
    out[idx++] = kEndOfFrame;
    return idx;
}

} // namespace protocol
