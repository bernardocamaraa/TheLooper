#include "ui/Pages.h"

#include <cmath>

#include "Settings.h"
#include "UiText.h"
#include "ui/Icons.h"

namespace {
// Mesma escala dos medidores: -48 dB no fundo.
float levelToFraction(float amplitude) {
    if (amplitude <= 0.0001f) {
        return 0.0f;
    }
    return juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(amplitude) + 48.0f) / 48.0f);
}

void setupButton(juce::Component& parent, juce::TextButton& button, const juce::String& text, const juce::String& icon,
                 const juce::String& style) {
    button.setButtonText(text);
    ui::setButtonIcon(button, icon);
    ui::setButtonStyle(button, style);
    button.setWantsKeyboardFocus(false);
    parent.addAndMakeVisible(button);
}
} // namespace

void PlayPage::RingCard::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    theme::paintCard(g, bounds, theme::pxf(16.0f));
    ui::paintLoopRings(g, bounds.reduced(theme::pxf(18.0f)), ring);
}

PlayPage::PlayPage(AppContext& context) : Page(context) {
    viewToggle_.setOptions({ui::utf8("Cartões"), "Pedal"});
    viewToggle_.onChange = [this](int index) {
        setPedalView(index == 1);
        ctx_.settings().setPlayView(index == 1 ? "pedal" : "cards");
    };
    addAndMakeVisible(viewToggle_);
    addAndMakeVisible(ring_);

    for (int i = 0; i < config::kNumTracks; ++i) {
        auto card = std::make_unique<ui::TrackCard>(i);
        // No REC o cartao seleciona a track; no PLAY ele muta - exatamente o
        // que o botao da track faz no pedal.
        card->onClick = [this, i] {
            ctx_.sendPedalEvent(static_cast<protocol::ButtonId>(protocol::kButtonTrack1 + i), protocol::kGesturePress);
        };
        card->onDoubleClick = [this, i] {
            ui::askText("Renomear track " + juce::String(i + 1),
                        ui::utf8("O nome aparece nos cartões, no mixer, no pedal e na tela de performance."),
                        ctx_.trackName(i), "Renomear", [this, i](juce::String name) { ctx_.setTrackName(i, name); });
        };
        addAndMakeVisible(*card);
        cards_[static_cast<size_t>(i)] = std::move(card);
    }

    setupButton(*this, recPlay_, "GRAVAR", "rec", "big");
    recPlay_.onClick = [this] { ctx_.sendPedalEvent(protocol::kButtonRecPlay, protocol::kGesturePress); };
    setupButton(*this, stop_, "STOP", "stop", {});
    stop_.onClick = [this] { ctx_.sendPedalEvent(protocol::kButtonPause, protocol::kGesturePress); };
    setupButton(*this, undo_, "DESFAZER", "undo", {});
    undo_.getProperties().set("subtitle", ui::utf8("tira a última camada"));
    undo_.onClick = [this] { ctx_.sendPedalEvent(protocol::kButtonUndo, protocol::kGesturePress); };
    setupButton(*this, mode_, "MODE", "mode", {});
    mode_.onClick = [this] { ctx_.sendPedalEvent(protocol::kButtonMode, protocol::kGesturePress); };
    setupButton(*this, clearAll_, "LIMPAR TUDO", "trash", "danger");
    clearAll_.onClick = [this] { ctx_.confirmClearAll(); };
    setupButton(*this, clearAllPedal_, "LIMPAR TUDO", "trash", "danger");
    clearAllPedal_.onClick = [this] { ctx_.confirmClearAll(); };

    hint_.setText(ui::utf8("Atalhos:   Espaço  PLAY+REC   ·   S  STOP   ·   U  desfazer   ·   Shift+U  limpa tudo   ·   "
                           "M  MODE   ·   1–4  tracks"),
                  juce::dontSendNotification);
    addAndMakeVisible(hint_);

    pedal_.onPedalEvent = [this](protocol::ButtonId button, protocol::Gesture gesture) {
        ctx_.sendPedalEvent(button, gesture);
    };
    addChildComponent(pedal_);

    const bool pedal = ctx_.settings().playView() == "pedal";
    viewToggle_.setSelected(pedal ? 1 : 0);
    setPedalView(pedal);
    refreshColours();
}

void PlayPage::setPedalView(bool pedal) {
    pedalView_ = pedal;
    ring_.setVisible(!pedal);
    for (auto& card : cards_) {
        card->setVisible(!pedal);
    }
    for (auto* b : {&recPlay_, &stop_, &undo_, &mode_, &clearAll_}) {
        b->setVisible(!pedal);
    }
    hint_.setVisible(!pedal);
    pedal_.setVisible(pedal);
    clearAllPedal_.setVisible(pedal);
    resized();
    repaint();
}

void PlayPage::updateTransport() {
    LooperEngine& engine = ctx_.engine();
    const bool recMode = engine.mode() == GlobalMode::REC_MODE;
    const bool playing = engine.transportPlaying();
    const int selected = engine.selectedTrack();
    const auto& p = theme::palette();

    juce::String label;
    juce::String subtitle;
    juce::Colour colour = p.rec;
    juce::String icon = "rec";
    if (!playing) {
        label = "PLAY";
        subtitle = ui::utf8("PLAY+REC  ·  retoma o loop");
        colour = p.play;
        icon = "play";
    } else if (!recMode) {
        label = "PLAY";
        subtitle = ui::utf8("PLAY+REC  ·  toca do início");
        colour = p.play;
        icon = "play";
    } else if (engine.trackState(selected) == TrackState::RECORDING) {
        label = "FECHAR TAKE";
        subtitle = ui::utf8("PLAY+REC  ·  encerra a camada");
    } else {
        label = "GRAVAR";
        subtitle = ui::utf8("PLAY+REC  ·  na track ") + juce::String(selected + 1);
    }

    if (recPlay_.getButtonText() != label || recPlay_.getProperties()["subtitle"].toString() != subtitle ||
        recPlay_.findColour(juce::TextButton::buttonColourId) != colour) {
        recPlay_.setButtonText(label);
        recPlay_.getProperties().set("subtitle", subtitle);
        ui::setButtonIcon(recPlay_, icon);
        recPlay_.setColour(juce::TextButton::buttonColourId, colour);
        recPlay_.repaint();
    }
}

void PlayPage::refresh() {
    LooperEngine& engine = ctx_.engine();
    const bool recMode = engine.mode() == GlobalMode::REC_MODE;
    const bool playing = engine.transportPlaying();
    const int selected = engine.selectedTrack();

    // Subtitulo de altura fixa: trocar de modo nao pode mexer no layout.
    const juce::String subtitle = recMode ? ui::utf8("Modo gravação  ·  toque num cartão para selecionar a track  ·  duplo clique renomeia")
                                          : ui::utf8("Modo play  ·  toque num cartão para mutar a track");
    if (subtitle != subtitle_) {
        subtitle_ = subtitle;
        repaint(header_);
    }

    if (!pedalView_) {
        for (int i = 0; i < config::kNumTracks; ++i) {
            ui::TrackCard::Data data;
            data.name = ctx_.trackName(i);
            data.state = engine.trackState(i);
            data.layers = engine.trackLayers(i);
            data.input = ui::inputMaskName(engine.trackInputMask(i));
            data.selected = recMode && i == selected;
            data.level = levelToFraction(engine.trackLevel(i));
            cards_[static_cast<size_t>(i)]->setData(data);
        }

        ui::RingState ring;
        ring.progress = engine.loopProgress();
        ring.defined = engine.loopDefined();
        ring.mood = ui::moodFor(recMode, playing);
        ring.lengthSeconds = engine.loopLengthSeconds();
        for (int i = 0; i < config::kNumTracks; ++i) {
            ring.states[static_cast<size_t>(i)] = engine.trackState(i);
        }
        ring_.ring = ring;
        ring_.repaint();
        updateTransport();
        return;
    }

    PedalMap::State pedal;
    // O firmware envia o press, nao o release: "apertado" = pressionado ha
    // menos de 180 ms.
    const int64_t litFrames = static_cast<int64_t>(0.18 * engine.sampleRate());
    for (int i = 0; i < protocol::kButtonCount; ++i) {
        pedal.pressed[i] = engine.framesSincePress(i) < litFrames;
    }
    for (int t = 0; t < config::kNumTracks; ++t) {
        pedal.led[t] = engine.ledColor(t);
        pedal.ledBlink[t] = engine.ledBlink(t);
        pedal.level[t] = engine.trackLevel(t);
        pedal.trackState[t] = engine.trackState(t);
        pedal.layers[t] = engine.trackLayers(t);
        pedal.names[t] = ctx_.trackName(t);
        pedal.recording = pedal.recording || pedal.trackState[t] == TrackState::RECORDING;
    }
    pedal.loopPosition = static_cast<float>(engine.loopProgress());
    pedal.loopDefined = engine.loopDefined();
    pedal.mood = ui::moodFor(recMode, playing);
    pedal.selectedTrack = recMode ? selected : -1;
    pedal.loopSeconds = engine.loopLengthSeconds();
    pedal.positionSeconds = engine.loopPositionSeconds();
    pedal.songText = ctx_.setlist().statusText();
    pedal_.updateState(pedal);
}

void PlayPage::refreshColours() {
    hint_.setColour(juce::Label::textColourId, theme::palette().t3);
    recPlay_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack); // forca a recomputar
    updateTransport();
    for (auto& card : cards_) {
        card->repaint();
    }
    pedal_.repaint();
    repaint();
}

void PlayPage::resized() {
    auto area = getLocalBounds();
    header_ = takeHeader(area);
    const bool compact = getWidth() < theme::px(640);
    const int toggleW = theme::px(compact ? 150.0f : 200.0f);
    viewToggle_.setBounds(header_.withLeft(header_.getRight() - toggleW).withSizeKeepingCentre(toggleW, theme::px(36))
                              .withX(header_.getRight() - toggleW));
    hint_.setFont(theme::text(theme::pxf(13.0f)));

    if (pedalView_) {
        auto bottom = area.removeFromBottom(theme::px(44));
        clearAllPedal_.setBounds(bottom.removeFromLeft(theme::px(200)));
        area.removeFromBottom(theme::px(12));
        pedal_.setBounds(area);
        return;
    }

    const int gap = theme::px(compact ? 9.0f : 12.0f);
    if (!compact) {
        hint_.setBounds(area.removeFromBottom(theme::px(24)));
        area.removeFromBottom(theme::px(8));
        auto transport = area.removeFromBottom(theme::px(74));
        area.removeFromBottom(theme::px(16));
        const float weights[] = {1.6f, 1.0f, 1.25f, 1.0f, 1.15f};
        juce::TextButton* buttons[] = {&recPlay_, &stop_, &undo_, &mode_, &clearAll_};
        const float totalW = 6.0f;
        const int available = transport.getWidth() - gap * 4;
        for (int i = 0; i < 5; ++i) {
            const int w = (i == 4) ? transport.getWidth()
                                   : juce::roundToInt(static_cast<float>(available) * weights[i] / totalW);
            buttons[i]->setBounds(transport.removeFromLeft(w));
            transport.removeFromLeft(gap);
        }
        hint_.setVisible(true);
    } else {
        hint_.setVisible(false);
        auto row2 = area.removeFromBottom(theme::px(56));
        area.removeFromBottom(gap);
        recPlay_.setBounds(area.removeFromBottom(theme::px(64)));
        area.removeFromBottom(theme::px(12));
        const int w = (row2.getWidth() - gap * 3) / 4;
        for (auto* b : {&stop_, &undo_, &mode_, &clearAll_}) {
            b->setBounds(row2.removeFromLeft(w));
            row2.removeFromLeft(gap);
        }
    }

    // Anel + cartoes (2 x 2).
    juce::Rectangle<int> cardsArea;
    if (!compact) {
        const int ringW = juce::jmin(area.getHeight(), area.getWidth() * 42 / 100);
        ring_.setBounds(area.removeFromLeft(ringW));
        area.removeFromLeft(theme::px(18));
        cardsArea = area;
    } else {
        const int ringH = juce::jmin(area.getWidth() * 55 / 100, area.getHeight() * 38 / 100);
        ring_.setBounds(area.removeFromTop(ringH).withSizeKeepingCentre(ringH, ringH));
        area.removeFromTop(gap);
        cardsArea = area;
    }
    const int cardW = (cardsArea.getWidth() - gap) / 2;
    const int cardH = (cardsArea.getHeight() - gap) / 2;
    for (int i = 0; i < config::kNumTracks; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        cards_[static_cast<size_t>(i)]->setBounds(cardsArea.getX() + col * (cardW + gap),
                                                  cardsArea.getY() + row * (cardH + gap), cardW, cardH);
    }
}

void PlayPage::paint(juce::Graphics& g) {
    ui::paintPageTitle(g, header_.withTrimmedRight(theme::px(220)), "Tocar", subtitle_);
}
