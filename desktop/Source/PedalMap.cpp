#include "PedalMap.h"

#include <cmath>
#include <cstring>

#include "PedalLookAndFeel.h"
#include "UiText.h"

namespace {
constexpr int kClearHoldMs = 3000; // igual ao config::kClearAllHoldMs do firmware

// --- Cores do equipamento (nao do tema do app) --------------------------
// O pedal e uma caixa PRETA com serigrafia clara, entao ele e desenhado com
// as cores dele mesmo. Fica lendo como um objeto apoiado no painel, que e
// exatamente o que e.
const juce::Colour kBodyTop{0xff17171a};
const juce::Colour kBodyBottom{0xff0a0a0c};
const juce::Colour kOutline{0xff34373d};
const juce::Colour kSwitchTopFace{0xff26282d};
const juce::Colour kSwitchBottomFace{0xff141518};
const juce::Colour kSwitchEdge{0xff3d4147};
const juce::Colour kLedOffFace{0xff1d1e21};
const juce::Colour kRingGroove{0xff08080a};
const juce::Colour kRingDim{0xff3a100e};

// --- Geometria, no espaco do desenho (1600 x 580) -----------------------
//
// Os switches ocupam a maior parte do desenho de proposito: a tela e usada
// num MONITOR TOUCH, onde o alvo tem de caber sob o dedo. Tudo o mais (anel,
// LEDs, margens) so pode crescer se sobrar espaco depois deles.
constexpr float kSwitchLeft = 26.0f;
constexpr float kSwitchRight = 1574.0f;
constexpr float kSwitchTop = 300.0f;
constexpr float kSwitchHeight = 254.0f;
constexpr float kSwitchGap = 14.0f;
constexpr float kSwitchWidth =
    ((kSwitchRight - kSwitchLeft) - kSwitchGap * (protocol::kButtonCount - 1)) / protocol::kButtonCount;

// Barrinha de LED acima de cada switch de track, como a marca acesa do
// equipamento.
constexpr float kLedBarY = 268.0f;
constexpr float kLedBarH = 12.0f;

// Visor dos VUs, onde o pedal real tem o display: ocupa a metade direita da
// area de cima, e os medidores dentro dele ficam alinhados com as colunas dos
// switches de track.
constexpr float kVisorX = 700.0f;
constexpr float kVisorY = 36.0f;
constexpr float kVisorW = 874.0f;
constexpr float kVisorH = 226.0f;

// Botao LIMPAR TUDO, entre o anel e o visor. Nao e um nono footswitch: e um
// controle de tela, com gesto proprio (segurar), para nao competir com os oito
// botoes que existem no equipamento.
constexpr float kClearX = 330.0f;
constexpr float kClearY = 92.0f;
constexpr float kClearW = 340.0f;
constexpr float kClearH = 112.0f;
constexpr int kClearAllHoldMs = 1800;  // tempo de hold do botao de tela
constexpr int kClearFlashMs = 900;     // duracao da confirmacao visual

// Anel do loop: fica onde esta o circulo vermelho do pedal, na esquerda.
constexpr float kRingCentreX = 162.0f;
constexpr float kRingCentreY = 148.0f;
constexpr float kRingRadius = 96.0f;    // raio da linha central do anel
constexpr float kRingThickness = 22.0f;

// Os rotulos sao os da serigrafia do pedal, nao os nomes internos: o desenho
// so serve se bater com o que esta escrito no equipamento.
juce::String switchLabel(int index) {
    switch (index) {
        case protocol::kButtonRecPlay: return "PLAY+REC";
        case protocol::kButtonPause: return "STOP";
        case protocol::kButtonUndo: return "CLEAR";
        case protocol::kButtonMode: return "MODE";
        case protocol::kButtonTrack1: return "1";
        case protocol::kButtonTrack2: return "2";
        case protocol::kButtonTrack3: return "3";
        case protocol::kButtonTrack4: return "4";
        default: return {};
    }
}

juce::Colour ledColourFor(protocol::LedColor color) {
    switch (color) {
        case protocol::kLedRed: return theme::ledRed;
        case protocol::kLedGreen: return theme::ledGreen;
        case protocol::kLedOrange: return theme::amber;
        case protocol::kLedOff:
        default: return kLedOffFace;
    }
}
} // namespace

PedalMap::PedalMap() {
    setInterceptsMouseClicks(true, false);
}

void PedalMap::updateState(const State& state) {
    // O piscar acompanha o relogio, nao o numero de repinturas, para bater com
    // o ritmo do firmware (config::kLedBlinkIntervalMs = 300ms).
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    const bool phase = ((now / 300) % 2) == 0;

    // O hold do LIMPAR TUDO e contado aqui, e nao num Timer proprio: esta
    // funcao ja e chamada a 30 Hz pela janela de controles.
    if (clearHolding_) {
        if (now - clearHoldStartMs_ >= static_cast<juce::uint32>(kClearAllHoldMs)) {
            clearHolding_ = false;
            clearedAtMs_ = now;
            if (onPedalEvent) {
                onPedalEvent(protocol::kButtonUndo, protocol::kGestureLongPress);
            }
        }
        repaint(art(kClearX, kClearY, kClearW, kClearH).getSmallestIntegerContainer());
    } else if (clearedAtMs_ != 0 && now - clearedAtMs_ <= static_cast<juce::uint32>(kClearFlashMs)) {
        repaint(art(kClearX, kClearY, kClearW, kClearH).getSmallestIntegerContainer());
    }

    // REPINTURA POR AREA.
    //
    // Duas coisas mudam a TODO quadro: o ponteiro do loop e os niveis dos VUs.
    // Repintar o desenho inteiro por causa delas era redesenhar caixa,
    // gradientes, oito switches e todo o texto 30 vezes por segundo - trabalho
    // suficiente para engasgar a thread da interface, ainda mais quando esta
    // janela esta espelhada num celular usado como segunda tela. O resto do
    // estado (switch pisado, LED, modo) muda por evento, e so ai vale
    // repintar tudo.
    State steady = state;
    State previous = state_;
    steady.loopPosition = previous.loopPosition = 0.0f;
    for (int t = 0; t < config::kNumTracks; ++t) {
        steady.level[t] = previous.level[t] = 0.0f;
    }
    const bool structuralChange =
        (phase != blinkPhase_) || std::memcmp(&steady, &previous, sizeof(State)) != 0;
    const bool motionOnly = !structuralChange && std::memcmp(&state, &state_, sizeof(State)) != 0;

    blinkPhase_ = phase;
    state_ = state;

    if (structuralChange) {
        repaint();
    } else if (motionOnly) {
        const float reach = kRingRadius + kRingThickness * 2.0f;
        repaint(art(kRingCentreX - reach, kRingCentreY - reach, reach * 2.0f, reach * 2.0f)
                    .getSmallestIntegerContainer());
        repaint(art(kVisorX, kVisorY, kVisorW, kVisorH).getSmallestIntegerContainer());
    }
}

juce::Rectangle<float> PedalMap::art(float x, float y, float w, float h) const {
    return {offsetX_ + x * scale_, offsetY_ + y * scale_, w * scale_, h * scale_};
}

juce::Rectangle<float> PedalMap::switchArt(int index) const {
    const float x = kSwitchLeft + static_cast<float>(index) * (kSwitchWidth + kSwitchGap);
    return {x, kSwitchTop, kSwitchWidth, kSwitchHeight};
}

void PedalMap::resized() {
    // Escala uniforme e centralizada: as proporcoes do pedal nao podem
    // distorcer, senao deixa de parecer o equipamento.
    const auto bounds = getLocalBounds().toFloat();
    scale_ = juce::jmin(bounds.getWidth() / kArtWidth, bounds.getHeight() / kArtHeight);
    offsetX_ = bounds.getX() + (bounds.getWidth() - kArtWidth * scale_) * 0.5f;
    offsetY_ = bounds.getY() + (bounds.getHeight() - kArtHeight * scale_) * 0.5f;
}

int PedalMap::switchAt(juce::Point<int> position) const {
    for (int i = 0; i < protocol::kButtonCount; ++i) {
        const auto sw = switchArt(i);
        if (art(sw.getX(), sw.getY(), sw.getWidth(), sw.getHeight()).contains(position.toFloat())) {
            return i;
        }
    }
    return -1;
}

void PedalMap::mouseDown(const juce::MouseEvent& e) {
    // O LIMPAR TUDO e testado ANTES dos switches: ele fica fora da fileira,
    // mas quem le o codigo tem de ver que ele tem prioridade de acerto.
    if (art(kClearX, kClearY, kClearW, kClearH).contains(e.getPosition().toFloat())) {
        clearHolding_ = true;
        clearHoldStartMs_ = juce::Time::getMillisecondCounter();
        mouseDownSwitch_ = -1;
        repaint();
        return;
    }

    mouseDownSwitch_ = switchAt(e.getPosition());
    mouseDownAtMs_ = juce::Time::getMillisecondCounter();

    if (mouseDownSwitch_ < 0) {
        return;
    }
    repaint();
    // Todo switch dispara no press-down, igual ao firmware - menos o CLEAR,
    // que precisa distinguir toque curto de hold e por isso so decide no
    // soltar (mesma logica do Button.h do firmware).
    if (mouseDownSwitch_ != protocol::kButtonUndo && onPedalEvent) {
        onPedalEvent(static_cast<protocol::ButtonId>(mouseDownSwitch_), protocol::kGesturePress);
    }
}

void PedalMap::mouseUp(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    if (clearHolding_) {
        // Soltou antes da hora: nao limpa nada. E o proprio ponto do gesto.
        clearHolding_ = false;
        repaint();
        return;
    }

    if (mouseDownSwitch_ == protocol::kButtonUndo && onPedalEvent) {
        const bool held = (juce::Time::getMillisecondCounter() - mouseDownAtMs_) >=
                           static_cast<juce::uint32>(kClearHoldMs);
        onPedalEvent(protocol::kButtonUndo,
                      held ? protocol::kGestureLongPress : protocol::kGesturePress);
    }
    mouseDownSwitch_ = -1;
    repaint();
}

// Anel do loop: uma volta do anel e uma volta do loop mestre. A cabeca
// luminosa gira no sentido horario a partir das 12h, entao da para ver de
// relance onde o loop esta sem ler numero nenhum - que e a unica pergunta que
// importa enquanto se toca.
void PedalMap::paintLoopRing(juce::Graphics& g) const {
    const auto centre = art(kRingCentreX, kRingCentreY, 0.0f, 0.0f).getPosition();
    const float radius = kRingRadius * scale_;
    const float thickness = juce::jmax(2.0f, kRingThickness * scale_);

    // Poco preto atras do anel, do jeito que o circulo e rebaixado na caixa.
    const float wellRadius = radius + thickness * 0.75f;
    g.setColour(kRingGroove);
    g.fillEllipse(centre.x - wellRadius, centre.y - wellRadius, wellRadius * 2.0f, wellRadius * 2.0f);

    // Trilho: o anel apagado. Enquanto o loop mestre nao existe ele e tudo o
    // que se ve - e pulsa em vermelho se a primeira gravacao estiver correndo,
    // porque nesse momento nao ha volta para mostrar, so o fato de estar
    // gravando.
    // A COR e a do modo global - a mesma da moldura das janelas: vermelho em
    // REC_MODE, verde em Mute Mode, cinza parado. Assim o anel responde as
    // duas perguntas de uma vez: em que modo estou e onde o loop esta.
    const juce::Colour moodColour = ui::frameColour(state_.mood);
    const bool primingPulse = state_.recording && !state_.loopDefined && blinkPhase_;
    g.setColour(primingPulse ? moodColour.withAlpha(0.75f)
                              : moodColour.withMultipliedSaturation(0.9f).darker(1.6f));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, thickness);

    if (!state_.loopDefined) {
        return;
    }

    const float position = juce::jlimit(0.0f, 1.0f, state_.loopPosition);
    const float sweep = position * juce::MathConstants<float>::twoPi;
    const juce::Colour arcColour = state_.recording ? theme::ledRed : moodColour;

    if (sweep > 0.001f) {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, 0.0f, sweep, true);

        // Halo primeiro, arco depois: o vidro do LED espalha luz na caixa.
        g.setColour(arcColour.withAlpha(0.22f));
        g.strokePath(arc, juce::PathStrokeType(thickness * 2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        g.setColour(arcColour);
        g.strokePath(arc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // Cabeca: o ponto que gira. E ele que da a leitura de velocidade do loop.
    const float headX = centre.x + radius * std::sin(sweep);
    const float headY = centre.y - radius * std::cos(sweep);
    const float headRadius = thickness * 0.62f;

    g.setColour(arcColour.withAlpha(0.30f));
    g.fillEllipse(headX - headRadius * 2.2f, headY - headRadius * 2.2f, headRadius * 4.4f,
                   headRadius * 4.4f);
    g.setColour(juce::Colours::white.interpolatedWith(arcColour, 0.55f));
    g.fillEllipse(headX - headRadius, headY - headRadius, headRadius * 2.0f, headRadius * 2.0f);
}

// Visor com os quatro VUs, no lugar onde o pedal real tem o display. Cada
// medidor fica na COLUNA do switch da sua track, entao nao ha o que legendar:
// o medidor esta em cima do botao a que pertence.
void PedalMap::paintMeters(juce::Graphics& g) const {
    const auto visor = art(kVisorX, kVisorY, kVisorW, kVisorH);
    const float radius = 8.0f * scale_;

    g.setColour(kRingGroove);
    g.fillRoundedRectangle(visor, radius);
    g.setColour(kOutline);
    g.drawRoundedRectangle(visor, radius, juce::jmax(1.0f, 2.0f * scale_));

    for (int t = 0; t < config::kNumTracks; ++t) {
        const auto sw = switchArt(protocol::kButtonTrack1 + t);
        const float meterW = sw.getWidth() * 0.5f;
        const auto meter =
            art(sw.getCentreX() - meterW * 0.5f, kVisorY + 20.0f, meterW, kVisorH - 40.0f);

        theme::paintGroove(g, meter, 4.0f * scale_);

        const float level = juce::jlimit(0.0f, 1.0f, state_.level[t]);
        if (level > 0.001f) {
            auto filled = meter.reduced(2.0f * scale_);
            filled = filled.withTop(filled.getBottom() - filled.getHeight() * level);
            // Verde ate perto do topo, ambar na zona de atencao - o mesmo
            // vocabulario dos medidores das janelas.
            g.setGradientFill({theme::amber, filled.getX(), meter.getY(), theme::ledGreen, filled.getX(),
                                meter.getBottom(), false});
            g.fillRect(filled);
        }
    }
}

// LIMPAR TUDO: apaga as quatro tracks e devolve o pedal ao estado de fabrica
// da musica - o mesmo que o hold do CLEAR no equipamento. Enche da esquerda
// para a direita enquanto e segurado, e so dispara quando a barra completa.
void PedalMap::paintClearAll(juce::Graphics& g) const {
    const auto box = art(kClearX, kClearY, kClearW, kClearH);
    const float radius = 14.0f * scale_;
    const float stroke = juce::jmax(1.5f, 3.0f * scale_);
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    const bool flashing = (clearedAtMs_ != 0) &&
                          (now - clearedAtMs_ <= static_cast<juce::uint32>(kClearFlashMs));

    g.setColour(kSwitchBottomFace);
    g.fillRoundedRectangle(box, radius);

    if (clearHolding_) {
        // A barra que enche E a contagem: mostra quanto falta, e desistir e
        // so soltar.
        const float progress = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(now - clearHoldStartMs_) / static_cast<float>(kClearAllHoldMs));
        auto filled = box.withWidth(box.getWidth() * progress);
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(filled.toNearestInt());
        g.setColour(theme::ledRed.withAlpha(0.85f));
        g.fillRoundedRectangle(box, radius);
    } else if (flashing) {
        g.setColour(theme::ledRed);
        g.fillRoundedRectangle(box, radius);
    }

    g.setColour(theme::ledRed);
    g.drawRoundedRectangle(box.reduced(stroke * 0.5f), radius, stroke);

    auto text = box.toNearestInt().reduced(static_cast<int>(14.0f * scale_),
                                            static_cast<int>(10.0f * scale_));
    auto caption = text.removeFromBottom(static_cast<int>(26.0f * scale_));

    g.setColour(flashing ? kBodyBottom : theme::ink);
    g.setFont(theme::legend(34.0f * scale_, true));
    g.drawFittedText(flashing ? ui::utf8("LIMPO") : ui::utf8("LIMPAR TUDO"), text,
                      juce::Justification::centred, 1, 0.4f);

    g.setColour(flashing ? kBodyBottom : theme::inkDim);
    g.setFont(theme::legend(22.0f * scale_));
    g.drawFittedText(flashing ? ui::utf8("as 4 tracks foram apagadas")
                              : ui::utf8("segure para apagar as 4 tracks"),
                      caption, juce::Justification::centred, 1, 0.4f);
}

void PedalMap::paint(juce::Graphics& g) {
    const float thinStroke = juce::jmax(1.0f, 3.0f * scale_);

    // --- Caixa ------------------------------------------------------------
    const auto body = art(10.0f, 10.0f, kArtWidth - 20.0f, kArtHeight - 20.0f);
    const float bodyRadius = 18.0f * scale_;
    g.setGradientFill({kBodyTop, body.getX(), body.getY(), kBodyBottom, body.getX(), body.getBottom(),
                        false});
    g.fillRoundedRectangle(body, bodyRadius);
    g.setColour(kOutline);
    g.drawRoundedRectangle(body, bodyRadius, thinStroke);

    // --- Anel do loop e visor dos VUs -------------------------------------
    paintLoopRing(g);
    paintMeters(g);
    paintClearAll(g);

    // A legenda vai DENTRO do anel: fora dela roubaria altura dos switches, que
    // agora tem prioridade.
    g.setColour(theme::inkFaint);
    g.setFont(theme::legend(24.0f * scale_, true));
    g.drawText("LOOP", art(kRingCentreX - 100.0f, kRingCentreY - 16.0f, 200.0f, 32.0f),
                juce::Justification::centred, false);

    // --- LEDs bicolores das tracks ---------------------------------------
    for (int t = 0; t < config::kNumTracks; ++t) {
        const auto sw = switchArt(protocol::kButtonTrack1 + t);
        const float barW = sw.getWidth() * 0.46f;
        const auto bar = art(sw.getCentreX() - barW * 0.5f, kLedBarY, barW, kLedBarH);
        const bool on = (state_.led[t] != protocol::kLedOff) && (!state_.ledBlink[t] || blinkPhase_);
        const float barRadius = bar.getHeight() * 0.5f;

        if (on) {
            g.setColour(ledColourFor(state_.led[t]).withAlpha(0.28f));
            g.fillRoundedRectangle(bar.expanded(bar.getHeight() * 0.9f), barRadius * 2.0f);
        }
        g.setColour(on ? ledColourFor(state_.led[t]) : kLedOffFace);
        g.fillRoundedRectangle(bar, barRadius);
    }

    // --- Footswitches -----------------------------------------------------
    for (int i = 0; i < protocol::kButtonCount; ++i) {
        const auto sw = switchArt(i);
        const auto r = art(sw.getX(), sw.getY(), sw.getWidth(), sw.getHeight());
        const float radius = 18.0f * scale_;
        const bool pressed = state_.pressed[i] || (mouseDownSwitch_ == i);

        if (pressed) {
            // Flash amarelo: e o retorno de que o toque chegou, visivel de
            // relance e sem se confundir com o vermelho de gravacao nem com o
            // verde de reproducao, que ja tem significado nos LEDs.
            g.setColour(theme::amber.withAlpha(0.28f));
            g.fillRoundedRectangle(r.expanded(9.0f * scale_), radius * 1.4f);
            g.setColour(theme::amber);
            g.fillRoundedRectangle(r, radius);
        } else {
            g.setGradientFill({kSwitchTopFace, r.getX(), r.getY(), kSwitchBottomFace, r.getX(),
                                r.getBottom(), false});
            g.fillRoundedRectangle(r, radius);
            g.setColour(kSwitchEdge);
            g.drawRoundedRectangle(r.reduced(thinStroke * 0.5f), radius, thinStroke);
        }

        // Numeros das tracks em corpo bem maior que as palavras, como na
        // serigrafia. drawFittedText encolhe o que nao couber - "PLAY+REC" e
        // bem mais largo que os outros rotulos e ficava cortado.
        const bool isTrack = (i >= protocol::kButtonTrack1);
        g.setFont(isTrack ? theme::display(126.0f * scale_) : theme::legend(40.0f * scale_, true));
        g.setColour(pressed ? kBodyBottom : theme::ink);
        g.drawFittedText(switchLabel(i), r.toNearestInt().reduced(static_cast<int>(8.0f * scale_), 0),
                          juce::Justification::centred, 1, 0.40f);
    }
}
