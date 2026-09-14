// Identidade visual do app.
//
// Premissa: esta tela e o FACEPLATE do pedal, nao um painel de software. O
// vocabulario e o de equipamento: caixa de aluminio anodizado, legenda
// serigrafada, poco rebaixado, medidor de LED segmentado. Por isso o fundo e
// grafite QUENTE e nao preto (preto puro so existe em tela, nao em caixa
// pintada), e a profundidade vem de um fio de luz na aresta de cima com
// sombra embaixo - do jeito que a luz pega numa peca fresada - em vez de
// bordas chapadas.
//
// As duas cores de acento nao foram escolhidas por gosto: sao as cores dos
// LEDs bicolores do proprio hardware. Vermelho e verde significam na tela
// exatamente o que significam no pedal, e nada mais usa essas cores.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme {

// CONTRASTE: esta interface e lida A DISTANCIA - a tela virada para quem
// assiste fica a metros de quem le. Os tons medios que funcionavam num monitor
// a 60 cm sumiam a tres metros, entao a paleta foi aberta: o fundo desceu, a
// serigrafia subiu, e os dois cinzas secundarios pararam de ser "quase o
// fundo". Nenhuma informacao depende SO de cor - todo estado tambem esta
// escrito por extenso, para quem nao distingue vermelho de verde.
//
// --- Caixa ---------------------------------------------------------------
// Fundo PRETO, nao mais grafite: e o maior contraste possivel contra a
// serigrafia clara e contra os LEDs, e e o que sustenta a leitura a metros de
// distancia. Os painies ficam poucos passos acima do preto - o suficiente para
// terem forma de peca, longe o bastante para nao clarear a tela.
const juce::Colour enclosure{0xff000000};  // corpo
const juce::Colour panel{0xff101216};      // face fresada, um degrau acima
const juce::Colour panelRaised{0xff1b1f25};
const juce::Colour groove{0xff000000};     // poco rebaixado (medidor, curso do fader)
const juce::Colour hairline{0xff565e69};

// --- Serigrafia ----------------------------------------------------------
// Contraste sobre a caixa: ink ~14:1, inkDim ~7,5:1, inkFaint ~4,6:1. O mais
// fraco dos tres so carrega texto de apoio (rodape, unidades) - nunca um
// estado.
const juce::Colour ink{0xfff0f2f5};        // legenda
const juce::Colour inkDim{0xffaeb4bc};     // legenda secundaria
const juce::Colour inkFaint{0xff848b94};

// --- LEDs do hardware ----------------------------------------------------
// Saturacao no talo, como LED de verdade aceso: sobre o preto, e o que se ve
// primeiro do outro lado da sala. Nao adianta clarear (ir para o rosa ou para
// o verde-agua) - o que carrega a distancia e a saturacao, nao a luminancia.
const juce::Colour ledRed{0xffff1f14};
const juce::Colour ledGreen{0xff00f57a};
const juce::Colour amber{0xffffb400};      // so a zona de atencao do medidor

// Fontes resolvidas uma vez no primeiro uso, com fallback se a familia nao
// estiver instalada.
//
// Bahnschrift e derivada da DIN, a norma de letreiro de painel industrial -
// e a letra que ja estaria serigrafada nessa caixa. Cascadia Mono entra so
// nos numeros, por ser tabular: sem isso o "100 %" muda de largura ao virar
// "98 %" e o valor treme enquanto se mexe no fader.
juce::Font legend(float height, bool strong = false);  // legendas em caixa alta
juce::Font display(float height);                       // nomes de track, leitura a distancia
juce::Font data(float height);                          // numeros

// Fio de luz em cima + sombra embaixo, para dar relevo a um painel.
void paintPanel(juce::Graphics& g, juce::Rectangle<int> bounds, bool raised = false);

// Poco rebaixado (o oposto: sombra em cima, luz embaixo).
void paintGroove(juce::Graphics& g, juce::Rectangle<float> bounds, float radius);

// Regua fina com a legenda apoiada nela, do jeito que uma serigrafia divide
// as secoes de um painel de mesa.
void paintSectionRule(juce::Graphics& g, juce::Rectangle<int> row, const juce::String& text);

} // namespace theme

class PedalLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PedalLookAndFeel();

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                           juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
};
