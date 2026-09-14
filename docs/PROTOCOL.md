# Protocolo serial Mega ↔ PC

Fonte de verdade em código: `shared/protocol.h` (incluído por firmware e
desktop). Este documento é a versão em prosa para consulta rápida.

## Framing

```
[SOF 0xAA][LEN][TYPE][PAYLOAD...LEN bytes][CRC8][EOF 0x55]
```

- Baud rate: 115200.
- `LEN`: tamanho do payload, 0–8 bytes.
- `CRC8`: poly 0x07, init 0x00, calculado sobre `TYPE + PAYLOAD`.
- O parser (`protocol::FrameParser`) é uma state machine que, em caso de
  falha de CRC ou de `EOF` inesperado, descarta 1 byte e volta a procurar o
  próximo `SOF` — nunca trava, resincroniza sozinho após ruído na linha ou
  reconexão a meio de um frame.

## Mensagens Mega → PC

| Tipo | Valor | Payload | Descrição |
|---|---|---|---|
| `MSG_BUTTON_EVENT` | 0x01 | `[buttonId][gesture]` | Evento de botão classificado |
| `MSG_HEARTBEAT` | 0x02 | (vazio) | Enviado a cada 500ms |
| `MSG_FIRMWARE_HELLO` | 0x03 | `[firmwareVersion]` | Enviado uma vez no boot/reset do Mega |

`buttonId`: REC_PLAY=0, PAUSE=1, UNDO=2, MODE=3, TRACK1..4=4..7.
`gesture`: PRESS=0 (todos os botões, disparado no press-down para latência
mínima), LONG_PRESS=1 (só emitido pelo UNDO, hold de 3s).

## Mensagens PC → Mega

| Tipo | Valor | Payload | Descrição |
|---|---|---|---|
| `MSG_LED_SET` | 0x10 | `[trackId][color][blink]` | Atualiza o LED de uma track |
| `MSG_LED_SET_ALL` | 0x11 | `[color×4][blink×4]` | Resync completo dos 4 LEDs |

`color`: OFF=0, RED=1, GREEN=2, ORANGE=3 (ORANGE existe no hardware mas não é
usado pela FSM atual — ver `docs/CONTROL_MODEL.md`).

## Robustez / reconexão

- Sem ACK por mensagem individual — a robustez vem da resincronização
  automática do parser somada ao `MSG_LED_SET_ALL` periódico (a cada 2s) e
  imediato após um `MSG_FIRMWARE_HELLO` (reconexão/reset do Mega).
- O PC considera o Mega desconectado se nenhum `MSG_HEARTBEAT`/`MSG_FIRMWARE_HELLO`
  chegar por mais de 2s, fecha a porta COM e tenta reabrir periodicamente.
- A porta COM é auto-detectada pelo VID:PID do chip USB-serial do Mega
  (`config::kArduinoMegaVendorId`/`kArduinoMegaProductId` em
  `desktop/Source/Config.h`) via SetupAPI; se a auto-detecção falhar, defina
  `config::kComPortNameOverride` com o nome fixo da porta (ex: `"COM5"`).
