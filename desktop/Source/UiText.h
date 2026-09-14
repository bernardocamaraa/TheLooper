// Textos de interface compartilhados pelas duas janelas.
#pragma once

#include <juce_core/juce_core.h>

#include "Config.h"

namespace ui {

// Todo literal com acento TEM de passar por aqui.
//
// juce::String(const char*) interpreta os bytes como Latin-1, nao como UTF-8:
// os fontes sao UTF-8 (/utf-8 no MSVC), entao "Violao" com til chegava na tela
// como "ViolA£o". Curiosamente String::operator+= faz o contrario e decodifica
// UTF-8, o que deixava metade da interface certa e metade quebrada - dai a
// necessidade de um caminho unico e explicito.
inline juce::String utf8(const char* text) {
    return juce::String(juce::CharPointer_UTF8(text));
}

// Nome legivel de uma combinacao de entradas: "Voz", "Violao",
// "Voz + Violao"... Gerado a partir de config::kInputChannelNames para que
// mudar kNumChannels (ou a fiacao) nao exija mexer na interface.
inline juce::String inputMaskName(uint32_t mask) {
    if (mask == 0) {
        return "Nenhuma";
    }
    juce::String name;
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        if (((mask >> ch) & 1u) != 0u) {
            if (name.isNotEmpty()) {
                name += " + ";
            }
            name += utf8(config::kInputChannelNames[ch]);
        }
    }
    return name;
}

} // namespace ui
