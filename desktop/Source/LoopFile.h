// ARQUIVO .loop - a musica inteira em disco, para chegar na feira com os
// loops prontos em vez de ter que grava-los na frente das pessoas.
//
// Guarda o que cada track TOCA (a soma das camadas), mais os ajustes de mesa:
// nome, entrada roteada, volume e se estava mutada. Abrir devolve as quatro
// tracks tocando o mesmo que tocavam na hora de salvar.
//
// O que NAO e guardado, de proposito:
//
// - As camadas separadas. Uma track volta com uma camada so, entao o UNDO
//   numa musica aberta apaga a track em vez de descascar o ultimo overdub.
//   Guardar camada a camada multiplicaria o tamanho do arquivo por seis para
//   preservar um historico que ninguem desfaz numa apresentacao.
// - O volume aplicado ao audio. O fader vai como numero, entao da para mexer
//   depois de abrir sem estragar o que foi gravado.
//
// FORMATO (little-endian, que e o que importa aqui: Windows em x86-64):
//
//   char     magic[8]      "PEDALLP1"
//   uint32   metaLength    tamanho do XML em bytes
//   char     meta[]        XML UTF-8 com sampleRate, lengthSamples e os
//                          ajustes de cada track
//   float32  audio[]       para cada track COM audio, na ordem das tracks:
//                          lengthSamples * kNumChannels amostras intercaladas
//
// O XML na frente e proposital: da para abrir o arquivo num editor de texto e
// descobrir o que ele tem dentro sem nenhuma ferramenta.
#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "Config.h"

struct LoopSession {
    struct Track {
        juce::String name;
        uint32_t inputMask = config::kDefaultInputMask;
        float gain = config::kDefaultTrackGain;
        bool muted = false;
        // Vazio = track sem audio. Caso contrario, lengthSamples * kNumChannels
        // amostras intercaladas.
        std::vector<float> audio;
    };

    double sampleRate = 0.0;
    int64_t lengthSamples = 0;
    std::array<Track, config::kNumTracks> tracks;

    bool hasAudio() const;
};

namespace loopfile {

// Extensao e filtro usados pelo seletor de arquivos.
constexpr const char* kExtension = ".loop";
constexpr const char* kWildcard = "*.loop";

// Devolvem uma mensagem de erro pronta para mostrar ao usuario, ou string
// vazia em caso de sucesso. Erro nunca e silencioso: perder uma musica salva
// na vespera da feira e o tipo de coisa que precisa aparecer na tela.
juce::String save(const juce::File& file, const LoopSession& session);
juce::String load(const juce::File& file, LoopSession& session);

// Ajusta a sessao para uma sample rate diferente da que foi gravada. Sem isto
// uma musica gravada a 48 kHz tocaria mais lenta (e mais grave) num device
// aberto a 44,1 - e o device nem sempre abre na mesma taxa da vez anterior.
void resampleTo(LoopSession& session, double targetSampleRate);

} // namespace loopfile
