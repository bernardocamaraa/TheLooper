#include "ui/Pages.h"

#include "Settings.h"
#include "UiText.h"

MixerPage::MixerPage(AppContext& context) : Page(context) {
    LooperEngine& engine = ctx_.engine();

    for (int i = 0; i < config::kNumTracks; ++i) {
        auto strip = std::make_unique<TrackControlStrip>();
        strip->setTrackIndex(i);
        strip->setLevelSource([this, i] { return ctx_.engine().trackLevel(i); });
        strip->setValues(ctx_.trackName(i), engine.trackInputMask(i), engine.trackGain(i));
        strip->onNameChanged = [this, i](juce::String name) { ctx_.setTrackName(i, name); };
        strip->useRenameDialog([this, i] {
            ui::askText("Renomear track " + juce::String(i + 1),
                        ui::utf8("O nome aparece nos cartões, no mixer, no pedal e na tela de performance."),
                        ctx_.trackName(i), "Renomear", [this, i](juce::String name) {
                            ctx_.setTrackName(i, name);
                            pageShown();
                        });
        });
        strip->onInputMaskChanged = [this, i](uint32_t mask) { ctx_.setTrackInputMask(i, mask); };
        strip->onGainChanged = [this, i](float gain) { ctx_.setTrackGain(i, gain); };
        strip->onColourClicked = [this, i] {
            ui::showTrackColourPicker(*strips_[static_cast<size_t>(i)], i,
                                      [this, i](juce::Colour colour) { ctx_.setTrackColour(i, colour); });
        };
        strip->refreshColours();
        addAndMakeVisible(*strip);
        strips_[static_cast<size_t>(i)] = std::move(strip);
    }

    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        auto row = std::make_unique<InputRow>();
        row->name.setText(ui::utf8(config::kInputChannelNames[ch]), juce::dontSendNotification);
        row->name.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(row->name);

        row->gain.setSliderStyle(juce::Slider::LinearHorizontal);
        row->gain.setRange(0.0, 100.0 * config::kMaxInputGain, 1.0);
        row->gain.setSkewFactorFromMidPoint(100.0);
        row->gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
        row->gain.setTextValueSuffix(" %");
        row->gain.setDoubleClickReturnValue(true, 100.0 * config::kDefaultInputGain);
        row->gain.onValueChange = [this, ch] {
            ctx_.setInputGain(ch, static_cast<float>(inputs_[static_cast<size_t>(ch)]->gain.getValue() / 100.0));
        };
        addAndMakeVisible(row->gain);

        row->reset.setButtonText("100%");
        row->reset.setWantsKeyboardFocus(false);
        row->reset.setTooltip("Voltar este trim para 100%");
        row->reset.onClick = [this, ch] {
            inputs_[static_cast<size_t>(ch)]->gain.setValue(100.0 * config::kDefaultInputGain, juce::sendNotificationSync);
        };
        addAndMakeVisible(row->reset);
        inputs_[static_cast<size_t>(ch)] = std::move(row);
    }

    ui::setButtonStyle(monitorSwitch_, "switch");
    monitorSwitch_.setClickingTogglesState(true);
    monitorSwitch_.setWantsKeyboardFocus(false);
    monitorSwitch_.setTooltip(ui::utf8("Deixe desligado se a interface já faz monitoramento direto por hardware - "
                                       "os dois juntos dão som de lata."));
    monitorSwitch_.onClick = [this] {
        const bool on = monitorSwitch_.getToggleState();
        ctx_.engine().setSoftwareMonitoring(on);
        ctx_.settings().setSoftwareMonitoring(on);
    };
    addAndMakeVisible(monitorSwitch_);

    pageShown();
}

void MixerPage::pageShown() {
    // Uma sessao aberta ou uma musica do setlist pode ter mudado tudo por fora.
    LooperEngine& engine = ctx_.engine();
    for (int i = 0; i < config::kNumTracks; ++i) {
        strips_[static_cast<size_t>(i)]->setValues(ctx_.trackName(i), engine.trackInputMask(i), engine.trackGain(i));
    }
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        inputs_[static_cast<size_t>(ch)]->gain.setValue(100.0 * static_cast<double>(engine.inputGain(ch)),
                                                        juce::dontSendNotification);
    }
    monitorSwitch_.setToggleState(engine.softwareMonitoring(), juce::dontSendNotification);
}

void MixerPage::refresh() {
    LooperEngine& engine = ctx_.engine();
    const bool recMode = engine.mode() == GlobalMode::REC_MODE;
    const int selected = engine.selectedTrack();
    for (int i = 0; i < config::kNumTracks; ++i) {
        auto& strip = *strips_[static_cast<size_t>(i)];
        strip.setSelected(recMode && i == selected);
        strip.setTrackState(engine.trackState(i));
    }
}

void MixerPage::refreshColours() {
    for (auto& strip : strips_) {
        strip->refreshColours();
    }
    for (auto& row : inputs_) {
        row->name.setColour(juce::Label::textColourId, theme::palette().t1);
    }
    repaint();
}

void MixerPage::resized() {
    auto area = getLocalBounds();
    header_ = takeHeader(area);
    const bool compact = getWidth() < theme::px(700);
    const int gap = theme::px(12);

    if (compact) {
        inputsCard_ = area.removeFromBottom(theme::px(220));
        area.removeFromBottom(gap);
        const int w = (area.getWidth() - gap) / 2;
        const int h = (area.getHeight() - gap) / 2;
        for (int i = 0; i < config::kNumTracks; ++i) {
            strips_[static_cast<size_t>(i)]->setBounds(area.getX() + (i % 2) * (w + gap), area.getY() + (i / 2) * (h + gap),
                                                       w, h);
        }
    } else {
        inputsCard_ = area.removeFromRight(juce::jmax(theme::px(230), area.getWidth() * 22 / 100));
        area.removeFromRight(gap);
        const int w = (area.getWidth() - gap * (config::kNumTracks - 1)) / config::kNumTracks;
        for (int i = 0; i < config::kNumTracks; ++i) {
            strips_[static_cast<size_t>(i)]->setBounds(area.removeFromLeft(w));
            area.removeFromLeft(gap);
        }
    }

    auto card = inputsCard_.reduced(theme::px(16));
    card.removeFromTop(theme::px(28)); // "ENTRADAS"
    for (auto& row : inputs_) {
        row->name.setFont(theme::text(theme::pxf(15.0f), theme::Weight::Semibold));
        row->name.setBounds(card.removeFromTop(theme::px(24)));
        auto line = card.removeFromTop(theme::px(36));
        row->reset.setBounds(line.removeFromRight(theme::px(58)).withSizeKeepingCentre(theme::px(58), theme::px(30)));
        line.removeFromRight(theme::px(6));
        row->gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, theme::px(58), theme::px(24));
        row->gain.setBounds(line);
        card.removeFromTop(theme::px(12));
    }
    monitorRow_ = card.removeFromTop(theme::px(52));
    monitorSwitch_.setBounds(monitorRow_.withLeft(monitorRow_.getRight() - theme::px(52))
                                 .withSizeKeepingCentre(theme::px(52), theme::px(30)));
}

void MixerPage::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    ui::paintPageTitle(g, header_, "Mixer", ui::utf8("Volume, entrada e medidor de cada track. O volume não altera o "
                                                    "que foi gravado."));
    theme::paintCard(g, inputsCard_.toFloat().reduced(2.0f), theme::pxf(14.0f));
    auto card = inputsCard_.reduced(theme::px(16));
    g.setColour(p.t3);
    g.setFont(theme::caps(theme::pxf(11.0f)));
    g.drawText("ENTRADAS", card.removeFromTop(theme::px(20)), juce::Justification::centredLeft, false);

    auto text = monitorRow_.withTrimmedRight(theme::px(60)).toFloat();
    g.setColour(p.line);
    g.fillRect(monitorRow_.getX(), monitorRow_.getY(), monitorRow_.getWidth(), 1);
    g.setColour(p.t1);
    g.setFont(theme::text(theme::pxf(14.0f), theme::Weight::Semibold));
    g.drawText("Ouvir a entrada", text.removeFromTop(text.getHeight() * 0.55f), juce::Justification::bottomLeft, false);
    g.setColour(p.t3);
    g.setFont(theme::text(theme::pxf(12.0f)));
    g.drawFittedText(ui::utf8("Desligue se a interface já monitora"), text.toNearestInt(), juce::Justification::topLeft,
                     1, 0.8f);
}
