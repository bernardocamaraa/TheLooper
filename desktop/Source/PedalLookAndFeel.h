// Identidade visual do The Looper ("Keynote no palco").
//
// Calma por padrao, gritante quando importa: fundo neutro, tipografia grande
// e limpa, muito respiro - e cor forte SO para estado. Vermelho = gravando,
// verde = tocando (as cores dos LEDs do pedal), ambar = atencao. Cada track
// tem uma cor propria, a mesma em todas as telas, que o usuario pode trocar.
//
// Dois temas (escuro e claro) + "sistema". Os tokens mudam em tempo real:
// setMode() troca a paleta, atualiza os apelidos antigos (ink, panel...) e
// quem chamou repinta. Nenhuma informacao depende SO de cor - todo estado
// tambem aparece escrito.
#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme {

// --- Paleta ----------------------------------------------------------------

struct Palette {
    juce::Colour bg;     // fundo da janela
    juce::Colour s1;     // cartao
    juce::Colour s2;     // campo, controle
    juce::Colour s3;     // trilho, selecionado, poco
    juce::Colour line;   // divisoria
    juce::Colour line2;  // contorno de controle
    juce::Colour t1;     // texto
    juce::Colour t2;     // texto secundario
    juce::Colour t3;     // texto de apoio
    juce::Colour rec;    // gravando / selecionado em REC
    juce::Colour play;   // tocando / medidor
    juce::Colour amber;  // atencao
};

enum class Mode { Dark, Light, System };

// Troca o tema. Quem chama e responsavel por repintar as janelas (ver
// MainComponent::applyTheme).
void setMode(Mode mode);
Mode mode();
bool isDark();
const Palette& palette();

// --- Cor de cada track -----------------------------------------------------
//
// Parte de uma paleta inicial (Vivas / Contraste / Suaves, com versao para
// cada tema) e cada track pode ter uma cor propria escolhida pelo usuario.

constexpr int kTrackPresetCount = 3;
juce::String trackPresetName(int preset);
void setTrackPreset(int preset);
int trackPreset();
juce::Colour presetTrackColour(int preset, int track);

juce::Colour track(int index);
// transparentBlack = volta para a cor da paleta inicial.
void setTrackColour(int index, juce::Colour colour);
juce::Colour customTrackColour(int index);

// As 12 cores prontas do seletor de cor (nome + cor).
struct Swatch {
    const char* name;
    juce::uint32 argb;
};
const std::array<Swatch, 12>& trackSwatches();

// Vermelho e verde sao do ESTADO; uma track dessas cores confunde no palco.
// Devolve o aviso (vazio se a cor esta boa).
juce::String trackColourWarning(juce::Colour colour);

// --- Apelidos da paleta antiga ----------------------------------------------
//
// Os widgets e o plugin foram escritos com estes nomes. Continuam valendo,
// mas agora sao a paleta ativa (setMode atualiza todos).
inline juce::Colour enclosure{0xff0b0c0f};
inline juce::Colour panel{0xff15171c};
inline juce::Colour panelRaised{0xff1c1f25};
inline juce::Colour groove{0xff262a32};
inline juce::Colour hairline{0xff363b45};
inline juce::Colour ink{0xfff5f6f8};
inline juce::Colour inkDim{0xffa7adb8};
inline juce::Colour inkFaint{0xff767d89};
inline juce::Colour ledRed{0xffff3b30};
inline juce::Colour ledGreen{0xff30d158};
inline juce::Colour amber{0xffffb020};

// --- Tipografia -------------------------------------------------------------
//
// Segoe UI Variable (Windows 11; cai para Segoe UI no 10): a sans limpa do
// sistema, com cara de keynote. Numeros em Cascadia Mono, que e tabular - sem
// isso o relogio do loop "treme" enquanto conta.

enum class Weight { Light, Regular, Semibold, Bold };
juce::Font text(float height, Weight weight = Weight::Regular);
// Rotulo pequeno em caixa alta com espacamento ("ENTRADAS", "LOOP").
juce::Font caps(float height);
juce::Font numbers(float height, bool bold = false);

// Nomes antigos (widgets e plugin).
juce::Font legend(float height, bool strong = false);
juce::Font display(float height);
juce::Font data(float height);

// --- Escala -----------------------------------------------------------------
//
// Toda medida de layout passa por px(): a interface cresce com a janela (a
// referencia e 1040 px de largura) e com o "tamanho da interface" escolhido
// em Ajustes. Nada de pixel fixo no layout.

void setScale(float scale);
float scale();
int px(float value);
float pxf(float value);
// Compacto / Padrao / Grande (0, 1, 2).
void setSizeLevel(int level);
int sizeLevel();
float sizeFactor();

// --- Pintura ---------------------------------------------------------------

// Cartao: superficie s1, contorno fino e sombra difusa curta.
void paintCard(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, bool shadow = true);
// Contorno de selecao (track selecionada em REC): vermelho com halo.
void paintSelection(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour colour);
// Poco / trilho (medidor, fader, barra de progresso).
void paintGroove(juce::Graphics& g, juce::Rectangle<float> bounds, float radius);

// Nomes antigos.
void paintPanel(juce::Graphics& g, juce::Rectangle<int> bounds, bool raised = false);
void paintSectionRule(juce::Graphics& g, juce::Rectangle<int> row, const juce::String& text);

} // namespace theme

// Estilo de botao, lido pela LookAndFeel ("primary", "danger", "record",
// "ghost", "switch", "nav", "segment", "big"). Sem estilo = botao comum.
namespace ui {
void setButtonStyle(juce::Button& button, const juce::String& style);
juce::String buttonStyle(const juce::Button& button);
} // namespace ui

class PedalLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PedalLookAndFeel();

    // Reaplica as cores da paleta ativa (depois de theme::setMode).
    void refreshColours();

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

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

    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawAlertBox(juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea,
                      juce::TextLayout&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                       int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
};
