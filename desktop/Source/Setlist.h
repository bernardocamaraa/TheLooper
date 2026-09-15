// SETLIST: a ORDEM das musicas de um show.
//
// O loop e sempre gravado AO VIVO: o setlist nao carrega audio nem tem tempo
// de loop pre-definido. Cada musica guarda so o que prepara o palco - o nome
// de cada track. "Proxima" limpa o loop e aplica os nomes da musica seguinte.
//
// Arquivo .setlist = JSON legivel:
//   {"title": "Show de sabado", "songs": [{"name": "Vento Sul",
//     "tracks": ["Violao", "Voz", "Coro", "Palmas"]}]}
#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "Config.h"

struct SetlistSong {
    juce::String name;
    std::array<juce::String, config::kNumTracks> tracks;
};

struct Setlist {
    juce::String title;
    std::vector<SetlistSong> songs;
};

// O setlist aberto, o arquivo dele e a musica atual.
class SetlistModel {
public:
    Setlist data;
    juce::File file;   // vazio = ainda nao salvo
    int current = 0;

    bool empty() const { return data.songs.empty(); }
    int size() const { return static_cast<int>(data.songs.size()); }
    const SetlistSong* currentSong() const;
    const SetlistSong* song(int index) const;

    // Texto do HUD: "Musica 2/6 - Vento Sul" (vazio sem setlist).
    juce::String statusText() const;
};

namespace setlistfile {

constexpr const char* kExtension = ".setlist";
constexpr const char* kWildcard = "*.setlist";

// Vazio = ok; senao a mensagem de erro, pronta para mostrar.
juce::String save(const juce::File& file, const Setlist& setlist);
juce::String load(const juce::File& file, Setlist& setlist);

} // namespace setlistfile
