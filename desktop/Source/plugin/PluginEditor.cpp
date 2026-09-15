#include "PluginEditor.h"

#include "PedalKeys.h"
#include "UiText.h"

namespace {
constexpr int kBaseWidth = 1040; // largura em que a escala da interface e 1
}

PedalLooperEditor::PedalLooperEditor(PedalLooperProcessor& processor)
    : juce::AudioProcessorEditor(&processor), processor_(processor) {
    setLookAndFeel(&lookAndFeel_);

    addAndMakeVisible(progressBar_);
    addAndMakeVisible(pedalMap_);
    pedalMap_.onPedalEvent = [this](protocol::ButtonId button, protocol::Gesture gesture) {
        processor_.sendPedalEvent(button, gesture);
    };

    setupControls();
    setupInputStrips();
    setupTrackStrips();
    setPage(Page::Pedal);

    // Sem isto os atalhos do pedal (ver PedalKeys.h) nunca chegam: o host fica
    // com as teclas. O EDITOR_WANTS_KEYBOARD_FOCUS do CMakeLists e a outra
    // metade dessa mesma configuracao.
    setWantsKeyboardFocus(true);

    setResizable(true, true);
    setResizeLimits(820, 560, 3000, 2000);
    setSize(kBaseWidth, 760);

    startTimerHz(30);
}

PedalLooperEditor::~PedalLooperEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void PedalLooperEditor::setupControls() {
    pageToggle_.setButtonText("MESA");
    pageToggle_.setTooltip("Alternar entre o pedal e a mesa de mixagem");
    pageToggle_.setWantsKeyboardFocus(false);
    pageToggle_.onClick = [this] { setPage(page_ == Page::Pedal ? Page::Mixer : Page::Pedal); };
    addAndMakeVisible(pageToggle_);

    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setColour(juce::Label::textColourId, theme::inkFaint);
    addAndMakeVisible(statusLabel_);

    // --- Roteamento: as duas escolhas que so existem dentro do host ---

    mixOnMainToggle_.setButtonText(ui::utf8("Mix na saída principal"));
    mixOnMainToggle_.setClickingTogglesState(true);
    mixOnMainToggle_.setWantsKeyboardFocus(false);
    mixOnMainToggle_.setToggleState(processor_.mixOnMainOutput(), juce::dontSendNotification);
    mixOnMainToggle_.setTooltip(ui::utf8(
        "Ligado: a saída principal leva o mix completo. Depois de mapear as quatro "
        "saídas de track no wrapper do FL (Auto map outputs), DESLIGUE - senão o mix "
        "e as tracks somam duas vezes."));
    mixOnMainToggle_.onClick = [this] {
        processor_.setMixOnMainOutput(mixOnMainToggle_.getToggleState());
    };
    addAndMakeVisible(mixOnMainToggle_);

    mainBusToggle_.setClickingTogglesState(true);
    mainBusToggle_.setWantsKeyboardFocus(false);
    mainBusToggle_.setToggleState(processor_.mainBusIsGuitar(), juce::dontSendNotification);
    mainBusToggle_.setTooltip(ui::utf8(
        "Qual instrumento chega pela entrada principal (a track do mixer onde o "
        "plugin está). O outro entra pelo sidechain."));
    mainBusToggle_.onClick = [this] {
        processor_.setMainBusIsGuitar(mainBusToggle_.getToggleState());
        // O texto do botao E a informacao aqui, entao ele se reescreve.
        mainBusToggle_.setButtonText(ui::utf8(processor_.mainBusIsGuitar()
                                                  ? "Entrada principal: Violão"
                                                  : "Entrada principal: Voz"));
    };
    mainBusToggle_.setButtonText(ui::utf8(processor_.mainBusIsGuitar() ? "Entrada principal: Violão"
                                                                       : "Entrada principal: Voz"));
    addAndMakeVisible(mainBusToggle_);

    // --- Ajuste de latencia ---

    latencyCaption_.setText(ui::utf8("Latência"), juce::dontSendNotification);
    latencyCaption_.setColour(juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible(latencyCaption_);

    latencyTrimSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    latencyTrimSlider_.setRange(-50.0, 150.0, 0.5);
    latencyTrimSlider_.setValue(processor_.engine().latencyTrimMs(), juce::dontSendNotification);
    latencyTrimSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    latencyTrimSlider_.setTextValueSuffix(" ms");
    latencyTrimSlider_.setDoubleClickReturnValue(true, config::kDefaultLatencyTrimMs);
    // No plugin este slider e a UNICA compensacao que existe: o driver e do FL
    // e o plugin nao tem como perguntar a latencia dele a ninguem.
    latencyTrimSlider_.setTooltip(ui::utf8(
        "Se o overdub cai atrasado, aumente. Dentro do FL este é o único ajuste de "
        "latência que existe - o plugin não enxerga o driver."));
    latencyTrimSlider_.onValueChange = [this] {
        processor_.engine().setLatencyTrimMs(latencyTrimSlider_.getValue());
    };
    addAndMakeVisible(latencyTrimSlider_);

    monitorToggle_.setButtonText("Monitorar entrada");
    monitorToggle_.setClickingTogglesState(true);
    monitorToggle_.setWantsKeyboardFocus(false);
    monitorToggle_.setToggleState(processor_.engine().softwareMonitoring(), juce::dontSendNotification);
    monitorToggle_.setTooltip(ui::utf8(
        "Somar a entrada na saída. Deixe DESLIGADO se você já ouve o violão pela "
        "interface ou pela própria track do FL - os dois juntos dão som de lata."));
    monitorToggle_.onClick = [this] {
        processor_.engine().setSoftwareMonitoring(monitorToggle_.getToggleState());
    };
    addAndMakeVisible(monitorToggle_);

    // --- Pedal fisico ---

    pedalCaption_.setText("Porta COM", juce::dontSendNotification);
    pedalCaption_.setColour(juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible(pedalCaption_);

    pedalToggle_.setButtonText("Conectar pedal");
    pedalToggle_.setClickingTogglesState(true);
    pedalToggle_.setWantsKeyboardFocus(false);
    pedalToggle_.setToggleState(processor_.pedalEnabled(), juce::dontSendNotification);
    // NAO conecta sozinho - ver o comentario em PluginProcessor.h: o FL
    // instancia o plugin so para varrer, e a COM e de um processo por vez.
    pedalToggle_.setTooltip(ui::utf8(
        "Abre a porta serial do pedal. Fica desligado por padrão porque a porta é "
        "de um processo por vez - o app standalone e o plugin não podem usá-la juntos."));
    pedalToggle_.onClick = [this] { processor_.setPedalEnabled(pedalToggle_.getToggleState()); };
    addAndMakeVisible(pedalToggle_);

    comPortEditor_.setText(processor_.comPortOverride(), juce::dontSendNotification);
    comPortEditor_.setTextToShowWhenEmpty(ui::utf8("automático"), theme::inkFaint);
    comPortEditor_.setTooltip(ui::utf8("Vazio = detectar o Mega sozinho. Ex.: COM5"));
    comPortEditor_.onReturnKey = [this] {
        processor_.setComPortOverride(comPortEditor_.getText().trim());
        // Devolve o foco, senao a proxima tecla do pedal vai para a caixa.
        grabKeyboardFocus();
    };
    comPortEditor_.onFocusLost = [this] {
        processor_.setComPortOverride(comPortEditor_.getText().trim());
    };
    addAndMakeVisible(comPortEditor_);
}

void PedalLooperEditor::setupInputStrips() {
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        auto strip = std::make_unique<InputStrip>();

        strip->name.setText(ui::utf8(config::kInputChannelNames[ch]), juce::dontSendNotification);
        strip->name.setJustificationType(juce::Justification::centredLeft);
        strip->name.setColour(juce::Label::textColourId, theme::ink);
        addAndMakeVisible(strip->name);

        strip->gain.setSliderStyle(juce::Slider::LinearHorizontal);
        strip->gain.setRange(0.0, 100.0 * config::kMaxInputGain, 1.0);
        strip->gain.setSkewFactorFromMidPoint(100.0);
        strip->gain.setValue(100.0 * static_cast<double>(processor_.engine().inputGain(ch)),
                             juce::dontSendNotification);
        strip->gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
        strip->gain.setTextValueSuffix(" %");
        strip->gain.setDoubleClickReturnValue(true, 100.0 * config::kDefaultInputGain);
        strip->gain.onValueChange = [this, ch] {
            processor_.engine().setInputGain(
                ch, static_cast<float>(inputStrips_[static_cast<size_t>(ch)]->gain.getValue() / 100.0));
        };
        addAndMakeVisible(strip->gain);

        strip->reset.setButtonText(ui::utf8("Padrão"));
        strip->reset.setWantsKeyboardFocus(false);
        strip->reset.setTooltip("Voltar este trim para 100%");
        strip->reset.onClick = [this, ch] {
            inputStrips_[static_cast<size_t>(ch)]->gain.setValue(100.0 * config::kDefaultInputGain,
                                                                  juce::sendNotificationSync);
        };
        addAndMakeVisible(strip->reset);

        inputStrips_[static_cast<size_t>(ch)] = std::move(strip);
    }
}

void PedalLooperEditor::setupTrackStrips() {
    for (int i = 0; i < config::kNumTracks; ++i) {
        auto strip = std::make_unique<TrackControlStrip>();
        strip->setTrackIndex(i);
        strip->setLevelSource([this, i] { return processor_.engine().trackLevel(i); });

        // Os valores ja vivem no processor (que os restaurou do projeto do FL).
        // O strip so os espelha - ligar os callbacks depois evita reescrever
        // os mesmos valores na abertura.
        strip->setValues(processor_.trackName(i), processor_.engine().trackInputMask(i),
                         processor_.engine().trackGain(i));

        strip->onNameChanged = [this, i](juce::String name) { processor_.setTrackName(i, name); };
        strip->onInputMaskChanged = [this, i](uint32_t mask) {
            processor_.engine().setTrackInputMask(i, mask);
        };
        strip->onGainChanged = [this, i](float gain) { processor_.engine().setTrackGain(i, gain); };

        addAndMakeVisible(*strip);
        trackStrips_[static_cast<size_t>(i)] = std::move(strip);
    }
}

void PedalLooperEditor::setPage(Page page) {
    page_ = page;
    const bool pedalPage = (page == Page::Pedal);

    pageToggle_.setButtonText(pedalPage ? "MESA" : "PEDAL");

    pedalMap_.setVisible(pedalPage);
    mixOnMainToggle_.setVisible(pedalPage);
    mainBusToggle_.setVisible(pedalPage);
    latencyCaption_.setVisible(pedalPage);
    latencyTrimSlider_.setVisible(pedalPage);
    monitorToggle_.setVisible(pedalPage);
    pedalCaption_.setVisible(pedalPage);
    pedalToggle_.setVisible(pedalPage);
    comPortEditor_.setVisible(pedalPage);

    for (auto& strip : inputStrips_) {
        strip->name.setVisible(!pedalPage);
        strip->gain.setVisible(!pedalPage);
        strip->reset.setVisible(!pedalPage);
    }
    for (auto& strip : trackStrips_) {
        strip->setVisible(!pedalPage);
    }

    resized();
    repaint();
}

void PedalLooperEditor::applyUiScale(float scale) {
    if (std::abs(scale - uiScale_) < 0.01f) {
        return;
    }
    uiScale_ = scale;

    statusLabel_.setFont(theme::data(11.5f * scale));
    latencyCaption_.setFont(theme::legend(12.0f * scale));
    pedalCaption_.setFont(theme::legend(12.0f * scale));
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

void PedalLooperEditor::resized() {
    // Toda a interface escala com a largura, como no standalone: nada de
    // metrica em pixel fixo aqui dentro.
    const float scale = juce::jlimit(0.75f, 1.6f,
                                     static_cast<float>(getWidth()) / static_cast<float>(kBaseWidth));
    applyUiScale(scale);
    const auto sc = [scale](int v) { return juce::roundToInt(static_cast<float>(v) * scale); };

    auto area = getLocalBounds().reduced(juce::roundToInt(ui::frameThickness()) + sc(10));

    auto header = area.removeFromTop(sc(38));
    pageToggle_.setBounds(header.removeFromLeft(sc(120)));
    header.removeFromLeft(sc(10));
    statusLabel_.setBounds(header);

    area.removeFromTop(sc(6));
    progressBar_.setBounds(area.removeFromTop(sc(10)));
    area.removeFromTop(sc(12));

    if (page_ == Page::Pedal) {
        pedalRule_ = area.removeFromTop(sc(18));
        area.removeFromTop(sc(6));

        // O desenho do pedal guarda a proporcao do equipamento real.
        const int mapHeight = juce::jmin(
            area.getHeight() - sc(150),
            juce::roundToInt(static_cast<float>(area.getWidth()) * PedalMap::kArtHeight /
                             PedalMap::kArtWidth));
        pedalMap_.setBounds(area.removeFromTop(juce::jmax(sc(120), mapHeight)));
        area.removeFromTop(sc(14));

        routingRule_ = area.removeFromTop(sc(18));
        area.removeFromTop(sc(6));
        {
            auto row = area.removeFromTop(sc(34));
            const int half = (row.getWidth() - sc(10)) / 2;
            mixOnMainToggle_.setBounds(row.removeFromLeft(half));
            row.removeFromLeft(sc(10));
            mainBusToggle_.setBounds(row.removeFromLeft(half));
        }
        area.removeFromTop(sc(12));

        {
            auto row = area.removeFromTop(sc(30));
            latencyCaption_.setBounds(row.removeFromLeft(sc(80)));
            monitorToggle_.setBounds(row.removeFromRight(sc(190)));
            row.removeFromRight(sc(10));
            latencyTrimSlider_.setBounds(row);
        }
        area.removeFromTop(sc(10));

        {
            auto row = area.removeFromTop(sc(30));
            pedalToggle_.setBounds(row.removeFromLeft(sc(170)));
            row.removeFromLeft(sc(14));
            pedalCaption_.setBounds(row.removeFromLeft(sc(80)));
            comPortEditor_.setBounds(row.removeFromLeft(sc(120)));
        }
        return;
    }

    // --- Pagina da mesa ---
    inputsRule_ = area.removeFromTop(sc(18));
    area.removeFromTop(sc(6));
    for (auto& strip : inputStrips_) {
        auto row = area.removeFromTop(sc(30));
        strip->name.setBounds(row.removeFromLeft(sc(110)));
        strip->reset.setBounds(row.removeFromRight(sc(90)));
        row.removeFromRight(sc(8));
        strip->gain.setBounds(row);
        area.removeFromTop(sc(6));
    }

    area.removeFromTop(sc(10));
    channelsRule_ = area.removeFromTop(sc(18));
    area.removeFromTop(sc(8));

    const int gap = sc(10);
    const int stripWidth = (area.getWidth() - gap * (config::kNumTracks - 1)) / config::kNumTracks;
    for (int i = 0; i < config::kNumTracks; ++i) {
        trackStrips_[static_cast<size_t>(i)]->setBounds(area.removeFromLeft(stripWidth));
        area.removeFromLeft(gap);
    }
}

void PedalLooperEditor::paint(juce::Graphics& g) {
    g.fillAll(theme::enclosure);

    if (page_ == Page::Pedal) {
        theme::paintSectionRule(g, pedalRule_, "PEDAL");
        theme::paintSectionRule(g, routingRule_, ui::utf8("ROTEAMENTO NO FL"));
    } else {
        theme::paintSectionRule(g, inputsRule_, "ENTRADAS");
        theme::paintSectionRule(g, channelsRule_, "CANAIS");
    }

    ui::paintModeFrame(g, getLocalBounds(), mood_);
}

bool PedalLooperEditor::keyPressed(const juce::KeyPress& key) {
    const ui::PedalKeyAction action = ui::pedalActionForKey(key);
    if (!action.valid) {
        return false;
    }
    processor_.sendPedalEvent(action.button, action.gesture);
    return true;
}

void PedalLooperEditor::timerCallback() {
    LooperEngine& engine = processor_.engine();

    const bool recMode = (engine.mode() == GlobalMode::REC_MODE);
    const bool playing = engine.transportPlaying();
    const int selected = engine.selectedTrack();

    const ui::FrameMood mood = ui::moodFor(recMode, playing);
    if (mood != mood_) {
        mood_ = mood;
        repaint();
    }

    progressBar_.setProgress(engine.loopProgress(), engine.loopDefined(), mood);

    for (int i = 0; i < config::kNumTracks; ++i) {
        auto& strip = *trackStrips_[static_cast<size_t>(i)];
        strip.setSelected(recMode && (i == selected));
        strip.setTrackState(engine.trackState(i));
    }

    {
        PedalMap::State pedal;
        // O firmware envia o press, nao o release, entao "apertado" e
        // aproximado por "pressionado nos ultimos 180ms".
        const int64_t litFrames = static_cast<int64_t>(0.18 * engine.sampleRate());
        for (int i = 0; i < protocol::kButtonCount; ++i) {
            pedal.pressed[i] = engine.framesSincePress(i) < litFrames;
        }
        for (int t = 0; t < config::kNumTracks; ++t) {
            pedal.led[t] = engine.ledColor(t);
            pedal.ledBlink[t] = engine.ledBlink(t);
            pedal.level[t] = engine.trackLevel(t);
            pedal.recording = pedal.recording || (engine.trackState(t) == TrackState::RECORDING);
        }
        pedal.loopPosition = static_cast<float>(engine.loopProgress());
        pedal.loopDefined = engine.loopDefined();
        pedal.mood = mood;
        pedalMap_.updateState(pedal);
    }

    // Todo literal acentuado passa por ui::utf8 - ver o comentario em UiText.h.
    const juce::String sep = ui::utf8("   ·   ");
    juce::String status;
    if (!processor_.pedalEnabled()) {
        status = "PEDAL DESLIGADO";
    } else {
        status = processor_.pedalConnected() ? "PEDAL CONECTADO" : "PEDAL DESCONECTADO";
    }

    const double rate = engine.sampleRate();
    if (rate > 0.0) {
        status += sep + juce::String(rate / 1000.0, 1) + " kHz";
    }
    status += sep + ui::utf8("compensação ") + juce::String(engine.latencyTrimMs(), 1) + " ms";
    if (!processor_.mixOnMainOutput()) {
        status += sep + ui::utf8("mix só nas saídas de track");
    }
    statusLabel_.setText(status, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId,
                            (processor_.pedalEnabled() && !processor_.pedalConnected())
                                ? theme::amber
                                : theme::inkFaint);
}
