#include "ui/Pages.h"

#include "Settings.h"
#include "UiText.h"
#include "ui/Brand.h"

namespace {
const char* sectionName(int section) {
    switch (section) {
        case 0: return "\xc3\x81udio";
        case 1: return "Pedal";
        case 2: return "Lat\xc3\xaancia";
        case 3: return "Apar\xc3\xaancia";
        case 4: return "Grava\xc3\xa7\xc3\xb5\x65s";
        case 5: return "Sobre";
        default: return "";
    }
}
} // namespace

SettingsPage::SettingsPage(AppContext& context) : Page(context) {
    for (int i = 0; i < SectionCount; ++i) {
        auto& b = nav_[static_cast<size_t>(i)];
        b.setButtonText(ui::utf8(sectionName(i)));
        ui::setButtonStyle(b, "ghost");
        b.setWantsKeyboardFocus(false);
        b.onClick = [this, i] { showSection(i); };
        addAndMakeVisible(b);
    }

    // --- Audio: o seletor do JUCE embutido na pagina (nada de dialogo) ---
    deviceSelector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
        ctx_.deviceManager(), 0, config::kNumChannels, 0, config::kNumChannels, false, false, false, false);
    addChildComponent(*deviceSelector_);

    // --- Pedal ---
    addChildComponent(pedalStatus_);
    comPort_.setText(ctx_.settings().comPortOverride(), juce::dontSendNotification);
    comPort_.setTextToShowWhenEmpty(ui::utf8("automático"), theme::palette().t3);
    comPort_.onTextChange = [this] { ctx_.settings().setComPortOverride(comPort_.getText()); };
    addChildComponent(comPort_);
    reconnect_.setButtonText("Reconectar");
    reconnect_.setWantsKeyboardFocus(false);
    reconnect_.onClick = [this] { ctx_.reconnectPedal(); };
    addChildComponent(reconnect_);

    // --- Latencia ---
    latencyInfo_.setJustificationType(juce::Justification::centredRight);
    addChildComponent(latencyInfo_);
    latencyTrim_.setSliderStyle(juce::Slider::LinearHorizontal);
    latencyTrim_.setRange(-50.0, 150.0, 0.5);
    latencyTrim_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 26);
    latencyTrim_.setTextValueSuffix(" ms");
    latencyTrim_.setDoubleClickReturnValue(true, config::kDefaultLatencyTrimMs);
    latencyTrim_.onValueChange = [this] {
        const double trim = latencyTrim_.getValue();
        ctx_.engine().setLatencyTrimMs(trim);
        ctx_.settings().setLatencyTrimMs(trim);
    };
    addChildComponent(latencyTrim_);
    ui::setButtonStyle(monitorSwitch_, "switch");
    monitorSwitch_.setClickingTogglesState(true);
    monitorSwitch_.setWantsKeyboardFocus(false);
    monitorSwitch_.onClick = [this] {
        const bool on = monitorSwitch_.getToggleState();
        ctx_.engine().setSoftwareMonitoring(on);
        ctx_.settings().setSoftwareMonitoring(on);
    };
    addChildComponent(monitorSwitch_);

    // --- Aparencia ---
    themeMode_.setOptions({"Escuro", "Claro", "Sistema"});
    themeMode_.onChange = [this](int index) {
        ctx_.settings().setThemeMode(index);
        ctx_.applyAppearance();
    };
    addChildComponent(themeMode_);
    uiSize_.setOptions({"Compacto", ui::utf8("Padrão"), "Grande"});
    uiSize_.onChange = [this](int index) {
        ctx_.settings().setUiSizeLevel(index);
        ctx_.applyAppearance();
    };
    addChildComponent(uiSize_);
    trackPreset_.setOptions({"Vivas", "Contraste", "Suaves"});
    trackPreset_.onChange = [this](int index) {
        ctx_.settings().setTrackPreset(index);
        // A paleta inicial vale para as quatro: tira as cores proprias.
        for (int t = 0; t < config::kNumTracks; ++t) {
            ctx_.setTrackColour(t, juce::Colours::transparentBlack);
        }
        ctx_.applyAppearance();
    };
    addChildComponent(trackPreset_);
    for (int t = 0; t < config::kNumTracks; ++t) {
        auto& b = trackColour_[static_cast<size_t>(t)];
        ui::setButtonStyle(b, "big");
        b.setWantsKeyboardFocus(false);
        b.onClick = [this, t] {
            ui::showTrackColourPicker(trackColour_[static_cast<size_t>(t)], t,
                                      [this, t](juce::Colour colour) { ctx_.setTrackColour(t, colour); });
        };
        addChildComponent(b);
    }

    // --- Gravacoes ---
    addChildComponent(recordingPath_);
    openRecordings_.setButtonText("Abrir pasta");
    ui::setButtonIcon(openRecordings_, "folder");
    openRecordings_.onClick = [this] {
        const juce::File folder = ctx_.recordingFolder();
        folder.createDirectory();
        folder.revealToUser();
    };
    addChildComponent(openRecordings_);
    chooseRecordings_.setButtonText(ui::utf8("Escolher…"));
    chooseRecordings_.onClick = [this] { ctx_.chooseRecordingFolder(); };
    addChildComponent(chooseRecordings_);
    openLoops_.setButtonText("Abrir pasta");
    ui::setButtonIcon(openLoops_, "folder");
    openLoops_.onClick = [this] { ctx_.loopsFolder().revealToUser(); };
    addChildComponent(openLoops_);

    showSection(section_);
    pageShown();
}

SettingsPage::~SettingsPage() = default;

void SettingsPage::showSection(int section) {
    section_ = juce::jlimit(0, SectionCount - 1, section);
    for (int i = 0; i < SectionCount; ++i) {
        nav_[static_cast<size_t>(i)].setToggleState(i == section_, juce::dontSendNotification);
    }
    deviceSelector_->setVisible(section_ == Audio);
    for (juce::Component* c : {static_cast<juce::Component*>(&pedalStatus_), static_cast<juce::Component*>(&comPort_),
                               static_cast<juce::Component*>(&reconnect_)}) {
        c->setVisible(section_ == Pedal);
    }
    for (juce::Component* c : {static_cast<juce::Component*>(&latencyInfo_), static_cast<juce::Component*>(&latencyTrim_),
                               static_cast<juce::Component*>(&monitorSwitch_)}) {
        c->setVisible(section_ == Latency);
    }
    themeMode_.setVisible(section_ == Appearance);
    uiSize_.setVisible(section_ == Appearance);
    trackPreset_.setVisible(section_ == Appearance);
    for (auto& b : trackColour_) {
        b.setVisible(section_ == Appearance);
    }
    for (juce::Component* c : {static_cast<juce::Component*>(&openRecordings_),
                               static_cast<juce::Component*>(&chooseRecordings_), static_cast<juce::Component*>(&openLoops_)}) {
        c->setVisible(section_ == Recordings);
    }
    recordingPath_.setVisible(false);
    resized();
    repaint();
}

void SettingsPage::pageShown() {
    Settings& s = ctx_.settings();
    themeMode_.setSelected(s.themeMode());
    uiSize_.setSelected(s.uiSizeLevel());
    trackPreset_.setSelected(s.trackPreset());
    latencyTrim_.setValue(ctx_.engine().latencyTrimMs(), juce::dontSendNotification);
    monitorSwitch_.setToggleState(ctx_.engine().softwareMonitoring(), juce::dontSendNotification);
    comPort_.setText(s.comPortOverride(), juce::dontSendNotification);
    refreshColours();
    refresh();
}

void SettingsPage::refresh() {
    const auto& p = theme::palette();
    const bool connected = ctx_.pedalConnected();
    pedalStatus_.setText(connected ? "Conectado" : "Desconectado", juce::dontSendNotification);
    pedalStatus_.setColour(juce::Label::textColourId, connected ? p.play : p.amber);
    latencyInfo_.setText(juce::String(ctx_.engine().latencyMs(), 1) + " ms", juce::dontSendNotification);
}

void SettingsPage::refreshColours() {
    const auto& p = theme::palette();
    for (int t = 0; t < config::kNumTracks; ++t) {
        auto& b = trackColour_[static_cast<size_t>(t)];
        b.setColour(juce::TextButton::buttonColourId, theme::track(t));
        b.setButtonText(juce::String(t + 1) + ui::utf8("  ·  ") + ctx_.trackName(t));
    }
    latencyInfo_.setColour(juce::Label::textColourId, p.t1);
    comPort_.setTextToShowWhenEmpty(ui::utf8("automático"), p.t3);
    repaint();
}

juce::Rectangle<int> SettingsPage::addRow(juce::Rectangle<int>& area, const juce::String& title,
                                          const juce::String& subtitle, int controlWidth, int height) {
    auto row = area.removeFromTop(height > 0 ? height : theme::px(68));
    rows_.push_back({title, subtitle, row});
    return row.removeFromRight(controlWidth).withSizeKeepingCentre(controlWidth, theme::px(38));
}

void SettingsPage::resized() {
    auto area = getLocalBounds();
    header_ = takeHeader(area);
    const bool compact = getWidth() < theme::px(700);

    if (compact) {
        auto navArea = area.removeFromTop(theme::px(84));
        area.removeFromTop(theme::px(12));
        const int w = (navArea.getWidth() - theme::px(12)) / 3;
        for (int i = 0; i < SectionCount; ++i) {
            nav_[static_cast<size_t>(i)].setBounds(navArea.getX() + (i % 3) * (w + theme::px(6)),
                                                   navArea.getY() + (i / 3) * theme::px(44), w, theme::px(38));
        }
    } else {
        auto navArea = area.removeFromLeft(theme::px(200));
        area.removeFromLeft(theme::px(18));
        for (auto& b : nav_) {
            b.setBounds(navArea.removeFromTop(theme::px(44)));
            navArea.removeFromTop(theme::px(4));
        }
    }

    content_ = area;
    rows_.clear();
    about_ = {};
    auto inner = content_.reduced(theme::px(22), theme::px(10));
    const auto font = theme::text(theme::pxf(15.0f));
    pedalStatus_.setFont(theme::text(theme::pxf(15.0f), theme::Weight::Semibold));
    latencyInfo_.setFont(theme::numbers(theme::pxf(15.0f), true));
    comPort_.setFont(font);

    switch (section_) {
        case Audio:
            deviceSelector_->setBounds(inner.reduced(0, theme::px(8)));
            break;
        case Pedal:
            pedalStatus_.setJustificationType(juce::Justification::centredRight);
            pedalStatus_.setBounds(addRow(inner, "Status", "Arduino Mega pela porta serial (USB)", theme::px(200)));
            comPort_.setBounds(addRow(inner, "Porta", ui::utf8("Vazio = detectar o pedal sozinho pelo USB. Ex.: COM5"),
                                      theme::px(180)));
            reconnect_.setBounds(addRow(inner, "Reconectar", "Fecha e reabre a porta serial com a porta acima",
                                        theme::px(150)));
            break;
        case Latency:
            latencyInfo_.setBounds(addRow(inner, ui::utf8("Latência compensada"), "Driver + ajuste fino", theme::px(160)));
            latencyTrim_.setTextBoxStyle(juce::Slider::TextBoxRight, false, theme::px(80), theme::px(26));
            latencyTrim_.setBounds(addRow(inner, "Ajuste fino",
                                          "Positivo = grava mais cedo (use se o overdub entra atrasado)",
                                          juce::jmin(theme::px(360), inner.getWidth() / 2)));
            monitorSwitch_.setBounds(addRow(inner, "Ouvir a entrada",
                                            ui::utf8("Desligue se a interface já faz monitoramento direto"),
                                            theme::px(56)));
            break;
        case Appearance: {
            const int w = juce::jmin(theme::px(330), inner.getWidth() / 2);
            themeMode_.setBounds(addRow(inner, "Tema", "Sistema acompanha o modo do Windows", w));
            uiSize_.setBounds(addRow(inner, "Tamanho da interface", "Para telas pequenas, touch ou vistas de longe", w));
            trackPreset_.setBounds(addRow(inner, "Paleta inicial", "Volta as quatro tracks para um conjunto pronto", w));
            addRow(inner, "Cor de cada track", ui::utf8("Toque numa track para escolher. Vale em todas as telas."), 0);
            auto buttons = inner.removeFromTop(theme::px(46));
            const int bw = (buttons.getWidth() - theme::px(10) * 3) / 4;
            for (auto& b : trackColour_) {
                b.setBounds(buttons.removeFromLeft(bw));
                buttons.removeFromLeft(theme::px(10));
            }
            break;
        }
        case Recordings: {
            auto buttons = addRow(inner, ui::utf8("Pasta das gravações"), ctx_.recordingFolder().getFullPathName(),
                                  theme::px(300));
            openRecordings_.setBounds(buttons.removeFromLeft(theme::px(146)));
            buttons.removeFromLeft(theme::px(8));
            chooseRecordings_.setBounds(buttons);
            openLoops_.setBounds(addRow(inner, ui::utf8("Sessões, setlists e stems"),
                                        ctx_.loopsFolder().getParentDirectory().getFullPathName(), theme::px(146)));
            break;
        }
        case About:
            about_ = inner;
            break;
        default:
            break;
    }
}

void SettingsPage::paint(juce::Graphics& g) {
    const auto& p = theme::palette();
    ui::paintPageTitle(g, header_, "Ajustes", ui::utf8("Tudo o que se configura uma vez, num lugar só."));
    theme::paintCard(g, content_.toFloat().reduced(1.0f), theme::pxf(16.0f));

    for (size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        if (i > 0) {
            g.setColour(p.line);
            g.fillRect(row.area.getX(), row.area.getY(), row.area.getWidth(), 1);
        }
        auto text = row.area.toFloat().withTrimmedRight(theme::pxf(12.0f));
        g.setColour(p.t1);
        g.setFont(theme::text(theme::pxf(15.0f), theme::Weight::Semibold));
        g.drawText(row.title, text.removeFromTop(text.getHeight() * 0.52f), juce::Justification::bottomLeft, false);
        g.setColour(p.t3);
        g.setFont(theme::text(theme::pxf(12.5f)));
        g.drawFittedText(row.subtitle, text.toNearestInt().withTrimmedTop(2), juce::Justification::topLeft, 1, 0.8f);
    }

    if (!about_.isEmpty()) {
        auto r = about_.toFloat();
        auto logo = r.removeFromTop(juce::jmin(r.getHeight() * 0.5f, theme::pxf(220.0f)));
        brand::draw(g, brand::full(), logo.reduced(theme::pxf(10.0f)), p.t1);
        r.removeFromTop(theme::pxf(12.0f));
        g.setColour(p.t1);
        g.setFont(theme::text(theme::pxf(17.0f), theme::Weight::Semibold));
        g.drawText(ui::utf8("The Looper  ·  versão 2.0"), r.removeFromTop(theme::pxf(28.0f)), juce::Justification::centred,
                   false);
        g.setColour(p.t3);
        g.setFont(theme::text(theme::pxf(13.0f)));
        g.drawFittedText(ui::utf8("Looper de 4 tracks com camadas infinitas, feito para tocar ao vivo com a pedaleira "
                                  "The Looper. Fontes do sistema (Segoe UI Variable e Cascadia Mono)."),
                         r.removeFromTop(theme::pxf(60.0f)).toNearestInt(), juce::Justification::centredTop, 3, 0.9f);
    }
}
