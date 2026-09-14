// Versao VST3 do looper, para rodar dentro do FL Studio.
//
// O motor e EXATAMENTE o mesmo do standalone (LooperEngine): a unica coisa
// que muda e quem entrega o audio. No app, um device ASIO via AudioEngine;
// aqui, o host. Por isso nada de AudioDeviceManager, VirtualMicOutput nem
// dialogo de configuracao de audio nesta versao - o dono do driver e o FL.
//
// --- BARRAMENTOS ---
//
// O looper trabalha com DUAS entradas fisicas separadas (violao e voz, ver
// config::kInputChannelNames) e o FL entrega uma track do mixer por vez, cada
// uma estereo. Entao:
//
//   entrada 0 "Violao"  (principal) - a track do mixer onde o plugin esta
//   entrada 1 "Voz"     (sidechain) - outra track, com "Sidechain to this
//                                     track" + "Auto map inputs" no wrapper
//
// Cada uma e somada em MONO com (L+R)/2. O usuario manda o mesmo mono nos
// dois lados, entao a media devolve o proprio sinal - a soma daria +6 dB.
//
//   saida 0 "Mix"       (principal) - o mix completo, como o standalone
//   saida 1..4 "TRACK n"            - cada track do looper isolada
//
// As saidas 1..4 existem para o "Auto map outputs" do wrapper do FL, que
// joga cada uma na track do mixer seguinte a do plugin. Assim da para
// equalizar/gravar cada track do looper separadamente.
//
// ATENCAO ao double: se as 5 saidas estiverem audiveis ao mesmo tempo, o mix
// e as tracks somam duas vezes. Por isso existe mixOnMainOutput() - ligado por
// padrao (para quem NAO mapeou as saidas extras ainda ouvir alguma coisa) e
// que deve ser DESLIGADO depois de mapear as quatro.
#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Config.h"
#include "LooperEngine.h"
#include "Messages.h"
#include "SerialLink.h"
#include "SpscQueue.h"

class PedalLooperProcessor : public juce::AudioProcessor {
public:
    PedalLooperProcessor();
    ~PedalLooperProcessor() override;

    // --- AudioProcessor ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PEDAL Looper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // O looper compensa a latencia POR DENTRO (ver setLatencyTrimMs). Reportar
    // latencia aqui faria o FL deslocar o plugin inteiro na linha do tempo, que
    // e outra coisa.
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Usado pelo editor ---
    LooperEngine& engine() { return looperEngine_; }

    // Enfileira um toque vindo da interface (teclado ou botao na tela).
    // RT-safe do lado do consumidor, igual ao standalone.
    void sendPedalEvent(protocol::ButtonId button, protocol::Gesture gesture);

    // O mix no barramento principal. Ver o comentario sobre double no topo.
    bool mixOnMainOutput() const { return mixOnMain_.load(std::memory_order_relaxed); }
    void setMixOnMainOutput(bool on) { mixOnMain_.store(on, std::memory_order_relaxed); }

    // Qual instrumento chega pelo barramento PRINCIPAL. Padrao: o violao (a
    // voz entra pelo sidechain). Inverter aqui e mais facil do que refazer o
    // roteamento do mixer do FL.
    bool mainBusIsGuitar() const { return mainBusIsGuitar_.load(std::memory_order_relaxed); }
    void setMainBusIsGuitar(bool guitar) { mainBusIsGuitar_.store(guitar, std::memory_order_relaxed); }

    // --- Pedal fisico ---
    //
    // NAO conecta sozinho na construcao: o FL instancia o plugin durante a
    // varredura, e abrir a COM ali tomaria a porta de quem estiver usando (e
    // so um processo por vez). Quem liga e o usuario, no editor, e a escolha
    // fica salva no projeto.
    void setPedalEnabled(bool enabled);
    bool pedalEnabled() const { return pedalEnabled_; }
    bool pedalConnected() const { return pedalEnabled_ && serialLink_.isConnected(); }
    void setComPortOverride(const juce::String& port);
    juce::String comPortOverride() const { return comPort_; }

    // Nomes das tracks (so a GUI usa; o motor nao liga para eles).
    juce::String trackName(int i) const { return trackNames_[static_cast<size_t>(i)]; }
    void setTrackName(int i, const juce::String& name) { trackNames_[static_cast<size_t>(i)] = name; }

private:
    // BusesProperties e protected em juce::AudioProcessor, entao a montagem
    // dos barramentos tem de ser membro da classe (nao da para faze-la numa
    // funcao livre no .cpp).
    static BusesProperties buildBuses();

    LooperEngine looperEngine_;

    // Duas filas de botao, uma por produtor: SpscQueue e estritamente
    // single-producer (ver SpscQueue.h). Mesma regra do standalone.
    SpscQueue<ButtonEventMsg, 64> pedalButtonQueue_; // produtor: thread do SerialLink
    SpscQueue<ButtonEventMsg, 64> uiButtonQueue_;    // produtor: thread da GUI
    SpscQueue<LedCommand, 64> ledCommandQueue_;
    SerialLink serialLink_;

    std::atomic<bool> mixOnMain_{true};
    std::atomic<bool> mainBusIsGuitar_{true};

    bool prepared_ = false; // ver prepareToPlay
    bool pedalEnabled_ = false;
    juce::String comPort_;
    std::array<juce::String, config::kNumTracks> trackNames_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalLooperProcessor)
};
