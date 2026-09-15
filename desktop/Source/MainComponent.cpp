#include "MainComponent.h"

#include <cmath>

#include "PedalKeys.h"
#include "RtThread.h"
#include "UiText.h"
#include "ui/Pages.h"
#include "ui/Widgets.h"

namespace {
constexpr const char* kControlsWindowId = "controls";
constexpr const char* kMetersWindowId = "meters";
constexpr juce::uint32 kSetlistArmMs = 2500;
} // namespace

MainComponent::MainComponent()
    : audioEngine_(looperEngine_, buttonEventQueue_, uiButtonQueue_, ledCommandQueue_, micRingQueue_, recorder_),
      virtualMic_(micRingQueue_),
      serialLink_(buttonEventQueue_, ledCommandQueue_) {
    // Tema antes de tudo: as cores da LookAndFeel saem da paleta ativa.
    loadAppearance();
    lookAndFeel_.refreshColours();
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);

    // Antes de abrir qualquer device: tira o processo do modo de eficiencia do
    // Windows, que estrangulava o audio com o app minimizado.
    rt::disableProcessPowerThrottling();

    looperEngine_.prepare();
    setupAudioDevice();
    startVirtualMic(); // opcional: falha em silencio sem o cabo virtual
    serialLink_.start(settings_.comPortOverride());

    for (int i = 0; i < config::kNumTracks; ++i) {
        trackNames_[static_cast<size_t>(i)] = settings_.trackName(i);
    }
    restoreMixer();
    loadSetlist();
    setupPerformanceWindow();

    shell_ = std::make_unique<AppShell>(*this);
    shell_->addPage("tocar", "Tocar", ui::Icon::Play, std::make_unique<PlayPage>(*this));
    shell_->addPage("mixer", "Mixer", ui::Icon::Mixer, std::make_unique<MixerPage>(*this));
    shell_->addPage("sessoes", ui::utf8("Sessões"), ui::Icon::Sessions, std::make_unique<SessionsPage>(*this));
    shell_->addPage("setlist", "Setlist", ui::Icon::Setlist, std::make_unique<SetlistPage>(*this));
    shell_->addPage("telas", "Telas", ui::Icon::Screens, std::make_unique<ScreensPage>(*this));
    shell_->addPage("ajustes", "Ajustes", ui::Icon::Settings, std::make_unique<SettingsPage>(*this));
    shell_->onPageChanged = [this](const juce::String& id) { settings_.setPage(id); };
    addAndMakeVisible(*shell_);

    setSize(1180, 800);
    shell_->showPage(settings_.page());
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
    // As abas primeiro: o seletor de audio embutido escuta o device manager.
    shell_.reset();

    if (recorder_.isRecording()) {
        recorder_.stop();
    }
    performanceWindow_.reset(); // antes do componente que ela aponta
    performanceComponent_.reset();

    serialLink_.stop();
    virtualMic_.stop();
    mainDeviceManager_.removeChangeListener(this);
    mainDeviceManager_.removeAudioCallback(&audioEngine_);

    // Salva o estado final antes de fechar o device (createStateXml precisa do
    // device aberto para registrar sample rate e buffer size).
    settings_.saveAudioDeviceState(mainDeviceManager_.createStateXml());
    settings_.flush();
    mainDeviceManager_.closeAudioDevice();

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// Inicializacao
// ---------------------------------------------------------------------------

void MainComponent::setupAudioDevice() {
    // A configuracao salva da ultima sessao tem prioridade. Antes o app
    // forcava driver e taxa a cada abertura, descartando o que fosse escolhido.
    auto savedAudioState = settings_.audioDeviceState();
    const bool hadSavedState = (savedAudioState != nullptr);
    mainDeviceManager_.initialise(config::kNumChannels, config::kNumChannels, savedAudioState.get(), true);

    if (!hadSavedState) {
        mainDeviceManager_.setCurrentAudioDeviceType("ASIO", true);
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        mainDeviceManager_.getAudioDeviceSetup(setup);
        setup.sampleRate = config::kPreferredSampleRate;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setRange(0, config::kNumChannels, true);
        setup.outputChannels.setRange(0, config::kNumChannels, true);
        mainDeviceManager_.setAudioDeviceSetup(setup, true);
    }

    mainDeviceManager_.addAudioCallback(&audioEngine_);
    mainDeviceManager_.addChangeListener(this);
    settings_.saveAudioDeviceState(mainDeviceManager_.createStateXml());
    settings_.flush();
}

// Abre (ou reabre) o cabo virtual na taxa do device principal: entre os dois
// ha uma fila de frames, sem conversao.
void MainComponent::startVirtualMic() {
    double rate = 0.0;
    if (auto* device = mainDeviceManager_.getCurrentAudioDevice()) {
        rate = device->getCurrentSampleRate();
    }
    if (juce::approximatelyEqual(rate, virtualMicSampleRate_) && rate > 0.0) {
        return;
    }
    virtualMic_.stop();
    virtualMic_.start(rate);
    virtualMicSampleRate_ = rate;
}

void MainComponent::restoreMixer() {
    for (int i = 0; i < config::kNumTracks; ++i) {
        looperEngine_.setTrackInputMask(i, settings_.trackInputMask(i));
        looperEngine_.setTrackGain(i, settings_.trackGain(i));
    }
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        looperEngine_.setInputGain(ch, settings_.inputGain(ch));
    }
    looperEngine_.setLatencyTrimMs(settings_.latencyTrimMs());
    looperEngine_.setSoftwareMonitoring(settings_.softwareMonitoring());
}

void MainComponent::loadAppearance() {
    const int mode = settings_.themeMode();
    theme::setMode(mode == 1 ? theme::Mode::Light : mode == 2 ? theme::Mode::System : theme::Mode::Dark);
    theme::setSizeLevel(settings_.uiSizeLevel());
    theme::setTrackPreset(settings_.trackPreset());
    for (int t = 0; t < config::kNumTracks; ++t) {
        theme::setTrackColour(t, settings_.trackColour(t));
    }
}

void MainComponent::applyAppearance() {
    loadAppearance();
    lookAndFeel_.refreshColours();
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
        window->setBackgroundColour(theme::enclosure);
    }
    if (performanceWindow_ != nullptr) {
        performanceWindow_->setBackgroundColour(theme::enclosure);
    }
    sendLookAndFeelChange();
    if (performanceComponent_ != nullptr) {
        performanceComponent_->sendLookAndFeelChange();
        performanceComponent_->refreshColours();
    }
    shell_->refreshColours();
    repaint();
}

void MainComponent::loadSetlist() {
    const juce::File file = settings_.setlistFile();
    if (file.existsAsFile() && setlistfile::load(file, setlist_.data).isEmpty()) {
        setlist_.file = file;
        setlist_.current = juce::jlimit(0, juce::jmax(0, setlist_.size() - 1), settings_.setlistIndex());
    }
}

// ---------------------------------------------------------------------------
// Tela de performance (requisito inegociavel: ela sempre existe)
// ---------------------------------------------------------------------------

void MainComponent::setupPerformanceWindow() {
    performanceComponent_ = std::make_unique<PerformanceComponent>(
        looperEngine_, [this](int i) { return trackNames_[static_cast<size_t>(i)]; },
        [this] { return setlist_.statusText(); });

    performanceWindow_ = std::make_unique<PerformanceWindow>(ui::utf8("The Looper — Tela de performance"),
                                                             performanceComponent_.get());
    performanceWindow_->onHidden = [this] { settings_.setMetersVisible(false); };
    performanceWindow_->onKeyPressed = [this](const juce::KeyPress& key) { return handleKey(key); };

    // Abre sozinha so quando faz sentido: com um segundo monitor, ou quando o
    // usuario ja escolheu um. Com um monitor so, tela cheia cobriria os
    // controles logo na abertura.
    const int displays = juce::Desktop::getInstance().getDisplays().displays.size();
    if (settings_.metersVisible() && (displays > 1 || settings_.windowDisplay(kMetersWindowId) >= 0)) {
        showMetersWindow();
    }
}

void MainComponent::showMetersWindow() {
    if (performanceWindow_ != nullptr) {
        performanceWindow_->showOnDisplay(metersDisplay());
    }
}

bool MainComponent::metersVisible() {
    return performanceWindow_ != nullptr && performanceWindow_->isVisible();
}

void MainComponent::setMetersVisible(bool visible) {
    settings_.setMetersVisible(visible);
    if (visible) {
        showMetersWindow();
    } else if (performanceWindow_ != nullptr) {
        performanceWindow_->setVisible(false);
    }
}

int MainComponent::metersDisplay() {
    // O monitor e uma ESCOLHA salva (a janela nao tem barra de titulo para
    // arrastar). Se ele nao existe mais, cai no segundo, e dai no principal.
    const int count = juce::Desktop::getInstance().getDisplays().displays.size();
    const int saved = settings_.windowDisplay(kMetersWindowId);
    if (saved >= 0 && saved < count) {
        return saved;
    }
    return juce::jmin(1, juce::jmax(0, count - 1));
}

void MainComponent::setMetersDisplay(int index) {
    settings_.setWindowDisplay(kMetersWindowId, index);
    if (metersVisible()) {
        showMetersWindow();
    }
}

int MainComponent::controlsDisplay() {
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    const auto centre = getScreenBounds().getCentre();
    for (int i = 0; i < displays.size(); ++i) {
        if (displays[i].logicalBounds.contains(centre.toFloat())) {
            return i;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Tracks, pedal e teclado
// ---------------------------------------------------------------------------

void MainComponent::sendPedalEvent(protocol::ButtonId button, protocol::Gesture gesture) {
    ButtonEventMsg msg;
    msg.buttonId = button;
    msg.gesture = gesture;
    // Best-effort, igual ao caminho do pedal: fila cheia = descarta.
    uiButtonQueue_.push(msg);
}

bool MainComponent::handleKey(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown()) {
        const int code = key.getKeyCode();
        if (code >= '1' && code <= '6') {
            shell_->showPageIndex(code - '1');
            return true;
        }
        return false;
    }
    const juce::juce_wchar c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (c == 'n' || c == 'p') {
        setlistStep(c == 'n' ? 1 : -1);
        return true;
    }
    const ui::PedalKeyAction action = ui::pedalActionForKey(key);
    if (!action.valid) {
        return false;
    }
    sendPedalEvent(action.button, action.gesture);
    return true;
}

juce::String MainComponent::trackName(int track) {
    return trackNames_[static_cast<size_t>(juce::jlimit(0, config::kNumTracks - 1, track))];
}

void MainComponent::setTrackName(int track, const juce::String& name) {
    const juce::String cleaned = name.trim().substring(0, config::kMaxTrackNameLength);
    if (cleaned.isEmpty()) {
        return;
    }
    trackNames_[static_cast<size_t>(track)] = cleaned;
    settings_.setTrackName(track, cleaned);
}

void MainComponent::setTrackInputMask(int track, uint32_t mask) {
    looperEngine_.setTrackInputMask(track, mask);
    settings_.setTrackInputMask(track, mask);
}

void MainComponent::setTrackGain(int track, float gain) {
    looperEngine_.setTrackGain(track, gain);
    settings_.setTrackGain(track, gain);
}

void MainComponent::setInputGain(int channel, float gain) {
    looperEngine_.setInputGain(channel, gain);
    settings_.setInputGain(channel, gain);
}

void MainComponent::setTrackColour(int track, juce::Colour colour) {
    settings_.setTrackColour(track, colour);
    theme::setTrackColour(track, colour);
    shell_->refreshColours();
    if (performanceComponent_ != nullptr) {
        performanceComponent_->refreshColours();
    }
}

void MainComponent::confirmClearAll() {
    // LIMPAR TUDO de tela: clicar e CONFIRMAR, nunca segurar (pedido do
    // usuario). E a unica acao sem volta, e num touch um toque acidental e
    // questao de tempo.
    ui::confirm(ui::utf8("Limpar as 4 tracks?"),
                ui::utf8("Apaga todas as camadas de todas as tracks e o tamanho do loop. Não dá para desfazer."),
                "Limpar tudo", [this] { sendPedalEvent(protocol::kButtonUndo, protocol::kGestureLongPress); });
}

bool MainComponent::pedalConnected() {
    return serialLink_.isConnected();
}

void MainComponent::reconnectPedal() {
    // A porta e lida uma vez no start(), entao trocar exige reabrir.
    serialLink_.stop();
    serialLink_.start(settings_.comPortOverride());
}

juce::String MainComponent::audioSummary() {
    const juce::String sep = ui::utf8("  ·  ");
    auto* device = mainDeviceManager_.getCurrentAudioDevice();
    if (device == nullptr) {
        return ui::utf8("sem dispositivo de áudio");
    }
    return device->getName() + sep + juce::String(device->getCurrentSampleRate() / 1000.0, 1) + " kHz" + sep +
           juce::String(looperEngine_.latencyMs(), 1) + " ms";
}

// ---------------------------------------------------------------------------
// Sessoes (.loop)
// ---------------------------------------------------------------------------

juce::File MainComponent::loopsFolder() {
    auto folder = settings_.recordingFolder().getChildFile("Loops");
    folder.createDirectory();
    return folder;
}

juce::File MainComponent::stemsFolder() {
    auto folder = settings_.recordingFolder().getChildFile("Stems");
    folder.createDirectory();
    return folder;
}

juce::String MainComponent::saveSession(const juce::File& file) {
    if (!looperEngine_.loopDefined()) {
        return ui::utf8("Não há nada gravado para salvar.");
    }
    LoopSession session;
    session.sampleRate = looperEngine_.sampleRate();
    session.lengthSamples = looperEngine_.masterLoopLength();
    const size_t samplesPerTrack =
        static_cast<size_t>(session.lengthSamples) * static_cast<size_t>(config::kNumChannels);

    // O callback de audio sai de cena enquanto os buffers sao lidos:
    // removeAudioCallback so volta depois que o callback em curso terminou.
    mainDeviceManager_.removeAudioCallback(&audioEngine_);
    for (int i = 0; i < config::kNumTracks; ++i) {
        auto& track = session.tracks[static_cast<size_t>(i)];
        track.name = trackNames_[static_cast<size_t>(i)];
        track.inputMask = looperEngine_.trackInputMask(i);
        track.gain = looperEngine_.trackGain(i);
        track.muted = looperEngine_.trackMuted(i);
        if (looperEngine_.trackLayers(i) > 0) {
            track.audio.resize(samplesPerTrack);
            looperEngine_.readTrackMix(i, track.audio.data(), session.lengthSamples);
        }
    }
    mainDeviceManager_.addAudioCallback(&audioEngine_);

    // A escrita em disco (a parte lenta) fica fora da janela sem audio.
    return loopfile::save(file, session);
}

juce::String MainComponent::openSession(const juce::File& file) {
    LoopSession session;
    const juce::String error = loopfile::load(file, session);
    if (error.isNotEmpty()) {
        return error;
    }
    // O device pode estar noutra taxa - sem ajustar, tocaria mais lento ou
    // mais rapido.
    const double deviceRate = looperEngine_.sampleRate();
    if (deviceRate > 0.0) {
        loopfile::resampleTo(session, deviceRate);
    }

    std::array<const float*, config::kNumTracks> audio{};
    std::array<bool, config::kNumTracks> muted{};
    for (int i = 0; i < config::kNumTracks; ++i) {
        const auto& track = session.tracks[static_cast<size_t>(i)];
        audio[static_cast<size_t>(i)] = track.audio.empty() ? nullptr : track.audio.data();
        muted[static_cast<size_t>(i)] = track.muted;
    }

    mainDeviceManager_.removeAudioCallback(&audioEngine_);
    looperEngine_.applyLoadedSession(session.lengthSamples, audio.data(), muted.data());
    for (int i = 0; i < config::kNumTracks; ++i) {
        const auto& track = session.tracks[static_cast<size_t>(i)];
        looperEngine_.setTrackInputMask(i, track.inputMask);
        looperEngine_.setTrackGain(i, track.gain);
    }
    mainDeviceManager_.addAudioCallback(&audioEngine_);

    for (int i = 0; i < config::kNumTracks; ++i) {
        const auto& track = session.tracks[static_cast<size_t>(i)];
        if (track.name.isNotEmpty()) {
            setTrackName(i, track.name);
        }
        settings_.setTrackInputMask(i, track.inputMask);
        settings_.setTrackGain(i, track.gain);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Gravacao em disco
// ---------------------------------------------------------------------------

bool MainComponent::recordingToDisk() {
    return recorder_.isRecording();
}

void MainComponent::setRecordingToDisk(bool on) {
    if (!on) {
        recorder_.stop();
        return;
    }
    if (!recorder_.start(settings_.recordingFolder())) {
        ui::notify(ui::utf8("Não deu para gravar"),
                   ui::utf8("Não foi possível criar o arquivo em:\n") + settings_.recordingFolder().getFullPathName() +
                       ui::utf8("\n\nEscolha outra pasta em Ajustes → Gravações."));
    }
}

juce::File MainComponent::recordingFolder() {
    return settings_.recordingFolder();
}

void MainComponent::chooseRecordingFolder() {
    auto chooser = std::make_shared<juce::FileChooser>(ui::utf8("Pasta das gravações"), settings_.recordingFolder());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, chooser](const juce::FileChooser& fc) {
                             const juce::File chosen = fc.getResult();
                             if (chosen != juce::File{}) {
                                 settings_.setRecordingFolder(chosen);
                                 shell_->refreshColours();
                             }
                         });
}

// ---------------------------------------------------------------------------
// Setlist: o loop e sempre gravado ao vivo; trocar de musica limpa o loop e
// aplica os nomes das tracks da musica.
// ---------------------------------------------------------------------------

bool MainComponent::setlistArmed() {
    return setlistArmedAt_ != 0 && juce::Time::getMillisecondCounter() - setlistArmedAt_ < kSetlistArmMs;
}

void MainComponent::setlistStep(int direction) {
    if (setlist_.empty()) {
        return;
    }
    const int target = setlist_.current + direction;
    if (target < 0 || target >= setlist_.size()) {
        return;
    }
    setlistGoTo(target);
}

void MainComponent::setlistGoTo(int index) {
    if (setlist_.song(index) == nullptr) {
        return;
    }
    // Com loop gravado, o primeiro toque so ARMA a troca: apagar o loop no
    // meio da musica por um toque sem querer seria o pior erro possivel.
    if (looperEngine_.loopDefined() && !setlistArmed()) {
        setlistArmedAt_ = juce::Time::getMillisecondCounter();
        return;
    }
    setlistArmedAt_ = 0;
    applySong(index);
}

void MainComponent::applySong(int index) {
    const SetlistSong* song = setlist_.song(index);
    if (song == nullptr) {
        return;
    }
    if (looperEngine_.loopDefined()) {
        sendPedalEvent(protocol::kButtonUndo, protocol::kGestureLongPress); // limpa tudo
    }
    for (int t = 0; t < config::kNumTracks; ++t) {
        const juce::String& name = song->tracks[static_cast<size_t>(t)];
        if (name.isNotEmpty()) {
            setTrackName(t, name);
        }
    }
    setlist_.current = index;
    settings_.setSetlistIndex(index);
    shell_->refreshColours(); // nomes novos nos cartoes, canais e botoes do pedal
}

// ---------------------------------------------------------------------------

void MainComponent::saveControlsWindowBounds(juce::Rectangle<int> bounds) {
    settings_.setWindowBounds(kControlsWindowId, bounds);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &mainDeviceManager_) {
        settings_.saveAudioDeviceState(mainDeviceManager_.createStateXml());
        settings_.flush();
        startVirtualMic(); // o cabo virtual acompanha a taxa do device principal
    }
}

void MainComponent::timerCallback() {
    shell_->refresh();
    if (performanceComponent_ != nullptr && performanceWindow_ != nullptr && performanceWindow_->isVisible()) {
        performanceComponent_->refresh();
    }
}

void MainComponent::resized() {
    if (shell_ != nullptr) {
        shell_->setBounds(getLocalBounds());
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::palette().bg);
}
