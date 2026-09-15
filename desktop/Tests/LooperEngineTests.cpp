// Testes do motor de camadas infinitas, sem device de audio nem JUCE.
//
// O teste dirige a LooperEngine como o callback de audio faria (serviceBlock a
// cada 256 frames, processFrame por frame, botoes entre os blocos) com audio
// sintetico, e mantem em paralelo o que CADA camada deveria ter gravado. Depois
// de cada gravacao e de cada desfazer, a soma da track (readTrackMix) tem de
// bater amostra por amostra com a soma das camadas que sobraram.
//
// Rodar: build/Release/LooperEngineTests.exe  (sai com codigo 1 se algo falhar)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
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

// Sinal pequeno e diferente por camada: 300+ camadas somadas ficam longe do
// limiter, e uma camada trocada por outra apareceria na comparacao.
float signal(int seed, int64_t frame) {
    const int v = static_cast<int>((seed * 131 + frame * 7) % 29) - 14;
    return 0.0005f * static_cast<float>(v) / 14.0f;
}

class Harness {
public:
    Harness() {
        engine.prepare();
        engine.setAudioDeviceInfo(kSampleRate, 0); // latencia 0: grava onde le
        waitForSpares();
    }

    void press(protocol::ButtonId button, protocol::Gesture gesture = protocol::kGesturePress) {
        engine.handleButtonEvent(button, gesture);
        syncCaptures();
    }

    // Roda `frames` frames como o callback faria, gravando `seed` na track que
    // estiver capturando - e anotando onde cada amostra foi parar.
    void run(int64_t frames, int seed) {
        for (int64_t i = 0; i < frames; ++i) {
            if ((blockCounter_++ % 256) == 0) {
                engine.serviceBlock();
            }
            const float v = signal(seed, i);
            if (capturing_ >= 0) {
                const auto pos = static_cast<size_t>(std::llround(engine.loopPositionSeconds() * kSampleRate));
                // Cada volta de overdub e uma camada: quando a escrita passa
                // pelo comeco do loop, a volta anterior fecha (igual ao motor).
                if (pos == 0 && engine.masterLoopLength() > 0 && !current_->empty()) {
                    current_->resize(static_cast<size_t>(engine.masterLoopLength()) * 2, 0.0f);
                    layers_[capturing_].push_back(std::move(*current_));
                    current_ = std::make_unique<std::vector<float>>();
                    ++layersAtStart_;
                }
                auto& layer = *current_;
                if (layer.size() < (pos + 1) * 2) {
                    layer.resize((pos + 1) * 2, 0.0f);
                }
                layer[pos * 2] += v;
                layer[pos * 2 + 1] += v;
            }
            const float in[config::kNumChannels] = {v, v};
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
            run(256, 0);
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
        if (worst > 1e-4) {
            std::printf("    track %d: diferenca maxima %.6g\n", track, worst);
        }
        return worst <= 1e-4 && engine.trackLayers(track) == static_cast<int>(layers_[track].size());
    }

    bool allMatch() {
        bool ok = true;
        for (int t = 0; t < config::kNumTracks; ++t) {
            ok = matches(t) && ok;
        }
        return ok;
    }

    LooperEngine engine;

private:
    void waitForSpares() {
        for (int i = 0; i < 2000 && engine.layerStore().readyFullBuffers() < config::kSpareFullBuffers; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Espelha o que a FSM fez com as capturas depois de um botao: fechou
    // (camada nova), cancelou (descarta) ou abriu outra.
    void syncCaptures() {
        if (capturing_ >= 0) {
            const int layersNow = engine.trackLayers(capturing_);
            const bool stillRecording = engine.trackState(capturing_) == TrackState::RECORDING;
            if (layersNow > layersAtStart_) {
                current_->resize(static_cast<size_t>(engine.masterLoopLength()) * 2, 0.0f);
                layers_[capturing_].push_back(std::move(*current_));
                capturing_ = -1;
            } else if (!stillRecording) {
                capturing_ = -1; // cancelado
            } else {
                return; // o mesmo passe continua
            }
        }
        for (int t = 0; t < config::kNumTracks; ++t) {
            if (engine.trackState(t) == TrackState::RECORDING) {
                capturing_ = t;
                layersAtStart_ = engine.trackLayers(t);
                current_ = std::make_unique<std::vector<float>>();
                break;
            }
        }
    }

    std::vector<std::vector<float>> layers_[config::kNumTracks];
    std::unique_ptr<std::vector<float>> current_;
    int capturing_ = -1;
    int layersAtStart_ = 0;
    int64_t blockCounter_ = 0;
};

using protocol::kButtonRecPlay;
using protocol::kButtonTrack1;
using protocol::kButtonTrack2;
using protocol::kButtonUndo;

// Loop mestre na track 1 + (layers - 1) overdubs, um por volta.
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
        h.run(64, 0); // bem menos que um bloco entre os toques
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

    h.press(kButtonTrack1);
    h.press(kButtonRecPlay);           // overdub na track 1...
    h.run(kLoopFrames / 2, 960);
    h.press(kButtonUndo);              // ...cancelado no meio
    expect(h.settle() && h.allMatch(), "passe cancelado sumiu da soma");

    h.press(kButtonTrack2);
    bool lapsOk = h.expectedLayers(1) >= 2; // o passe de 1,5 volta virou mais de uma camada
    while (h.expectedLayers(1) > 0) {
        h.press(kButtonUndo);
        h.undoExpected(1);
        lapsOk = h.settle() && h.allMatch() && lapsOk;
    }
    expect(lapsOk && h.engine.loopDefined(), "track 2 desfeita volta a volta, loop mestre continua");

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
    h.press(kButtonRecPlay);           // fecha a ultima volta
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

} // namespace

int main() {
    std::printf("Motor de camadas infinitas - testes\n");
    testInfiniteLayersAndUndoAll();
    testRapidUndo();
    testMultiTrackCancelClear();
    testLoadedSession();
    testOverdubLaps();
    std::printf("\n%s (%d falha%s)\n", failures == 0 ? "TUDO OK" : "HOUVE FALHAS", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
