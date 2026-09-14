// JANELA DE CONTROLES: tudo o que se ajusta fica aqui (trim das entradas,
// canal de mesa de cada track, configuracao de audio), para poder ficar num
// monitor enquanto os VUs ficam no outro - ver PerformanceComponent.
//
// Esta classe tambem e dona de toda a cadeia (LooperEngine ->
// AudioEngine/ASIO, SerialLink, VirtualMicOutput), da Settings e da segunda
// janela. Ver software/docs/CONTROL_MODEL.md para a FSM que a LooperEngine
// implementa.
#pragma once

#include <array>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "LoopProgressBar.h"
#include "LooperEngine.h"
#include "Messages.h"
#include "ModeFrame.h"
#include "AudienceView.h"
#include "LoopFile.h"
#include "PedalMap.h"
#include "PedalLookAndFeel.h"
#include "PerformanceComponent.h"
#include "Recorder.h"
#include "SerialLink.h"
#include "Settings.h"
#include "SpscQueue.h"
#include "TrackControlStrip.h"
#include "VirtualMicOutput.h"

class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener {
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Usados pela janela principal (Main.cpp) para restaurar/salvar a propria
    // posicao entre execucoes.
    Settings& settings() { return settings_; }
    void saveControlsWindowBounds(juce::Rectangle<int> bounds);

    // Atalhos de teclado do pedal. Chamado pelo keyPressed() das DUAS janelas
    // (ver Main.cpp e PerformanceWindow): a tecla sobe ate a janela quando o
    // componente com foco nao a consome, e assim os atalhos funcionam
    // independente de qual das duas esta em primeiro plano. Devolve true se
    // consumiu a tecla.
    bool handlePedalKey(const juce::KeyPress& key);

    // Enfileira um evento de botao vindo da INTERFACE (teclado ou barra de
    // botoes). RT-safe do lado do consumidor - ver AudioEngine.
    void sendPedalEvent(protocol::ButtonId button, protocol::Gesture gesture);

private:
    void timerCallback() override; // atualiza as DUAS janelas (~30fps)
    // Salva a configuracao de audio sempre que ela muda (troca de driver,
    // device, sample rate...), inclusive pelo dialogo de configuracao.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void setupAudioDevice();
    void startVirtualMic();
    void setupInputStrips();
    void setupTrackStrips();
    void setupPerformanceWindow();
    void setupAudioControls();
    void showAudioSettings();
    void showScreensMenu();
    void showLoopMenu();
    void saveLoop();
    void openLoop();
    juce::File loopsFolder() const;
    void setMetersVisible(bool visible);
    void setAudienceOnMeters(bool audience);
    void setThirdScreenOpen(bool open);
    void setupAudienceWindow();
    // Reposiciona uma janela de apresentacao no monitor salvo para ela.
    void applyWindowDisplay(PerformanceWindow* window, const juce::String& windowId, int fallbackIndex);

    // As tres paginas da janela de controles. Elas se revezam em vez de
    // dividir a janela: cada uma sozinha fica com tudo, que e a unica forma de
    // o pedal e a mesa terem tamanho de dedo num monitor touch.
    enum class Page { Pedal, Mixer };
    void setPage(Page page);

    // Re-fixa as fontes que sao definidas no construtor, para elas
    // acompanharem o tamanho da janela - ver o comentario em resized().
    void applyUiScale(float scale);

    // Um trim por entrada fisica (Violao, Voz...).
    struct InputStrip {
        juce::Label name;
        juce::Slider gain;
        juce::TextButton reset;
    };

    // Ordem de declaracao = ordem de construcao/destruicao: a LookAndFeel
    // precisa sobreviver a todo componente que a usa, entao vem primeiro (e
    // e desinstalada no destrutor, antes de qualquer um deles morrer).
    PedalLookAndFeel lookAndFeel_;

    // A LooperEngine precisa sobreviver ao AudioEngine, etc. A Settings vem
    // cedo porque e lida durante a construcao de quase tudo o mais.
    Settings settings_;

    LooperEngine looperEngine_;

    SpscQueue<ButtonEventMsg, 64> buttonEventQueue_;  // produtor: thread do SerialLink
    SpscQueue<ButtonEventMsg, 64> uiButtonQueue_;     // produtor: thread da GUI
    SpscQueue<LedCommand, 64> ledCommandQueue_;
    SpscQueue<AudioFrame, 8192> micRingQueue_;

    Recorder recorder_; // declarado ANTES do AudioEngine, que guarda uma referencia
    AudioEngine audioEngine_;
    juce::AudioDeviceManager mainDeviceManager_; // device ASIO (interface real)
    VirtualMicOutput virtualMic_;
    double virtualMicSampleRate_ = 0.0; // taxa com que o cabo virtual esta aberto
    SerialLink serialLink_;

    std::array<juce::String, config::kNumTracks> trackNames_;

    Page page_ = Page::Pedal;
    ui::FrameMood mood_ = ui::FrameMood::Stopped;
    float uiScale_ = 0.0f; // ultima escala aplicada (0 = fontes ainda nao ajustadas)
    LoopProgressBar progressBar_;

    juce::TextButton pageToggle_;
    juce::TextButton audioSettingsButton_;
    // Um botao so para as janelas de apresentacao: mostrar/esconder, escolher
    // o que cada uma mostra e em qual monitor. Tres botoes separados comiam a
    // largura do cabecalho no celular - e um menu e alvo maior para o dedo do
    // que tres botoes espremidos.
    juce::TextButton screensButton_;
    juce::TextButton loopFileButton_;   // salvar/abrir a musica inteira (.loop)
    // O seletor de arquivos e assincrono: precisa sobreviver a chamada que o
    // abriu, senao a janela some antes de o usuario escolher.
    std::unique_ptr<juce::FileChooser> fileChooser_;
    bool metersVisible_ = true;
    bool audienceOnMeters_ = false;
    bool thirdScreenOpen_ = false;
    juce::Label statusLabel_;
    PedalMap pedalMap_;

    // Ajustes que antes eram constantes de compilacao.
    juce::Label latencyCaption_;
    juce::Slider latencyTrimSlider_;
    juce::TextButton monitorToggle_{"Monitorar entrada"};
    juce::TextButton reconnectButton_{"Reconectar pedal"};
    juce::TextButton recordButton_;
    juce::TextButton openFolderButton_{"Abrir pasta"};
    juce::Label comPortCaption_;
    juce::TextEditor comPortEditor_;

    // Reguas de secao com a legenda apoiada nelas (serigrafia de painel).
    // Sao pintadas, nao componentes - nao ha nada para interagir com elas.
    juce::Rectangle<int> pedalRule_;
    juce::Rectangle<int> audioRule_;
    juce::Rectangle<int> inputsRule_;
    juce::Rectangle<int> channelsRule_;

    std::array<std::unique_ptr<InputStrip>, config::kNumChannels> inputStrips_;
    std::array<std::unique_ptr<TrackControlStrip>, config::kNumTracks> trackStrips_;

    std::unique_ptr<PerformanceComponent> performanceComponent_;
    std::unique_ptr<PerformanceWindow> performanceWindow_;

    // Terceira janela, opcional: a tela de plateia num monitor so dela, para
    // os medidores continuarem visiveis para quem toca.
    std::unique_ptr<AudienceView> audienceScreen_;
    std::unique_ptr<PerformanceWindow> audienceWindow_;

    std::unique_ptr<juce::DialogWindow> audioSettingsWindow_;
    juce::TooltipWindow tooltipWindow_;
};
