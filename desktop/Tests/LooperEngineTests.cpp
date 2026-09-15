// Testes do motor de camadas infinitas, sem device de audio nem JUCE.
//
// O teste dirige a LooperEngine como o callback de audio faria (serviceBlock a
// cada 256 frames, processFrame por frame, botoes entre os blocos) com audio
// sintetico, e mantem em paralelo o que CADA camada deveria ter gravado -
// seguindo as mesmas regras do motor para o que conta como camada (ver
// config::kSilentLayerPeak). Depois de cada gravacao e de cada desfazer, a soma
// da track (readTrackMix) tem de bater amostra por amostra com a soma das
// camadas que sobraram, e o numero de camadas tambem.
//
// Rodar: build/Release/LooperEngineTests.exe  (sai com codigo 1 se algo falhar)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "LooperEngine.h"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int64_t kLoopFrames = 4800; // 0,1 s: rapido, mas com loop de verdade
int failures = 0;

void expect(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " ok " : "FALHOU", what);
    if (!ok) {
        ++failures;
    }
}

// Sinal diferente por camada, com pico de 0,01 (bem acima do limiar de
// silencio): uma camada trocada por outra apareceria na comparacao.
float signal(int seed, int64_t frame) {
    const int v = static_cast<int>((seed * 131 + frame * 7) % 29) - 14;
    return 0.01f * static_cast<float>(v) / 14.0f;
}

class Harness {
public:
    Harness() {
        engine.prepare();
        engine.setAudioDeviceInfo(kSampleRate, 0); // latencia 0: grava onde le
        waitForSpares();
    }

    void press(protocol::ButtonId button, protocol::Gesture gesture = protocol::kGesturePress) {
        const int before = pass_.track;
        const bool loopWasDefined = engine.masterLoopLength() > 0;
        engine.handleButtonEvent(button, gesture);

        if (before >= 0) {
            const bool stillRecording = engine.trackState(before) == TrackState::RECORDING;
            // Fechar a base abre um overdub na mesma track: o passe mudou mesmo
            // com a track continuando em RECORDING.
            const bool baseJustClosed = !loopWasDefined && engine.masterLoopLength() > 0;
            if (!stillRecording || baseJustClosed) {
                const bool cancelled = (button == protocol::kButtonUndo && gesture == protocol::kGesturePress);
                if (!cancelled) {
                    finishPass();
                }
                pass_ = Pass{};
            }
        }
        if (pass_.track < 0) {
            for (int t = 0; t < config::kNumTracks; ++t) {
                if (engine.trackState(t) == TrackState::RECORDING) {
                    pass_.track = t;
                    pass_.base = engine.masterLoopLength() == 0;
                    break;
                }
            }
        }
        if (humanTiming) {
            breathe();
        }
    }

    // Roda `frames` frames como o callback faria, com o sinal `seed` (vezes
    // gain) na entrada - e anota o que a track que grava deveria guardar.
    void run(int64_t frames, int seed, float gain = 1.0f) {
        for (int64_t i = 0; i < frames; ++i) {
            if ((blockCounter_++ % 256) == 0) {
                engine.serviceBlock();
            }
            const float v = signal(seed, i) * gain;
            if (pass_.track >= 0) {
                record(v);
            }
            const float in[config::kNumChannels] = {v, v};
            float out[config::kNumChannels] = {};
            engine.processFrame(in, out);
        }
    }

    // Entrada constante, com nada gravando (para o teste do VU e para andar o
    // transporte).
    void runInput(int64_t frames, float value) {
        for (int64_t i = 0; i < frames; ++i) {
            if ((blockCounter_++ % 256) == 0) {
                engine.serviceBlock();
            }
            const float in[config::kNumChannels] = {value, value};
            float out[config::kNumChannels] = {};
            engine.processFrame(in, out);
        }
    }

    // Deixa o tempo passar ate nenhum desfazer estar em andamento (as camadas
    // em disco precisam da thread do LayerStore).
    bool settle() {
        for (int attempt = 0; attempt < 4000; ++attempt) {
            bool settled = true;
            for (int t = 0; t < config::kNumTracks; ++t) {
                settled = settled && engine.trackLayersSettled(t);
            }
            if (settled) {
                return true;
            }
            runInput(256, 0.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    // Simula o tempo real entre dois passes (a thread do LayerStore trabalha).
    void breathe() { std::this_thread::sleep_for(std::chrono::milliseconds(3)); }

    void undoExpected(int track) {
        if (!layers_[track].empty()) {
            layers_[track].pop_back();
        }
    }
    void clearExpected() {
        for (auto& list : layers_) {
            list.clear();
        }
    }
    void addExpectedLayer(int track, std::vector<float> layer) { layers_[track].push_back(std::move(layer)); }
    size_t expectedLayers(int track) const { return layers_[track].size(); }

    // A soma da track bate com a soma das camadas que deveriam existir?
    bool matches(int track) {
        const int64_t length = engine.masterLoopLength();
        if (length <= 0) {
            return layers_[track].empty();
        }
        std::vector<float> got(static_cast<size_t>(length) * 2);
        engine.readTrackMix(track, got.data(), length);
        std::vector<float> want(got.size(), 0.0f);
        for (const auto& layer : layers_[track]) {
            for (size_t i = 0; i < want.size() && i < layer.size(); ++i) {
                want[i] += layer[i];
            }
        }
        double worst = 0.0;
        for (size_t i = 0; i < got.size(); ++i) {
            worst = std::max(worst, static_cast<double>(std::fabs(got[i] - want[i])));
        }
        const bool countOk = engine.trackLayers(track) == static_cast<int>(layers_[track].size());
        if (worst > 1e-4 || !countOk) {
            std::printf("    track %d: diferenca maxima %.6g, camadas %d (esperado %zu)\n", track, worst,
                        engine.trackLayers(track), layers_[track].size());
        }
        return worst <= 1e-4 && countOk;
    }

    bool allMatch() {
        bool ok = true;
        for (int t = 0; t < config::kNumTracks; ++t) {
            ok = matches(t) && ok;
        }
        return ok;
    }

    LooperEngine engine;
    // Espera um pouco depois de cada botao, como uma pessoa: sem isso o teste
    // aperta mais rapido do que a thread do LayerStore repoe os buffers.
    bool humanTiming = false;

private:
    // O passe em andamento, espelhando o que o motor faz (ver
    // AudioTrack::closeCapture / splitCapture).
    struct Pass {
        int track = -1;
        bool base = false; // o passe que define o loop
        bool started = false;
        size_t startPos = 0;
        int laps = 0;
        int64_t lapFrames = 0;
        float lapPeak = 0.0f;
        int64_t passFrames = 0;
        float passPeak = 0.0f;
        std::vector<float> current;
    };

    static std::vector<float> padded(std::vector<float> layer, int64_t length) {
        layer.resize(static_cast<size_t>(length) * 2, 0.0f);
        return layer;
    }

    void record(float v) {
        const auto pos = static_cast<size_t>(std::llround(engine.loopPositionSeconds() * kSampleRate));
        const int64_t length = engine.masterLoopLength();
        if (!pass_.started) {
            pass_.started = true;
            pass_.startPos = pos;
        } else if (!pass_.base && length > 0 && pos == pass_.startPos) {
            // Uma volta inteira desde o inicio do passe.
            if (pass_.lapPeak < config::kSilentLayerPeak) {
                pass_.lapFrames = 0; // volta muda: nao vira camada
                pass_.lapPeak = 0.0f;
            } else {
                layers_[pass_.track].push_back(padded(std::move(pass_.current), length));
                pass_.current.clear();
                ++pass_.laps;
                pass_.lapFrames = 0;
                pass_.lapPeak = 0.0f;
            }
        }
        if (pass_.current.size() < (pos + 1) * 2) {
            pass_.current.resize((pos + 1) * 2, 0.0f);
        }
        pass_.current[pos * 2] += v;
        pass_.current[pos * 2 + 1] += v;
        ++pass_.lapFrames;
        ++pass_.passFrames;
        pass_.lapPeak = std::max(pass_.lapPeak, std::fabs(v));
        pass_.passPeak = std::max(pass_.passPeak, std::fabs(v));
    }

    void finishPass() {
        const int64_t length = engine.masterLoopLength();
        auto& list = layers_[pass_.track];
        const auto minFrames = static_cast<int64_t>(config::kMinLayerSeconds * kSampleRate);
        std::vector<float> layer = padded(std::move(pass_.current), length);
        if (pass_.base) {
            list.push_back(std::move(layer));
        } else if (pass_.laps > 0 && (pass_.lapFrames < length || pass_.lapPeak < config::kSilentLayerPeak) &&
                   !list.empty()) {
            auto& last = list.back(); // sobra de volta: entra na ultima volta inteira
            for (size_t i = 0; i < last.size() && i < layer.size(); ++i) {
                last[i] += layer[i];
            }
        } else if (pass_.laps == 0 &&
                   (pass_.passFrames < minFrames || pass_.passPeak < config::kSilentLayerPeak)) {
            // curto demais ou mudo: nao vira camada
        } else {
            list.push_back(std::move(layer));
        }
    }

    void waitForSpares() {
        for (int i = 0; i < 2000 && engine.layerStore().readyFullBuffers() < config::kSpareFullBuffers; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::vector<std::vector<float>> layers_[config::kNumTracks];
    Pass pass_;
    int64_t blockCounter_ = 0;
};

using protocol::kButtonRecPlay;
using protocol::kButtonTrack1;
using protocol::kButtonTrack2;
using protocol::kButtonUndo;

// Loop mestre na track 1 + (layers - 1) overdubs de uma volta cada.
void recordLayers(Harness& h, int layers, int seedBase) {
    h.press(kButtonRecPlay);        // abre o passe que define o loop
    h.run(kLoopFrames, seedBase);
    h.press(kButtonRecPlay);        // fecha, e ja abre o primeiro overdub
    h.breathe();
    for (int k = 1; k < layers; ++k) {
        h.run(kLoopFrames, seedBase + k);
        h.press(kButtonRecPlay);    // fecha
        h.breathe();
        if (k + 1 < layers) {
            h.press(kButtonRecPlay); // abre o proximo
        }
    }
    if (layers == 1) {
        h.press(kButtonRecPlay);    // fecha o overdub vazio que a base abriu
    }
}

void testInfiniteLayersAndUndoAll() {
    std::printf("\n1) 300 camadas numa track, com despejo em disco, e desfazer todas\n");
    Harness h;
    h.engine.layerStore().setRamBudgetBytes(2 * 1024 * 1024); // forca o disco
    recordLayers(h, 300, 1);
    expect(h.settle(), "tudo assentado depois de gravar");
    expect(h.engine.trackLayers(0) == 300, "a track tem 300 camadas");
    expect(h.engine.bufferShortages() == 0, "nunca faltou buffer para abrir camada");
    expect(h.matches(0), "soma = as 300 camadas");
    expect(h.engine.layerStore().diskBytes() > 0, "camadas antigas foram para o disco");

    bool allOk = true;
    for (int k = 300; k > 1; --k) {
        h.press(kButtonUndo);
        h.undoExpected(0);
        if (!h.settle() || !h.matches(0)) {
            std::printf("    parou ao desfazer a camada %d\n", k);
            allOk = false;
            break;
        }
    }
    expect(allOk, "cada desfazer tirou exatamente a camada mais nova (299 vezes)");
    h.press(kButtonUndo); // a ultima
    h.undoExpected(0);
    expect(h.engine.trackLayers(0) == 0 && !h.engine.loopDefined(), "ultima camada desfeita: track vazia e loop zerado");
    expect(h.engine.layerStore().diskBytes() == 0, "nada sobrou em disco");
}

void testRapidUndo() {
    std::printf("\n2) 60 camadas e 59 desfazer seguidos sem esperar (camadas voltando do disco)\n");
    Harness h;
    h.engine.layerStore().setRamBudgetBytes(0); // tudo o que for guardado vai para o disco
    recordLayers(h, 60, 500);
    expect(h.settle(), "assentado depois de gravar");
    for (int k = 0; k < 59; ++k) {
        h.press(kButtonUndo);
        h.undoExpected(0);
        h.runInput(64, 0.0f); // bem menos que um bloco entre os toques
    }
    expect(h.settle(), "os desfazer pendentes terminaram");
    expect(h.matches(0), "sobrou so a camada base, exata");
}

void testMultiTrackCancelClear() {
    std::printf("\n3) Varias tracks, passe de mais de uma volta, cancelar e limpar tudo\n");
    Harness h;
    recordLayers(h, 3, 900);          // track 1: base + 2 overdubs
    h.press(kButtonTrack2);            // seleciona a track 2
    h.press(kButtonRecPlay);           // grava a base dela a partir do meio do loop
    h.run(kLoopFrames + kLoopFrames / 2, 950);
    h.press(kButtonRecPlay);
    expect(h.settle() && h.allMatch(), "track 2 com passe de 1,5 volta + track 1 intacta");
    expect(h.engine.trackLayers(1) == 1, "1,5 volta = 1 camada (a sobra entra na volta inteira)");

    h.press(kButtonTrack1);
    h.press(kButtonRecPlay);           // overdub na track 1...
    h.run(kLoopFrames / 2, 960);
    h.press(kButtonUndo);              // ...cancelado no meio
    expect(h.settle() && h.allMatch(), "passe cancelado sumiu da soma");

    h.press(kButtonTrack2);
    h.press(kButtonUndo);
    h.undoExpected(1);
    expect(h.settle() && h.allMatch() && h.engine.loopDefined(), "track 2 limpa, loop mestre continua");

    h.press(kButtonUndo, protocol::kGestureLongPress); // Clear All
    h.clearExpected();
    expect(h.settle() && h.allMatch() && !h.engine.loopDefined(), "Clear All: tudo vazio");
}

void testLoadedSession() {
    std::printf("\n4) Sessao aberta de arquivo + overdub + desfazer\n");
    Harness h;
    std::vector<float> base(static_cast<size_t>(kLoopFrames) * 2);
    for (size_t i = 0; i < base.size(); ++i) {
        base[i] = signal(77, static_cast<int64_t>(i / 2));
    }
    const float* audio[config::kNumTracks] = {base.data(), nullptr, nullptr, nullptr};
    const bool muted[config::kNumTracks] = {};
    h.engine.applyLoadedSession(kLoopFrames, audio, muted);
    h.addExpectedLayer(0, base);
    expect(h.allMatch(), "sessao aberta = audio do arquivo");

    h.press(kButtonRecPlay);           // retoma e abre um overdub na track 1
    h.run(kLoopFrames, 78);
    h.press(kButtonRecPlay);
    expect(h.settle() && h.allMatch(), "overdub por cima da sessao");
    h.press(kButtonUndo);
    h.undoExpected(0);
    expect(h.settle() && h.allMatch(), "desfazer o overdub volta ao arquivo");
    h.press(kButtonUndo);
    h.undoExpected(0);
    expect(h.settle() && h.allMatch() && !h.engine.loopDefined(), "desfazer a base apaga a track");
}

void testOverdubLaps() {
    std::printf("\n5) Overdub sem parar por 5 voltas = 5 camadas, desfeitas uma a uma\n");
    Harness h;
    h.press(kButtonRecPlay);           // passe que define o loop
    h.run(kLoopFrames, 1200);
    h.press(kButtonRecPlay);           // fecha e ja abre o overdub
    h.breathe();
    h.run(kLoopFrames * 5, 1300);      // 5 voltas seguidas, sem apertar nada
    h.press(kButtonRecPlay);           // fecha a ultima volta (inteira)
    expect(h.settle() && h.engine.trackLayers(0) == 6, "base + 5 voltas = 6 camadas");
    expect(h.allMatch(), "cada volta guardou exatamente o que foi tocado nela");
    expect(h.engine.bufferShortages() == 0, "nenhuma volta ficou sem buffer");

    bool ok = true;
    for (int k = 0; k < 5; ++k) {
        h.press(kButtonUndo);
        h.undoExpected(0);
        ok = h.settle() && h.allMatch() && ok;
    }
    expect(ok && h.engine.trackLayers(0) == 1, "cada desfazer tirou uma volta; sobrou a base");

    h.press(kButtonRecPlay);           // overdub de novo...
    h.run(kLoopFrames * 2 + kLoopFrames / 2, 1400);
    h.press(kButtonUndo);              // ...desfazer no meio cancela so a volta em andamento
    expect(h.settle() && h.allMatch() && h.engine.trackLayers(0) == 3,
           "desfazer gravando cancela so a volta atual, as 2 inteiras ficam");
}

void testSelectedMeter() {
    std::printf("\n6) VU: a track selecionada mede o que toca E a entrada\n");
    Harness h;
    recordLayers(h, 2, 1500);          // track 1 tocando (sinal baixo), nada gravando
    h.press(kButtonTrack2);            // seleciona a track 2, vazia
    h.runInput(4800, 0.5f);            // entrada forte, sem gravar
    expect(h.engine.trackLevel(1) > 0.4f, "selecionada e vazia: mostra a entrada");
    expect(h.engine.trackLevel(0) < 0.1f, "nao selecionada: so o que toca (a entrada nao entra)");

    h.press(kButtonTrack1);            // seleciona a track 1, que esta tocando
    h.runInput(4800, 0.5f);
    expect(h.engine.trackLevel(0) > 0.4f, "selecionada tocando: a entrada aparece junto");
    h.runInput(4800 * 20, 0.0f);       // entrada em silencio (2 s)
    expect(h.engine.trackLevel(0) > 0.0f, "selecionada sem entrada: continua mostrando o que toca");

    h.press(protocol::kButtonPause);   // STOP
    h.runInput(4800 * 20, 0.5f);
    expect(h.engine.trackLevel(0) > 0.4f, "parado: a entrada da selecionada continua medida");
    h.press(kButtonTrack2);
    h.runInput(4800 * 20, 0.0f);
    expect(h.engine.trackLevel(0) < 0.01f, "parado e sem selecao: o VU da track cai a zero");
}

void testWhatCountsAsLayer() {
    std::printf("\n7) O que conta como camada: voltas a partir do inicio do passe, sobra e passes mudos\n");
    Harness h;
    h.humanTiming = true;
    h.press(kButtonRecPlay);           // base...
    h.run(kLoopFrames, 2000);
    h.press(kButtonRecPlay);           // ...fecha e o overdub abre sozinho
    h.run(kLoopFrames / 10, 0, 0.0f);  // 10 ms de nada...
    h.press(kButtonRecPlay);           // ...e para
    expect(h.settle() && h.engine.trackLayers(0) == 1 && h.allMatch(),
           "parar logo depois da base nao cria camada");

    h.runInput(kLoopFrames / 2, 0.0f); // anda meio loop sem gravar
    h.press(kButtonRecPlay);
    h.run(kLoopFrames * 6 / 10, 2100); // 0,6 volta, cruzando o comeco do loop
    h.press(kButtonRecPlay);
    expect(h.settle() && h.engine.trackLayers(0) == 2 && h.allMatch(),
           "0,6 volta comecando no meio do loop = 1 camada");

    h.runInput(kLoopFrames / 3, 0.0f);
    h.press(kButtonRecPlay);
    h.run(kLoopFrames * 13 / 10, 2200);
    h.press(kButtonRecPlay);
    expect(h.settle() && h.engine.trackLayers(0) == 3 && h.allMatch(),
           "1,3 volta = 1 camada (a sobra entra na volta inteira)");

    h.press(kButtonRecPlay);
    h.run(kLoopFrames * 2, 2300);
    h.press(kButtonRecPlay);
    expect(h.settle() && h.engine.trackLayers(0) == 5 && h.allMatch(), "2 voltas inteiras = 2 camadas");

    h.press(kButtonRecPlay);
    h.run(kLoopFrames * 3 / 2, 0, 0.0f);
    h.press(kButtonRecPlay);
    expect(h.settle() && h.engine.trackLayers(0) == 5 && h.allMatch(), "gravar em silencio nao cria camada");

    h.press(kButtonRecPlay);
    h.run(kLoopFrames, 0, 0.0f);       // uma volta muda...
    h.run(kLoopFrames, 2400);          // ...e uma tocada
    h.press(kButtonRecPlay);
    expect(h.settle() && h.engine.trackLayers(0) == 6 && h.allMatch(), "volta muda + volta tocada = 1 camada");

    bool ok = true;
    while (h.expectedLayers(0) > 1) {
        h.press(kButtonUndo);
        h.undoExpected(0);
        ok = h.settle() && h.allMatch() && ok;
    }
    expect(ok, "desfazer tira cada camada certa, inclusive a volta com sobra");
    expect(h.engine.bufferShortages() == 0, "nunca faltou buffer (todos os passes comecaram de verdade)");
}

} // namespace

int main() {
    std::printf("Motor de camadas infinitas - testes\n");
    testInfiniteLayersAndUndoAll();
    testRapidUndo();
    testMultiTrackCancelClear();
    testLoadedSession();
    testOverdubLaps();
    testSelectedMeter();
    testWhatCountsAsLayer();
    std::printf("\n%s (%d falha%s)\n", failures == 0 ? "TUDO OK" : "HOUVE FALHAS", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
