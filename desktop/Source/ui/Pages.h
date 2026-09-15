// As seis abas da janela principal: Tocar, Mixer, Sessoes, Setlist, Telas e
// Ajustes. Cada uma faz o proprio layout e fala com o motor pelo AppContext.
#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "LoopFile.h"
#include "Page.h"
#include "Icons.h"
#include "PedalMap.h"
#include "TrackControlStrip.h"

// --- Tocar ------------------------------------------------------------------
// Cartoes das 4 tracks + anel do loop + transporte com os rotulos do pedal, ou
// a vista do pedal (botoes na posicao do equipamento).
class PlayPage : public Page {
public:
    explicit PlayPage(AppContext& context);

    void refresh() override;
    void refreshColours() override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class RingCard : public juce::Component {
    public:
        ui::RingState ring;
        void paint(juce::Graphics& g) override;
    };

    void setPedalView(bool pedal);
    void updateTransport();

    ui::SegmentedControl viewToggle_;
    RingCard ring_;
    std::array<std::unique_ptr<ui::TrackCard>, config::kNumTracks> cards_;
    juce::TextButton recPlay_;
    juce::TextButton stop_;
    juce::TextButton undo_;
    juce::TextButton mode_;
    juce::TextButton clearAll_;
    juce::TextButton clearAllPedal_;
    juce::Label hint_;
    PedalMap pedal_;

    bool pedalView_ = false;
    juce::String subtitle_;
    juce::Rectangle<int> header_;
};

// --- Mixer ------------------------------------------------------------------
class MixerPage : public Page {
public:
    explicit MixerPage(AppContext& context);

    void pageShown() override;
    void refresh() override;
    void refreshColours() override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct InputRow {
        juce::Label name;
        juce::Slider gain;
        juce::TextButton reset;
    };

    std::array<std::unique_ptr<TrackControlStrip>, config::kNumTracks> strips_;
    std::array<std::unique_ptr<InputRow>, config::kNumChannels> inputs_;
    juce::TextButton monitorSwitch_;
    juce::Rectangle<int> header_;
    juce::Rectangle<int> inputsCard_;
    juce::Rectangle<int> monitorRow_;
};

// --- Sessoes ----------------------------------------------------------------
// Biblioteca dos .loop: abrir, salvar a atual, exportar stems, renomear,
// duplicar e mandar para a lixeira.
class SessionsPage : public Page {
public:
    explicit SessionsPage(AppContext& context);
    ~SessionsPage() override;

    void pageShown() override;
    void refreshColours() override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Entry {
        juce::File file;
        LoopInfo info;
        bool valid = false;
        juce::Time modified;
    };
    class Grid;

    void rescan();
    void applyFilter();
    void select(int visibleIndex);
    void updateButtons();
    const Entry* selectedEntry() const;

    void saveCurrent();
    void openSelected();
    void exportSelected();
    void renameSelected();
    void duplicateSelected();
    void trashSelected();

    std::vector<Entry> entries_;
    std::vector<int> visible_;
    int selected_ = -1;

    juce::TextEditor search_;
    juce::TextButton save_;
    juce::Viewport viewport_;
    std::unique_ptr<Grid> grid_;
    juce::TextButton open_;
    juce::TextButton export_;
    juce::TextButton rename_;
    juce::TextButton duplicate_;
    juce::TextButton trash_;
    juce::TextButton reveal_;

    juce::Rectangle<int> header_;
    juce::Rectangle<int> detail_;
    juce::Rectangle<int> detailText_;
    bool compact_ = false;
};

// --- Setlist ----------------------------------------------------------------
// A ordem das musicas do show. O loop e sempre gravado ao vivo.
class SetlistPage : public Page {
public:
    explicit SetlistPage(AppContext& context);
    ~SetlistPage() override;

    void pageShown() override;
    void refresh() override;
    void refreshColours() override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class SongList;

    void editSong(int index, bool isNew);
    void persist();
    void showOpenMenu();
    void updateNowCard();

    std::unique_ptr<SongList> list_;
    juce::Viewport viewport_;
    int selectedRow_ = 0;

    juce::TextButton new_;
    juce::TextButton open_;
    juce::TextButton save_;
    juce::TextButton add_;
    juce::TextButton edit_;
    juce::TextButton remove_;
    juce::TextButton up_;
    juce::TextButton down_;
    juce::TextButton next_;
    juce::TextButton prev_;
    juce::TextButton apply_;

    juce::Rectangle<int> header_;
    juce::Rectangle<int> nowCard_;
    juce::Rectangle<int> nowText_;
    bool armed_ = false;
    int shownCurrent_ = -1;
};

// --- Telas ------------------------------------------------------------------
// Mapa dos monitores: onde fica a tela de performance.
class ScreensPage : public Page {
public:
    explicit ScreensPage(AppContext& context);
    ~ScreensPage() override;

    void pageShown() override;
    void refresh() override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class MonitorMap;

    void updateAssign();

    std::unique_ptr<MonitorMap> map_;
    int selected_ = 0;
    ui::SegmentedControl role_;
    juce::TextButton metersToggle_;
    juce::Rectangle<int> header_;
    juce::Rectangle<int> assignCard_;
    juce::Rectangle<int> assignText_;
};

// --- Ajustes ----------------------------------------------------------------
class SettingsPage : public Page {
public:
    explicit SettingsPage(AppContext& context);
    ~SettingsPage() override;

    void pageShown() override;
    void refresh() override;
    void refreshColours() override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    enum Section { Audio, Pedal, Latency, Appearance, Recordings, About, SectionCount };
    struct Row {
        juce::String title;
        juce::String subtitle;
        juce::Rectangle<int> area;
    };

    void showSection(int section);
    juce::Rectangle<int> addRow(juce::Rectangle<int>& area, const juce::String& title, const juce::String& subtitle,
                                int controlWidth, int height = 0);

    std::array<juce::TextButton, SectionCount> nav_;
    int section_ = Appearance;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector_;

    juce::Label pedalStatus_;
    juce::TextEditor comPort_;
    juce::TextButton reconnect_;

    juce::Label latencyInfo_;
    juce::Slider latencyTrim_;
    juce::TextButton monitorSwitch_;

    ui::SegmentedControl themeMode_;
    ui::SegmentedControl uiSize_;
    ui::SegmentedControl trackPreset_;
    std::array<juce::TextButton, config::kNumTracks> trackColour_;

    juce::Label recordingPath_;
    juce::TextButton openRecordings_;
    juce::TextButton chooseRecordings_;
    juce::TextButton openLoops_;

    std::vector<Row> rows_;
    juce::Rectangle<int> header_;
    juce::Rectangle<int> content_;
    juce::Rectangle<int> about_;
};
