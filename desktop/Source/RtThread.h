// Defesas contra o Windows tratar este app como um programa qualquer.
//
// O sintoma que motivou isto: "toda vez que minimizo o app, o audio fica
// estranho e mais lento". O Windows 11 aplica EcoQoS (modo de eficiencia) a
// processos cujas janelas estao todas minimizadas - ele derruba a frequencia
// de CPU das threads do processo para poupar bateria. Isso e otimo para um
// editor de texto e desastroso para audio: as threads de callback do ASIO e do
// WASAPI vivem DENTRO deste processo, entao elas perdem o prazo do bloco e o
// som sai picotado.
//
// Duas defesas, as duas padrao para aplicativo de audio no Windows:
//
// 1. O processo sai do estrangulamento por completo (SetProcessInformation).
// 2. Cada thread de callback se registra no MMCSS como "Pro Audio", que e o
//    mecanismo oficial do Windows para dizer "esta thread tem prazo real".
//    Thread registrada no MMCSS nao e estrangulada em segundo plano.
#pragma once

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <avrt.h>
 #pragma comment(lib, "avrt.lib")
#endif

namespace rt {

#if JUCE_WINDOWS
namespace detail {
// A inscricao vive na propria thread e e desfeita quando ela morre - o
// destrutor de um thread_local roda na saida da thread, entao o handle do
// MMCSS nao vaza quando o device de audio e fechado e reaberto.
struct ProAudioRegistration {
    HANDLE handle = nullptr;
    bool tried = false;

    ~ProAudioRegistration() {
        if (handle != nullptr) {
            AvRevertMmThreadCharacteristics(handle);
        }
    }
};
} // namespace detail
#endif

// Chamada no TOPO de cada callback de audio. Faz trabalho de verdade uma vez
// so por thread; nas outras chamadas e um teste de bool, que e RT-safe.
inline void joinProAudio() {
#if JUCE_WINDOWS
    static thread_local detail::ProAudioRegistration registration;
    if (registration.tried) {
        return;
    }
    registration.tried = true; // marca antes: se falhar, nao tenta a cada bloco
    DWORD taskIndex = 0;
    registration.handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
#endif
}

// Chamada uma vez, na abertura do app.
inline void disableProcessPowerThrottling() {
#if JUCE_WINDOWS
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0; // 0 com a mascara ligada = "nao estrangule este processo"
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
#endif
}

} // namespace rt
