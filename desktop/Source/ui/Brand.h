// Marca The LOOPER: a logo do usuario (Assets/logo-*.png, extraidas de
// thelooper.jpg com o papel transformado em transparente) embutida no
// executavel. As imagens sao MASCARAS - so o alpha importa -, entao a logo e
// pintada na cor que o tema pedir: clara no escuro, escura no claro.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace brand {

const juce::Image& mark();      // circulo com seta + traco de loop
const juce::Image& wordmark();  // "The / LOOPER"
const juce::Image& full();      // marca em cima, wordmark embaixo

// Pinta a mascara na cor dada, encaixada (centralizada) na area.
void draw(juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> area, juce::Colour colour);

// Icone do app (marca branca num quadrado escuro arredondado).
juce::Image appIcon();

// Imagem da tela de abertura, na paleta ativa.
juce::Image splashImage(int width, int height, const juce::String& status);

} // namespace brand
