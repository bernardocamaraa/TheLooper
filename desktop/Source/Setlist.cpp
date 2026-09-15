#include "Setlist.h"

#include "UiText.h"

const SetlistSong* SetlistModel::song(int index) const {
    if (index < 0 || index >= size()) {
        return nullptr;
    }
    return &data.songs[static_cast<size_t>(index)];
}

const SetlistSong* SetlistModel::currentSong() const {
    return song(current);
}

juce::String SetlistModel::statusText() const {
    const SetlistSong* now = currentSong();
    if (now == nullptr) {
        return {};
    }
    return ui::utf8("Música ") + juce::String(current + 1) + "/" + juce::String(size()) + ui::utf8("  ·  ") +
           now->name;
}

namespace setlistfile {

juce::String save(const juce::File& file, const Setlist& setlist) {
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("title", setlist.title);
    juce::Array<juce::var> songs;
    for (const auto& song : setlist.songs) {
        auto item = std::make_unique<juce::DynamicObject>();
        item->setProperty("name", song.name);
        juce::Array<juce::var> tracks;
        for (const auto& name : song.tracks) {
            tracks.add(name);
        }
        item->setProperty("tracks", tracks);
        songs.add(juce::var(item.release()));
    }
    root->setProperty("songs", songs);

    const juce::String json = juce::JSON::toString(juce::var(root.release()), false);
    file.getParentDirectory().createDirectory();
    if (!file.replaceWithText(json, false, false, "\n")) {
        return ui::utf8("Não consegui salvar em ") + file.getFullPathName();
    }
    return {};
}

juce::String load(const juce::File& file, Setlist& setlist) {
    if (!file.existsAsFile()) {
        return ui::utf8("O arquivo não existe mais: ") + file.getFullPathName();
    }
    const juce::var root = juce::JSON::parse(file.loadFileAsString());
    if (!root.isObject()) {
        return ui::utf8("Este arquivo não é um setlist do The Looper.");
    }
    Setlist loaded;
    loaded.title = root["title"].toString();
    if (const auto* songs = root["songs"].getArray()) {
        for (const auto& item : *songs) {
            SetlistSong song;
            song.name = item["name"].toString();
            if (const auto* tracks = item["tracks"].getArray()) {
                for (int i = 0; i < config::kNumTracks && i < tracks->size(); ++i) {
                    song.tracks[static_cast<size_t>(i)] = (*tracks)[i].toString();
                }
            }
            loaded.songs.push_back(std::move(song));
        }
    }
    setlist = std::move(loaded);
    return {};
}

} // namespace setlistfile
