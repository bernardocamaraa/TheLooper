// Persistencia das preferencias do app entre execucoes: configuracao do
// dispositivo de audio (driver, device, sample rate, buffer size, canais) e
// os ajustes de mesa de cada track (entrada roteada + volume).
//
// Antes o app reconfigurava tudo na mao a cada abertura
// (initialiseWithDefaultDevices + setAudioDeviceSetup fixo), o que jogava
// fora qualquer escolha feita no dialogo de audio - era preciso reconfigurar
// toda vez.
//
// Onde fica o arquivo (criado automaticamente):
//   %APPDATA%\PEDAL\Pedal Looper.settings
#pragma once

#include <memory>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h> // juce::Rectangle (posicao das janelas)

#include "Config.h"

class Settings {
public:
    Settings();
    ~Settings();

    // Estado do juce::AudioDeviceManager. Devolve nullptr na primeira
    // execucao (nada salvo ainda) - nesse caso o chamador aplica os padroes.
    std::unique_ptr<juce::XmlElement> audioDeviceState() const;
    void saveAudioDeviceState(std::unique_ptr<juce::XmlElement> state);

    // Bitmask das entradas roteadas para uma track (ver AudioTrack).
    uint32_t trackInputMask(int track) const;
    void setTrackInputMask(int track, uint32_t mask);

    // Volume (fader) de uma track.
    float trackGain(int track) const;
    void setTrackGain(int track, float gain);

    // Nome editavel de uma track (aparece nas duas janelas).
    juce::String trackName(int track) const;
    void setTrackName(int track, const juce::String& name);

    // Trim de uma entrada fisica.
    float inputGain(int channel) const;
    void setInputGain(int channel, float gain);

    // Ajuste fino de latencia, em ms (ver LooperEngine::setLatencyTrimMs).
    double latencyTrimMs() const;
    void setLatencyTrimMs(double trimMs);

    // Monitoramento da entrada por software.
    bool softwareMonitoring() const;
    void setSoftwareMonitoring(bool enabled);

    // Aba aberta na janela principal ("tocar", "mixer", "sessoes", "setlist",
    // "telas", "ajustes") e a vista da aba Tocar ("cards" ou "pedal").
    juce::String page() const;
    void setPage(const juce::String& page);
    juce::String playView() const;
    void setPlayView(const juce::String& view);

    // Tela de performance (janela de medidores) aberta.
    bool metersVisible() const;
    void setMetersVisible(bool visible);

    // Aparencia: tema (0 escuro, 1 claro, 2 sistema), tamanho da interface
    // (0 compacto, 1 padrao, 2 grande), paleta inicial das tracks e a cor
    // propria de cada track (transparente = a da paleta).
    int themeMode() const;
    void setThemeMode(int mode);
    int uiSizeLevel() const;
    void setUiSizeLevel(int level);
    int trackPreset() const;
    void setTrackPreset(int preset);
    juce::Colour trackColour(int track) const;
    void setTrackColour(int track, juce::Colour colour);

    // Setlist aberto e a musica atual dele.
    juce::File setlistFile() const;
    void setSetlistFile(const juce::File& file);
    int setlistIndex() const;
    void setSetlistIndex(int index);

    // Pasta onde as gravacoes sao salvas.
    juce::File recordingFolder() const;
    void setRecordingFolder(const juce::File& folder);

    // Porta COM fixa do Mega. Vazio = auto-detectar por VID:PID.
    juce::String comPortOverride() const;
    void setComPortOverride(const juce::String& port);

    // Posicao/tamanho de cada janela, para que a divisao entre os dois
    // monitores seja feita uma vez so e nao a cada abertura.
    juce::Rectangle<int> windowBounds(const juce::String& windowId) const;

    // Monitor onde cada janela de apresentacao abre em tela cheia. Elas nao
    // tem barra de titulo para arrastar, entao o que se guarda e o INDICE do
    // monitor, nao um retangulo. -1 = nunca escolhido.
    int windowDisplay(const juce::String& windowId) const;
    void setWindowDisplay(const juce::String& windowId, int displayIndex);

    // Descarta uma posicao salva que nao caiba em nenhum monitor LIGADO agora.
    // Sem isto, fechar o app com uma janela no monitor secundario (no caso
    // desta montagem, um celular usado como segunda tela) e reabrir sem ele
    // deixa a janela fora do alcance do mouse - ela existe, mas em lugar
    // nenhum. Devolve um retangulo vazio quando a posicao nao serve mais, que
    // e o mesmo que o chamador ja trata como "nunca foi salva".
    static juce::Rectangle<int> ensureOnScreen(juce::Rectangle<int> bounds);
    void setWindowBounds(const juce::String& windowId, juce::Rectangle<int> bounds);

    void flush();

private:
    juce::PropertiesFile* file() const;

    mutable juce::ApplicationProperties properties_;
};
