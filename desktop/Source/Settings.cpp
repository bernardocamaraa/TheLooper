#include "Settings.h"

// Para juce::Rectangle e juce::Desktop (validacao dos limites de janela).
#include <juce_gui_basics/juce_gui_basics.h>

namespace {
constexpr const char* kAudioDeviceStateKey = "audioDeviceState";

juce::String inputMaskKey(int track) {
    return "track" + juce::String(track) + "InputMask";
}

juce::String gainKey(int track) {
    return "track" + juce::String(track) + "Gain";
}

juce::String nameKey(int track) {
    return "track" + juce::String(track) + "Name";
}

juce::String inputGainKey(int channel) {
    return "input" + juce::String(channel) + "Gain";
}
} // namespace

Settings::Settings() {
    juce::PropertiesFile::Options options;
    options.applicationName = "Pedal Looper";
    options.folderName = "PEDAL";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);
}

Settings::~Settings() {
    flush();
}

juce::PropertiesFile* Settings::file() const {
    return properties_.getUserSettings();
}

std::unique_ptr<juce::XmlElement> Settings::audioDeviceState() const {
    auto* props = file();
    if (props == nullptr) {
        return nullptr;
    }
    return props->getXmlValue(kAudioDeviceStateKey);
}

void Settings::saveAudioDeviceState(std::unique_ptr<juce::XmlElement> state) {
    auto* props = file();
    if (props == nullptr || state == nullptr) {
        return;
    }
    props->setValue(kAudioDeviceStateKey, state.get());
}

uint32_t Settings::trackInputMask(int track) const {
    auto* props = file();
    if (props == nullptr) {
        return config::kDefaultInputMask;
    }
    const int value = props->getIntValue(inputMaskKey(track),
                                          static_cast<int>(config::kDefaultInputMask));
    // Descarta valores fora da faixa (arquivo editado a mao ou de uma versao
    // com outro numero de canais) em vez de rotear canais inexistentes.
    if (value < 0 || value > static_cast<int>(config::kAllInputsMask)) {
        return config::kDefaultInputMask;
    }
    return static_cast<uint32_t>(value);
}

void Settings::setTrackInputMask(int track, uint32_t mask) {
    if (auto* props = file()) {
        props->setValue(inputMaskKey(track), static_cast<int>(mask));
    }
}

float Settings::trackGain(int track) const {
    auto* props = file();
    if (props == nullptr) {
        return config::kDefaultTrackGain;
    }
    const double value = props->getDoubleValue(gainKey(track), config::kDefaultTrackGain);
    if (value < 0.0 || value > config::kMaxTrackGain) {
        return config::kDefaultTrackGain;
    }
    return static_cast<float>(value);
}

void Settings::setTrackGain(int track, float gain) {
    if (auto* props = file()) {
        props->setValue(gainKey(track), static_cast<double>(gain));
    }
}

juce::String Settings::trackName(int track) const {
    auto* props = file();
    const juce::String fallback = config::kDefaultTrackNames[track];
    if (props == nullptr) {
        return fallback;
    }
    const juce::String stored = props->getValue(nameKey(track), fallback).trim();
    return stored.isEmpty() ? fallback : stored.substring(0, config::kMaxTrackNameLength);
}

void Settings::setTrackName(int track, const juce::String& name) {
    if (auto* props = file()) {
        props->setValue(nameKey(track), name.trim().substring(0, config::kMaxTrackNameLength));
    }
}

float Settings::inputGain(int channel) const {
    auto* props = file();
    if (props == nullptr) {
        return config::kDefaultInputGain;
    }
    const double value = props->getDoubleValue(inputGainKey(channel), config::kDefaultInputGain);
    if (value < 0.0 || value > config::kMaxInputGain) {
        return config::kDefaultInputGain;
    }
    return static_cast<float>(value);
}

void Settings::setInputGain(int channel, float gain) {
    if (auto* props = file()) {
        props->setValue(inputGainKey(channel), static_cast<double>(gain));
    }
}

double Settings::latencyTrimMs() const {
    auto* props = file();
    if (props == nullptr) {
        return config::kDefaultLatencyTrimMs;
    }
    const double value = props->getDoubleValue("latencyTrimMs", config::kDefaultLatencyTrimMs);
    // O trim tem teto proprio para nao anular a compensacao nem estourar o
    // limite de config::kMaxLatencyMs.
    if (value < -config::kMaxLatencyMs || value > config::kMaxLatencyMs) {
        return config::kDefaultLatencyTrimMs;
    }
    return value;
}

void Settings::setLatencyTrimMs(double trimMs) {
    if (auto* props = file()) {
        props->setValue("latencyTrimMs", trimMs);
    }
}

bool Settings::softwareMonitoring() const {
    auto* props = file();
    return props != nullptr ? props->getBoolValue("softwareMonitoring", config::kDefaultSoftwareMonitoring)
                             : config::kDefaultSoftwareMonitoring;
}

void Settings::setSoftwareMonitoring(bool enabled) {
    if (auto* props = file()) {
        props->setValue("softwareMonitoring", enabled);
    }
}

bool Settings::metersVisible() const {
    auto* props = file();
    return props != nullptr ? props->getBoolValue("metersVisible", true) : true;
}

void Settings::setMetersVisible(bool visible) {
    if (auto* props = file()) {
        props->setValue("metersVisible", visible);
    }
}

int Settings::themeMode() const {
    auto* props = file();
    return juce::jlimit(0, 2, props != nullptr ? props->getIntValue("themeMode", 0) : 0);
}

void Settings::setThemeMode(int mode) {
    if (auto* props = file()) {
        props->setValue("themeMode", mode);
    }
}

int Settings::uiSizeLevel() const {
    auto* props = file();
    return juce::jlimit(0, 2, props != nullptr ? props->getIntValue("uiSizeLevel", 1) : 1);
}

void Settings::setUiSizeLevel(int level) {
    if (auto* props = file()) {
        props->setValue("uiSizeLevel", level);
    }
}

int Settings::trackPreset() const {
    auto* props = file();
    return props != nullptr ? props->getIntValue("trackPreset", 0) : 0;
}

void Settings::setTrackPreset(int preset) {
    if (auto* props = file()) {
        props->setValue("trackPreset", preset);
    }
}

juce::Colour Settings::trackColour(int track) const {
    auto* props = file();
    const juce::String stored =
        props != nullptr ? props->getValue("track" + juce::String(track) + "Colour") : juce::String();
    return stored.isEmpty() ? juce::Colours::transparentBlack : juce::Colour::fromString(stored);
}

void Settings::setTrackColour(int track, juce::Colour colour) {
    if (auto* props = file()) {
        props->setValue("track" + juce::String(track) + "Colour",
                        colour.isTransparent() ? juce::String() : colour.toString());
    }
}

juce::File Settings::setlistFile() const {
    auto* props = file();
    const juce::String stored = props != nullptr ? props->getValue("setlistFile") : juce::String();
    return stored.isEmpty() ? juce::File() : juce::File(stored);
}

void Settings::setSetlistFile(const juce::File& setlist) {
    if (auto* props = file()) {
        props->setValue("setlistFile", setlist.getFullPathName());
    }
}

int Settings::setlistIndex() const {
    auto* props = file();
    return props != nullptr ? props->getIntValue("setlistIndex", 0) : 0;
}

void Settings::setSetlistIndex(int index) {
    if (auto* props = file()) {
        props->setValue("setlistIndex", index);
    }
}

juce::Rectangle<int> Settings::ensureOnScreen(juce::Rectangle<int> bounds) {
    if (bounds.isEmpty()) {
        return {};
    }

    // O criterio e "da para pegar na barra de titulo": uma faixa util no topo
    // da janela precisa cair dentro da area de trabalho de algum monitor. Uma
    // janela que so encosta um canto na tela ja seria inarrastavel.
    const auto grabArea = bounds.withHeight(juce::jmin(bounds.getHeight(), 48));

    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays) {
        const auto visible = display.userArea.getIntersection(grabArea);
        if (visible.getWidth() >= 120 && visible.getHeight() >= 24) {
            return bounds;
        }
    }
    return {};
}

int Settings::windowDisplay(const juce::String& windowId) const {
    auto* props = file();
    return props != nullptr ? props->getIntValue(windowId + "Display", -1) : -1;
}

void Settings::setWindowDisplay(const juce::String& windowId, int displayIndex) {
    if (auto* props = file()) {
        props->setValue(windowId + "Display", displayIndex);
    }
}

juce::String Settings::page() const {
    auto* props = file();
    return props != nullptr ? props->getValue("page", "tocar") : juce::String("tocar");
}

void Settings::setPage(const juce::String& page) {
    if (auto* props = file()) {
        props->setValue("page", page);
    }
}

juce::String Settings::playView() const {
    auto* props = file();
    return props != nullptr ? props->getValue("playView", "cards") : juce::String("cards");
}

void Settings::setPlayView(const juce::String& view) {
    if (auto* props = file()) {
        props->setValue("playView", view);
    }
}

juce::String Settings::comPortOverride() const {
    auto* props = file();
    return props != nullptr ? props->getValue("comPortOverride", config::kComPortNameOverride).trim()
                             : juce::String(config::kComPortNameOverride);
}

void Settings::setComPortOverride(const juce::String& port) {
    if (auto* props = file()) {
        props->setValue("comPortOverride", port.trim().toUpperCase());
    }
}

juce::File Settings::recordingFolder() const {
    auto* props = file();
    const juce::String stored = props != nullptr ? props->getValue("recordingFolder") : juce::String();
    if (stored.isNotEmpty()) {
        return juce::File(stored);
    }
    // Padrao: Musicas/Pedal Looper. Nao aponta para nenhum caminho fixo da
    // maquina do usuario - quem quiser outro lugar escolhe na interface.
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Pedal Looper");
}

void Settings::setRecordingFolder(const juce::File& folder) {
    if (auto* props = file()) {
        props->setValue("recordingFolder", folder.getFullPathName());
    }
}

juce::Rectangle<int> Settings::windowBounds(const juce::String& windowId) const {
    auto* props = file();
    if (props == nullptr) {
        return {};
    }
    const juce::String stored = props->getValue(windowId + "Bounds");
    if (stored.isEmpty()) {
        return {};
    }
    const juce::Rectangle<int> bounds = juce::Rectangle<int>::fromString(stored);
    // Uma janela salva num monitor que nao existe mais ficaria inacessivel -
    // nesse caso devolve vazio e o chamador reposiciona.
    if (bounds.getWidth() < 200 || bounds.getHeight() < 150) {
        return {};
    }
    if (!juce::Desktop::getInstance().getDisplays().getTotalBounds(true).intersects(bounds)) {
        return {};
    }
    return bounds;
}

void Settings::setWindowBounds(const juce::String& windowId, juce::Rectangle<int> bounds) {
    if (auto* props = file()) {
        props->setValue(windowId + "Bounds", bounds.toString());
    }
}

void Settings::flush() {
    if (auto* props = file()) {
        props->saveIfNeeded();
    }
}
