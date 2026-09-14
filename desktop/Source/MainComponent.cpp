#include "MainComponent.h"

#include <cmath>

#include "RtThread.h"

#include "PedalKeys.h"
#include "UiText.h"

namespace {
constexpr const char* kControlsWindowId = "controls";
constexpr const char* kMetersWindowId = "meters";
constexpr const char* kAudienceWindowId = "audience";
} // namespace

MainComponent::MainComponent()
    : audioEngine_(looperEngine_, buttonEventQueue_, uiButtonQueue_, ledCommandQueue_, micRingQueue_,
                    recorder_),
      virtualMic_(micRingQueue_),
      serialLink_(buttonEventQueue_, ledCommandQueue_) {
    // Vale para as duas janelas e para os dialogos do JUCE (configuracao de
    // audio, menus das ComboBox) - sem isto o dialogo de audio abriria com a
    // aparencia padrao, destoando do resto.
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);

    // Antes de abrir qualquer device: tira o processo do modo de eficiencia do
    // Windows, que era o que estrangulava o audio com o app minimizado.
    rt::disableProcessPowerThrottling();

    looperEngine_.prepare();

    setupAudioDevice();

    // Opcional: falha silenciosamente se o cabo virtual nao estiver instalado.
    // Vai na taxa REAL do device principal - ver VirtualMicOutput::start.
    startVirtualMic();
    serialLink_.start(settings_.comPortOverride());

    addAndMakeVisible(progressBar_);

    // O desenho do pedal e a mesa de som se revezam na janela em vez de
    // dividi-la: cada um sozinho fica com a janela inteira, que e a unica
    // forma de os dois terem tamanho de dedo num monitor touch.
    pageToggle_.setWantsKeyboardFocus(false);
    pageToggle_.setTooltip(
        ui::utf8("Alterna a janela entre o mapa do pedal e a mesa de som. A escolha fica salva."));
    pageToggle_.onClick = [this] {
        setPage(page_ == Page::Pedal ? Page::Mixer : Page::Pedal);
    };
    addAndMakeVisible(pageToggle_);

    audioSettingsButton_.setWantsKeyboardFocus(false);
    audioSettingsButton_.setButtonText(ui::utf8("Configurar áudio"));
    audioSettingsButton_.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible(audioSettingsButton_);

    // As janelas de apresentacao ocupam um monitor inteiro e nao tem barra de
    // titulo (ver PerformanceWindow), entao TUDO sobre elas - mostrar, o que
    // mostrar e em qual monitor - se resolve neste menu.
    screensButton_.setButtonText(ui::utf8("Telas ▾"));
    screensButton_.setWantsKeyboardFocus(false);
    screensButton_.setTooltip(
        ui::utf8("Janelas de apresentação: medidores, tela de plateia e em qual monitor cada uma "
                 "abre."));
    screensButton_.onClick = [this] { showScreensMenu(); };
    addAndMakeVisible(screensButton_);

    // Salvar e abrir a musica inteira: chegar na feira com os loops prontos e
    // so apertar PLAY.
    loopFileButton_.setButtonText(ui::utf8("Loop ▾"));
    loopFileButton_.setWantsKeyboardFocus(false);
    loopFileButton_.setTooltip(
        ui::utf8("Salva as quatro tracks num arquivo .loop e abre um salvo antes."));
    loopFileButton_.onClick = [this] { showLoopMenu(); };
    addAndMakeVisible(loopFileButton_);

    // Rodape discreto: estado do pedal e os numeros do driver, em fonte
    // monoespacada para os valores nao dancarem ao mudar. A latencia saiu do
    // topo (pedido do usuario) mas continua a mao, porque e o que se olha
    // para ajustar config::kLatencyTrimMs.
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setFont(theme::data(11.5f));
    statusLabel_.setColour(juce::Label::textColourId, theme::inkFaint);
    addAndMakeVisible(statusLabel_);

    pedalMap_.onPedalEvent = [this](protocol::ButtonId button, protocol::Gesture gesture) {
        sendPedalEvent(button, gesture);
    };
    addAndMakeVisible(pedalMap_);

    for (int i = 0; i < config::kNumTracks; ++i) {
        trackNames_[static_cast<size_t>(i)] = settings_.trackName(i);
    }

    setupAudioControls();
    setupInputStrips();
    setupTrackStrips();
    setupPerformanceWindow();

    // Depois de tudo criado: e setPage que decide o que fica visivel.
    setPage(static_cast<Page>(juce::jlimit(0, 1, settings_.controlsPage())));

    if (settings_.thirdScreenOpen()) {
        setThirdScreenOpen(true);
    }

    setSize(1040, 760);
    startTimerHz(30);
}

void MainComponent::setupAudioDevice() {
    // Dispositivo ASIO principal (interface de audio real - violao + voz).
    // Ver docs/BUILD.md: requer o driver ASIO4ALL (ou outro driver ASIO)
    // instalado e o ASIO SDK disponivel no build.
    //
    // A configuracao salva da ultima sessao tem prioridade. Antes o app
    // forcava driver, sample rate e canais na mao a cada abertura, o que
    // descartava silenciosamente tudo o que fosse escolhido no dialogo de
    // audio - dai a necessidade de reconfigurar toda vez.
    auto savedAudioState = settings_.audioDeviceState();
    const bool hadSavedState = (savedAudioState != nullptr);
    mainDeviceManager_.initialise(config::kNumChannels, config::kNumChannels, savedAudioState.get(), true);

    if (!hadSavedState) {
        // Primeira execucao: aplica os padroes do projeto como ponto de
        // partida (a partir daqui vale o que o usuario escolher).
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

    // Grava o estado logo na abertura. O listener acima so pega as mudancas
    // DEPOIS deste ponto, entao sem isto a configuracao inicial so iria pro
    // disco no fechamento - e se o app fechasse mal, nao iria nunca.
    settings_.saveAudioDeviceState(mainDeviceManager_.createStateXml());
    settings_.flush();
}

void MainComponent::setupAudioControls() {
    auto caption = [](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setFont(theme::legend(12.0f));
        label.setColour(juce::Label::textColourId, theme::inkDim);
    };

    // --- Ajuste fino de latencia -----------------------------------------
    caption(latencyCaption_, ui::utf8("Ajuste de latência"));
    addAndMakeVisible(latencyCaption_);

    const double savedTrim = settings_.latencyTrimMs();
    looperEngine_.setLatencyTrimMs(savedTrim);

    latencyTrimSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    latencyTrimSlider_.setRange(-50.0, 150.0, 0.5);
    latencyTrimSlider_.setValue(savedTrim, juce::dontSendNotification);
    latencyTrimSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    latencyTrimSlider_.setTextValueSuffix(" ms");
    latencyTrimSlider_.setDoubleClickReturnValue(true, config::kDefaultLatencyTrimMs);
    latencyTrimSlider_.setTooltip(
        ui::utf8("Somado ao que o driver reporta. Positivo adianta a gravação — use se o overdub "
                 "entrar atrasado. O ASIO4ALL costuma reportar menos que a latência real."));
    latencyTrimSlider_.onValueChange = [this] {
        const double trim = latencyTrimSlider_.getValue();
        looperEngine_.setLatencyTrimMs(trim);
        settings_.setLatencyTrimMs(trim);
    };
    addAndMakeVisible(latencyTrimSlider_);

    // --- Monitoramento por software --------------------------------------
    const bool monitoring = settings_.softwareMonitoring();
    looperEngine_.setSoftwareMonitoring(monitoring);

    monitorToggle_.setClickingTogglesState(true);
    monitorToggle_.setToggleState(monitoring, juce::dontSendNotification);
    monitorToggle_.setWantsKeyboardFocus(false);
    monitorToggle_.setTooltip(
        ui::utf8("Deixe desligado se a sua interface já faz monitoramento direto por hardware — "
                 "os dois juntos somam o mesmo sinal duas vezes e dão som de lata."));
    monitorToggle_.onClick = [this] {
        const bool on = monitorToggle_.getToggleState();
        looperEngine_.setSoftwareMonitoring(on);
        settings_.setSoftwareMonitoring(on);
    };
    addAndMakeVisible(monitorToggle_);

    // --- Porta do pedal ---------------------------------------------------
    caption(comPortCaption_, "Porta COM");
    addAndMakeVisible(comPortCaption_);

    comPortEditor_.setText(settings_.comPortOverride(), juce::dontSendNotification);
    comPortEditor_.setTextToShowWhenEmpty("auto", theme::inkFaint);
    comPortEditor_.setFont(theme::data(12.0f));
    comPortEditor_.setTooltip(
        ui::utf8("Vazio = detectar sozinho pelo VID:PID do chip USB. Preencha (ex: COM5) só se a "
                 "detecção falhar. Vale ao reconectar."));
    comPortEditor_.onTextChange = [this] { settings_.setComPortOverride(comPortEditor_.getText()); };
    addAndMakeVisible(comPortEditor_);

    // --- Gravacao em disco -----------------------------------------------
    recordButton_.setButtonText(ui::utf8("● Gravar"));
    recordButton_.setClickingTogglesState(true);
    recordButton_.setWantsKeyboardFocus(false);
    recordButton_.setTooltip(ui::utf8("Grava em WAV 24 bits o que sai pelos alto-falantes, "
                                       "já mixado. Um arquivo por gravação, com data e hora."));
    recordButton_.onClick = [this] {
        if (recordButton_.getToggleState()) {
            if (!recorder_.start(settings_.recordingFolder())) {
                recordButton_.setToggleState(false, juce::dontSendNotification);
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, ui::utf8("Não deu para gravar"),
                    ui::utf8("Não foi possível criar o arquivo em:\n") +
                        settings_.recordingFolder().getFullPathName() +
                        ui::utf8("\n\nEscolha outra pasta em \"Abrir pasta\"."));
            }
        } else {
            recorder_.stop();
        }
    };
    addAndMakeVisible(recordButton_);

    openFolderButton_.setWantsKeyboardFocus(false);
    openFolderButton_.setTooltip(ui::utf8("Abre a pasta das gravações. Clique com Shift para "
                                           "escolher outra pasta."));
    openFolderButton_.onClick = [this] {
        const juce::File folder = settings_.recordingFolder();
        if (juce::ModifierKeys::getCurrentModifiers().isShiftDown()) {
            auto chooser = std::make_shared<juce::FileChooser>(ui::utf8("Pasta das gravações"), folder);
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [this, chooser](const juce::FileChooser& fc) {
                                      const juce::File chosen = fc.getResult();
                                      if (chosen != juce::File{}) {
                                          settings_.setRecordingFolder(chosen);
                                      }
                                  });
            return;
        }
        folder.createDirectory(); // so existe depois da primeira gravacao
        folder.revealToUser();
    };
    addAndMakeVisible(openFolderButton_);

    reconnectButton_.setWantsKeyboardFocus(false);
    reconnectButton_.setTooltip("Fecha e reabre a porta serial aplicando a porta acima");
    reconnectButton_.onClick = [this] {
        // A porta e lida uma vez no start(), entao trocar exige reabrir - e
        // assim nao ha string mutando entre threads.
        serialLink_.stop();
        serialLink_.start(settings_.comPortOverride());
    };
    addAndMakeVisible(reconnectButton_);
}

void MainComponent::setupInputStrips() {
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        auto strip = std::make_unique<InputStrip>();

        strip->name.setText(ui::utf8(config::kInputChannelNames[ch]), juce::dontSendNotification);
        strip->name.setJustificationType(juce::Justification::centredLeft);
        strip->name.setFont(theme::legend(14.0f));
        strip->name.setColour(juce::Label::textColourId, theme::ink);
        addAndMakeVisible(strip->name);

        const float saved = settings_.inputGain(ch);
        looperEngine_.setInputGain(ch, saved);

        strip->gain.setSliderStyle(juce::Slider::LinearHorizontal);
        strip->gain.setRange(0.0, 100.0 * config::kMaxInputGain, 1.0);
        strip->gain.setSkewFactorFromMidPoint(100.0);
        strip->gain.setValue(100.0 * static_cast<double>(saved), juce::dontSendNotification);
        strip->gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
        strip->gain.setTextValueSuffix(" %");
        strip->gain.setDoubleClickReturnValue(true, 100.0 * config::kDefaultInputGain);
        strip->gain.onValueChange = [this, ch] {
            const float gain = static_cast<float>(inputStrips_[static_cast<size_t>(ch)]->gain.getValue() / 100.0);
            looperEngine_.setInputGain(ch, gain);
            settings_.setInputGain(ch, gain);
        };
        addAndMakeVisible(strip->gain);

        strip->reset.setButtonText(ui::utf8("Padrão"));
        strip->reset.setTooltip("Voltar este trim para 100%");
        strip->reset.onClick = [this, ch] {
            inputStrips_[static_cast<size_t>(ch)]->gain.setValue(100.0 * config::kDefaultInputGain,
                                                                  juce::sendNotificationSync);
        };
        addAndMakeVisible(strip->reset);

        inputStrips_[static_cast<size_t>(ch)] = std::move(strip);
    }
}

void MainComponent::setupTrackStrips() {
    for (int i = 0; i < config::kNumTracks; ++i) {
        auto strip = std::make_unique<TrackControlStrip>();
        strip->setLevelSource([this, i] { return looperEngine_.trackLevel(i); });

        // Restaura o canal de mesa salvo e aplica na engine ANTES de ligar os
        // callbacks, para nao regravar os mesmos valores no disco na abertura.
        const uint32_t mask = settings_.trackInputMask(i);
        const float gain = settings_.trackGain(i);
        looperEngine_.setTrackInputMask(i, mask);
        looperEngine_.setTrackGain(i, gain);
        strip->setValues(trackNames_[static_cast<size_t>(i)], mask, gain);

        strip->onNameChanged = [this, i](juce::String name) {
            trackNames_[static_cast<size_t>(i)] = name;
            settings_.setTrackName(i, name);
        };
        strip->onInputMaskChanged = [this, i](uint32_t newMask) {
            looperEngine_.setTrackInputMask(i, newMask);
            settings_.setTrackInputMask(i, newMask);
        };
        strip->onGainChanged = [this, i](float newGain) {
            looperEngine_.setTrackGain(i, newGain);
            settings_.setTrackGain(i, newGain);
        };

        addAndMakeVisible(*strip);
        trackStrips_[static_cast<size_t>(i)] = std::move(strip);
    }
}

void MainComponent::setupPerformanceWindow() {
    performanceComponent_ = std::make_unique<PerformanceComponent>(
        looperEngine_, [this](int i) { return trackNames_[static_cast<size_t>(i)]; });

    performanceWindow_ = std::make_unique<PerformanceWindow>(ui::utf8("Pedal Looper — VUs"),
                                                              performanceComponent_.get());
    performanceWindow_->onHidden = [this] { metersVisible_ = false; };
    performanceWindow_->onKeyPressed = [this](const juce::KeyPress& key) { return handlePedalKey(key); };

    // Vista salva da ultima sessao (medidores ou plateia).
    audienceOnMeters_ = settings_.audienceOnMeters();
    performanceComponent_->setAudienceView(audienceOnMeters_);

    // Segundo monitor por padrao - e o que fica virado para quem toca.
    metersVisible_ = true;
    applyWindowDisplay(performanceWindow_.get(), kMetersWindowId, 1);
}

// Terceira janela: a tela de plateia sozinha, para um monitor virado para quem
// assiste. Existe so quando pedida - com dois monitores o caminho e alternar a
// vista da janela de VUs.
void MainComponent::setupAudienceWindow() {
    if (audienceWindow_ != nullptr) {
        return;
    }

    audienceScreen_ = std::make_unique<AudienceView>(
        looperEngine_, [this](int i) { return trackNames_[static_cast<size_t>(i)]; });

    audienceWindow_ = std::make_unique<PerformanceWindow>(ui::utf8("Pedal Looper — Plateia"),
                                                           audienceScreen_.get());
    audienceWindow_->onHidden = [this] {
        thirdScreenOpen_ = false;
        settings_.setThirdScreenOpen(false);
    };
    audienceWindow_->onKeyPressed = [this](const juce::KeyPress& key) { return handlePedalKey(key); };
}

// As janelas de apresentacao nao tem barra de titulo para arrastar, entao o
// monitor de cada uma e uma ESCOLHA salva, nao uma posicao. Quando o monitor
// escolhido nao existe mais (o celular usado como segunda tela ficou em casa),
// cai no padrao e, dai, no monitor principal.
void MainComponent::applyWindowDisplay(PerformanceWindow* window, const juce::String& windowId,
                                        int fallbackIndex) {
    if (window == nullptr) {
        return;
    }
    const int count = juce::Desktop::getInstance().getDisplays().displays.size();
    int index = settings_.windowDisplay(windowId);
    if (index < 0 || index >= count) {
        index = juce::jmin(fallbackIndex, juce::jmax(0, count - 1));
    }
    window->showOnDisplay(index);
}

void MainComponent::setMetersVisible(bool visible) {
    metersVisible_ = visible;
    if (performanceWindow_ == nullptr) {
        return;
    }
    if (visible) {
        applyWindowDisplay(performanceWindow_.get(), kMetersWindowId, 1);
    } else {
        performanceWindow_->setVisible(false);
    }
}

void MainComponent::setAudienceOnMeters(bool audience) {
    audienceOnMeters_ = audience;
    settings_.setAudienceOnMeters(audience);
    if (performanceComponent_ != nullptr) {
        performanceComponent_->setAudienceView(audience);
    }
}

void MainComponent::setThirdScreenOpen(bool open) {
    thirdScreenOpen_ = open;
    settings_.setThirdScreenOpen(open);

    if (open) {
        setupAudienceWindow();
        applyWindowDisplay(audienceWindow_.get(), kAudienceWindowId, 2);
    } else if (audienceWindow_ != nullptr) {
        audienceWindow_->setVisible(false);
    }
}

// Menu unico das janelas de apresentacao: o que aparece, o que cada uma mostra
// e em qual monitor. Sem barra de titulo nas janelas, este menu e o unico
// lugar onde essas tres coisas se decidem.

// --- Salvar e abrir a musica inteira (.loop) -----------------------------
//
// O ponto disto e chegar na feira com os loops PRONTOS: abrir o arquivo e ter
// as quatro tracks tocando o que tocavam quando foram salvas, sem gravar nada
// na frente das pessoas.

juce::File MainComponent::loopsFolder() const {
    // Ao lado das gravacoes, numa pasta propria: sao coisas diferentes (uma e
    // a musica para reabrir, a outra e o WAV da apresentacao).
    auto folder = settings_.recordingFolder().getChildFile("Loops");
    folder.createDirectory();
    return folder;
}

void MainComponent::showLoopMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, ui::utf8("Salvar loop..."), looperEngine_.loopDefined());
    menu.addItem(2, ui::utf8("Abrir loop..."));
    menu.addSeparator();
    menu.addItem(3, ui::utf8("Abrir a pasta dos loops"));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loopFileButton_),
                        [this](int result) {
                            if (result == 1) {
                                saveLoop();
                            } else if (result == 2) {
                                openLoop();
                            } else if (result == 3) {
                                loopsFolder().startAsProcess();
                            }
                        });
}

void MainComponent::saveLoop() {
    if (!looperEngine_.loopDefined()) {
        return;
    }

    const juce::String suggested =
        "Loop " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M") + loopfile::kExtension;

    fileChooser_ = std::make_unique<juce::FileChooser>(
        ui::utf8("Salvar a música"), loopsFolder().getChildFile(suggested), loopfile::kWildcard);

    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File{}) {
                return;
            }
            if (file.getFileExtension().isEmpty()) {
                file = file.withFileExtension(loopfile::kExtension);
            }

            LoopSession session;
            session.sampleRate = looperEngine_.sampleRate();
            session.lengthSamples = looperEngine_.masterLoopLength();

            const size_t samplesPerTrack = static_cast<size_t>(session.lengthSamples) *
                                            static_cast<size_t>(config::kNumChannels);

            // O callback de audio sai de cena enquanto os buffers sao lidos:
            // removeAudioCallback so volta depois que o callback em curso
            // terminou, entao dali em diante ninguem esta escrevendo neles.
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

            // A escrita em disco fica FORA da janela sem audio: ela e a parte
            // lenta, e nao precisa do audio parado.
            const juce::String error = loopfile::save(file, session);
            if (error.isNotEmpty()) {
                juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                             ui::utf8("Não deu para salvar"), error);
                return;
            }
            statusLabel_.setText(ui::utf8("Salvo: ") + file.getFileName(),
                                  juce::dontSendNotification);
        });
}

void MainComponent::openLoop() {
    fileChooser_ = std::make_unique<juce::FileChooser>(ui::utf8("Abrir uma música"), loopsFolder(),
                                                        loopfile::kWildcard);

    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file == juce::File{} || !file.existsAsFile()) {
                return;
            }

            LoopSession session;
            juce::String error = loopfile::load(file, session);
            if (error.isNotEmpty()) {
                juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                             ui::utf8("Não deu para abrir"), error);
                return;
            }

            // O device pode estar aberto numa taxa diferente da de quando a
            // musica foi gravada - inclusive de um dia para o outro, no mesmo
            // computador. Sem ajustar, ela tocaria mais lenta ou mais rapida.
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

            // A mesa na tela e o disco acompanham o que foi aberto - senao os
            // faders mostrariam os valores da sessao anterior.
            for (int i = 0; i < config::kNumTracks; ++i) {
                const auto& track = session.tracks[static_cast<size_t>(i)];
                if (track.name.isNotEmpty()) {
                    trackNames_[static_cast<size_t>(i)] = track.name;
                    settings_.setTrackName(i, track.name);
                }
                settings_.setTrackInputMask(i, track.inputMask);
                settings_.setTrackGain(i, track.gain);
                trackStrips_[static_cast<size_t>(i)]->setValues(
                    trackNames_[static_cast<size_t>(i)], track.inputMask, track.gain);
            }

            statusLabel_.setText(ui::utf8("Aberto: ") + file.getFileName() +
                                      ui::utf8("  ·  aperte PLAY+REC para tocar"),
                                  juce::dontSendNotification);
        });
}

void MainComponent::showScreensMenu() {
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    const int displayCount = juce::jmax(1, displays.size());

    juce::PopupMenu menu;
    menu.addItem(1, ui::utf8("Mostrar a janela de medidores"), true, metersVisible_);
    menu.addItem(2, ui::utf8("Mostrar a tela de plateia (janela separada)"), true, thirdScreenOpen_);

    menu.addSeparator();
    menu.addSectionHeader(ui::utf8("A janela de medidores mostra"));
    menu.addItem(3, ui::utf8("Medidores (para quem toca)"), true, !audienceOnMeters_);
    menu.addItem(4, ui::utf8("Tela de plateia (para quem assiste)"), true, audienceOnMeters_);

    if (displayCount > 1) {
        const int metersDisplay = settings_.windowDisplay(kMetersWindowId);
        const int audienceDisplay = settings_.windowDisplay(kAudienceWindowId);

        juce::PopupMenu metersMonitors;
        juce::PopupMenu audienceMonitors;
        for (int i = 0; i < displayCount; ++i) {
            const juce::String label = ui::utf8("Monitor ") + juce::String(i + 1) +
                                        (displays[i].isMain ? ui::utf8("  (principal)") : juce::String());
            metersMonitors.addItem(100 + i, label, true, metersDisplay == i);
            audienceMonitors.addItem(200 + i, label, true, audienceDisplay == i);
        }

        menu.addSeparator();
        menu.addSubMenu(ui::utf8("Monitor dos medidores"), metersMonitors);
        menu.addSubMenu(ui::utf8("Monitor da tela de plateia"), audienceMonitors);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&screensButton_),
                        [this](int result) {
                            if (result == 0) {
                                return;
                            }
                            if (result == 1) {
                                setMetersVisible(!metersVisible_);
                            } else if (result == 2) {
                                setThirdScreenOpen(!thirdScreenOpen_);
                            } else if (result == 3 || result == 4) {
                                setAudienceOnMeters(result == 4);
                            } else if (result >= 100 && result < 200) {
                                settings_.setWindowDisplay(kMetersWindowId, result - 100);
                                if (metersVisible_) {
                                    applyWindowDisplay(performanceWindow_.get(), kMetersWindowId, 1);
                                }
                            } else if (result >= 200 && result < 300) {
                                settings_.setWindowDisplay(kAudienceWindowId, result - 200);
                                if (thirdScreenOpen_) {
                                    applyWindowDisplay(audienceWindow_.get(), kAudienceWindowId, 2);
                                }
                            }
                        });
}

MainComponent::~MainComponent() {
    stopTimer();
    audioSettingsWindow_.reset();

    if (performanceWindow_ != nullptr) {
        settings_.setWindowBounds(kMetersWindowId, performanceWindow_->getBounds());
    }
    if (audienceWindow_ != nullptr) {
        settings_.setWindowBounds(kAudienceWindowId, audienceWindow_->getBounds());
    }
    audienceWindow_.reset();
    audienceScreen_.reset();
    // A janela precisa morrer antes do componente que ela aponta (ela o
    // referencia sem ser dona - setContentNonOwned).
    performanceWindow_.reset();
    performanceComponent_.reset();

    serialLink_.stop();
    virtualMic_.stop();
    mainDeviceManager_.removeChangeListener(this);
    mainDeviceManager_.removeAudioCallback(&audioEngine_);

    // Salva o estado final antes de fechar o device (createStateXml precisa do
    // device ainda aberto para registrar sample rate e buffer size).
    settings_.saveAudioDeviceState(mainDeviceManager_.createStateXml());
    settings_.flush();

    mainDeviceManager_.closeAudioDevice();

    // Por ultimo: nenhum componente pode continuar apontando para ela.
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainComponent::saveControlsWindowBounds(juce::Rectangle<int> bounds) {
    settings_.setWindowBounds(kControlsWindowId, bounds);
}

void MainComponent::sendPedalEvent(protocol::ButtonId button, protocol::Gesture gesture) {
    ButtonEventMsg msg;
    msg.buttonId = button;
    msg.gesture = gesture;
    // Best-effort, igual ao caminho do pedal: se a fila encheu (o callback de
    // audio parou), descartar e melhor do que bloquear a GUI.
    uiButtonQueue_.push(msg);
}

bool MainComponent::handlePedalKey(const juce::KeyPress& key) {
    const ui::PedalKeyAction action = ui::pedalActionForKey(key);
    if (!action.valid) {
        return false;
    }
    sendPedalEvent(action.button, action.gesture);
    return true;
}

// Abre (ou reabre) o cabo virtual na taxa do device principal. As duas pontas
// TEM de estar na mesma taxa: entre elas ha uma fila de frames, sem conversao.
void MainComponent::startVirtualMic() {
    double rate = 0.0;
    if (auto* device = mainDeviceManager_.getCurrentAudioDevice()) {
        rate = device->getCurrentSampleRate();
    }
    if (juce::approximatelyEqual(rate, virtualMicSampleRate_) && rate > 0.0) {
        return; // ja esta aberto na taxa certa
    }
    virtualMic_.stop();
    virtualMic_.start(rate);
    virtualMicSampleRate_ = rate;
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &mainDeviceManager_) {
        settings_.saveAudioDeviceState(mainDeviceManager_.createStateXml());
        settings_.flush();
        // Trocar de driver ou de taxa no dialogo de audio muda a taxa do device
        // principal, e o cabo virtual precisa seguir junto.
        startVirtualMic();
    }
}


// Troca de pagina: so o que pertence a pagina aberta continua visivel. O
// rotulo do botao diz para ONDE ele leva, nao onde se esta.
void MainComponent::setPage(Page page) {
    page_ = page;
    settings_.setControlsPage(static_cast<int>(page));

    pageToggle_.setButtonText(page == Page::Pedal ? ui::utf8("Mesa de som ▶")
                                                  : ui::utf8("◀ Mapa do pedal"));

    const bool pedalPage = (page == Page::Pedal);
    pedalMap_.setVisible(pedalPage);
    latencyCaption_.setVisible(pedalPage);
    latencyTrimSlider_.setVisible(pedalPage);
    monitorToggle_.setVisible(pedalPage);
    comPortCaption_.setVisible(pedalPage);
    comPortEditor_.setVisible(pedalPage);
    reconnectButton_.setVisible(pedalPage);
    for (auto& strip : inputStrips_) {
        strip->name.setVisible(pedalPage);
        strip->gain.setVisible(pedalPage);
        strip->reset.setVisible(pedalPage);
    }

    for (auto& strip : trackStrips_) {
        strip->setVisible(page == Page::Mixer);
    }


    resized();
    repaint();
}

// As fontes fixadas no construtor nao sabem do tamanho da janela. Aqui elas
// sao refeitas quando a escala muda - e SO quando muda, senao cada arrasto de
// borda reconstruiria uma dezena de Font e refaria o layout de texto.
void MainComponent::applyUiScale(float scale) {
    if (std::abs(scale - uiScale_) < 0.01f) {
        return;
    }
    uiScale_ = scale;

    statusLabel_.setFont(theme::data(11.5f * scale));
    latencyCaption_.setFont(theme::legend(12.0f * scale));
    comPortCaption_.setFont(theme::legend(12.0f * scale));
    comPortEditor_.setFont(theme::data(12.0f * scale));
    comPortEditor_.applyFontToAllText(theme::data(12.0f * scale));

    // Refazer o estilo da caixa de texto recria o Label dela, que e onde a
    // fonte do valor e escolhida (ver PedalLookAndFeel::createSliderTextBox).
    const auto box = [scale](juce::Slider& slider, int width, int height) {
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false,
                                juce::roundToInt(static_cast<float>(width) * scale),
                                juce::roundToInt(static_cast<float>(height) * scale));
    };
    box(latencyTrimSlider_, 70, 20);

    for (auto& strip : inputStrips_) {
        strip->name.setFont(theme::legend(14.0f * scale));
        box(strip->gain, 54, 20);
    }
}

void MainComponent::resized() {
    // ESCALA DA INTERFACE.
    //
    // O desenho do pedal escala sozinho (ele e um desenho), mas todo o resto
    // eram medidas fixas em pixel. Numa tela grande - o caso do monitor touch
    // com a janela maximizada - o pedal crescia e o resto continuava do mesmo
    // tamanho, entao tudo ficava minusculo do lado dele. Agora TODA medida
    // desta funcao passa por sc(), e as fontes fixadas no construtor sao
    // reajustadas em applyUiScale(). 1040 px de largura = escala 1 (o tamanho
    // de janela para o qual o layout foi desenhado).
    const float uiScale = juce::jlimit(1.0f, 1.9f, static_cast<float>(getWidth()) / 1040.0f);
    const auto sc = [uiScale](int value) {
        return juce::roundToInt(static_cast<float>(value) * uiScale);
    };
    applyUiScale(uiScale);

    // Margem para o conteudo nao encostar na moldura de modo.
    auto area = getLocalBounds().reduced(static_cast<int>(ui::frameThickness()));

    auto header = area.removeFromTop(sc(28));
    pageToggle_.setBounds(header.removeFromLeft(sc(160)));
    header.removeFromLeft(sc(16));
    audioSettingsButton_.setBounds(header.removeFromLeft(sc(130)));
    header.removeFromLeft(sc(8));
    screensButton_.setBounds(header.removeFromLeft(sc(110)));
    header.removeFromLeft(sc(8));
    loopFileButton_.setBounds(header.removeFromLeft(sc(95)));
    header.removeFromLeft(sc(8));
    recordButton_.setBounds(header.removeFromLeft(sc(110)));
    header.removeFromLeft(sc(8));
    openFolderButton_.setBounds(header.removeFromLeft(sc(110)));
    header.removeFromLeft(sc(16));
    progressBar_.setBounds(header.reduced(0, sc(10)));

    // O rodape sai primeiro, pela base: antes ele era reservado depois dos
    // blocos e acabava empurrado para fora da janela, cortado ao meio.
    area.removeFromBottom(sc(12));
    statusLabel_.setBounds(area.removeFromBottom(sc(16)));
    area.removeFromBottom(sc(14));

    // DUAS PAGINAS, uma de cada vez (ver setPage). O desenho do pedal e a mesa
    // nao dividem mais a janela: cada um sozinho fica com tudo o que ha, que e
    // a unica forma de os dois serem grandes o bastante para uso com o dedo
    // num monitor touch.
    const int ruleHeight = sc(16);

    if (page_ == Page::Mixer) {
        pedalRule_ = {};
        audioRule_ = {};
        inputsRule_ = {};

        channelsRule_ = area.removeFromTop(ruleHeight);
        area.removeFromTop(sc(8));

        const int gap = sc(12);
        const int stripWidth = (area.getWidth() - gap * (config::kNumTracks - 1)) / config::kNumTracks;
        for (int i = 0; i < config::kNumTracks; ++i) {
            trackStrips_[static_cast<size_t>(i)]->setBounds(area.removeFromLeft(stripWidth));
            area.removeFromLeft(gap);
        }
        return;
    }

    channelsRule_ = {};

    const int rowHeight = sc(26);
    const int rowGap = sc(10);

    // Os ajustes ficam numa faixa embaixo, em duas colunas (AUDIO | ENTRADAS),
    // e o desenho do pedal fica com TODO o resto da pagina.
    const int settingsHeight = ruleHeight + rowGap + rowHeight + rowGap + rowHeight;
    auto settings = area.removeFromBottom(settingsHeight);
    area.removeFromBottom(sc(14));

    pedalRule_ = area.removeFromTop(ruleHeight);
    area.removeFromTop(sc(8));
    // O desenho se centraliza sozinho na caixa mantendo a proporcao - ver
    // PedalMap::resized -, entao aqui basta entregar o espaco inteiro.
    pedalMap_.setBounds(area);

    auto audioColumn = settings.removeFromLeft(settings.getWidth() * 3 / 5);
    settings.removeFromLeft(sc(30));
    auto inputsColumn = settings;

    audioRule_ = audioColumn.removeFromTop(ruleHeight);
    audioColumn.removeFromTop(rowGap);
    {
        auto row = audioColumn.removeFromTop(rowHeight);
        latencyCaption_.setBounds(row.removeFromLeft(sc(120)));
        latencyTrimSlider_.setBounds(row);
        audioColumn.removeFromTop(rowGap);

        // Monitoracao e porta serial dividem a linha: nesta pagina a coluna e
        // larga o bastante para as duas.
        row = audioColumn.removeFromTop(rowHeight);
        monitorToggle_.setBounds(row.removeFromLeft(juce::jmin(sc(220), row.getWidth() / 2)));
        row.removeFromLeft(sc(24));
        comPortCaption_.setBounds(row.removeFromLeft(sc(75)));
        comPortEditor_.setBounds(row.removeFromLeft(sc(80)).reduced(0, 1));
        row.removeFromLeft(sc(10));
        reconnectButton_.setBounds(row.removeFromLeft(sc(140)));
    }

    inputsRule_ = inputsColumn.removeFromTop(ruleHeight);
    inputsColumn.removeFromTop(rowGap);

    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        auto row = inputsColumn.removeFromTop(rowHeight);
        auto& strip = *inputStrips_[static_cast<size_t>(ch)];
        strip.name.setBounds(row.removeFromLeft(sc(84)));
        strip.reset.setBounds(row.removeFromRight(sc(74)));
        row.removeFromRight(sc(10));
        strip.gain.setBounds(row);
        inputsColumn.removeFromTop(rowGap - sc(2));
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::enclosure);
    theme::paintSectionRule(g, pedalRule_, "PEDAL");
    theme::paintSectionRule(g, audioRule_, ui::utf8("ÁUDIO"));
    theme::paintSectionRule(g, inputsRule_, "ENTRADAS");
    theme::paintSectionRule(g, channelsRule_, "CANAIS");
    ui::paintModeFrame(g, getLocalBounds(), mood_);
}

void MainComponent::timerCallback() {
    const bool recMode = (looperEngine_.mode() == GlobalMode::REC_MODE);
    const bool playing = looperEngine_.transportPlaying();
    const int selected = looperEngine_.selectedTrack();

    const ui::FrameMood mood = ui::moodFor(recMode, playing);
    if (mood != mood_) {
        mood_ = mood;
        repaint();
    }

    progressBar_.setProgress(looperEngine_.loopProgress(), looperEngine_.loopDefined(), mood);

    for (int i = 0; i < config::kNumTracks; ++i) {
        auto& strip = *trackStrips_[static_cast<size_t>(i)];
        strip.setSelected(recMode && (i == selected));
        strip.setTrackState(looperEngine_.trackState(i));
    }

    // Mapa do pedal: qual switch acabou de ser pisado e o que o app esta
    // mandando para os LEDs.
    {
        PedalMap::State pedal;
        // O firmware envia o press, nao o release, entao "apertado" e
        // aproximado por "pressionado nos ultimos 180ms" - tempo suficiente
        // para o olho pegar sem ficar aceso demais numa sequencia rapida.
        const int64_t litFrames = static_cast<int64_t>(0.18 * looperEngine_.sampleRate());
        for (int i = 0; i < protocol::kButtonCount; ++i) {
            pedal.pressed[i] = looperEngine_.framesSincePress(i) < litFrames;
        }
        for (int t = 0; t < config::kNumTracks; ++t) {
            pedal.led[t] = looperEngine_.ledColor(t);
            pedal.ledBlink[t] = looperEngine_.ledBlink(t);
            pedal.level[t] = looperEngine_.trackLevel(t);
            pedal.recording = pedal.recording || (looperEngine_.trackState(t) == TrackState::RECORDING);
        }
        // O anel do pedal gira com o loop mestre: mesma fonte da barra de
        // progresso do cabecalho. A cor dele vem do modo, como a moldura.
        pedal.loopPosition = static_cast<float>(looperEngine_.loopProgress());
        pedal.loopDefined = looperEngine_.loopDefined();
        pedal.mood = mood;
        pedalMap_.updateState(pedal);
    }

    // Todo literal acentuado passa por ui::utf8 - ver o comentario em UiText.h.
    const juce::String sep = ui::utf8("   ·   ");
    juce::String status = serialLink_.isConnected() ? "PEDAL CONECTADO" : "PEDAL DESCONECTADO";
    if (auto* device = mainDeviceManager_.getCurrentAudioDevice()) {
        status += sep + device->getName() + sep +
                   juce::String(device->getCurrentSampleRate() / 1000.0, 1) + " kHz" + sep + "buffer " +
                   juce::String(device->getCurrentBufferSizeSamples());
    } else {
        status += sep + ui::utf8("sem dispositivo de áudio");
    }
    status += sep + ui::utf8("latência ") + juce::String(looperEngine_.latencyMs(), 1) + " ms";
    statusLabel_.setText(status, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId,
                            serialLink_.isConnected() ? theme::inkFaint : theme::amber);

    if (performanceComponent_ != nullptr && performanceWindow_ != nullptr &&
        performanceWindow_->isVisible()) {
        performanceComponent_->refresh();
    }

    if (audienceScreen_ != nullptr && audienceWindow_ != nullptr && audienceWindow_->isVisible()) {
        audienceScreen_->refresh();
    }
}

void MainComponent::showAudioSettings() {
    auto* selector = new juce::AudioDeviceSelectorComponent(mainDeviceManager_, 0, config::kNumChannels, 0,
                                                              config::kNumChannels, false, false, false, false);
    selector->setSize(500, 450);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = ui::utf8("Configuração de áudio (ASIO)");
    options.dialogBackgroundColour = theme::enclosure;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    audioSettingsWindow_.reset(options.launchAsync());
}
