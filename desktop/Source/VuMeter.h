// Medidor de nivel de uma track, em dois estilos.
//
// - Bar (janela de VUs): barra continua. E o medidor grande, lido a
//   distancia e no meio de uma musica, onde a altura de uma coluna cheia se
//   le mais rapido do que qualquer coisa.
// - Segments (canal do mixer): regua de LEDs segmentada. Ali o medidor e
//   estreito e serve para dosar o fader de perto, e o segmento ajuda a ler
//   valor ("tres abaixo do topo") em vez de so tendencia.
//
// O estilo importa porque depende da LARGURA disponivel: com segmentos numa
// coluna larga, cada segmento fica mais largo que alto e a regua le como um
// tecido listrado em vez de um medidor.
//
// Nos dois estilos os pixels apagados continuam visiveis de leve. Um medidor
// real se ve mesmo desligado, e isso e o que impede a track sem sinal de
// virar um retangulo preto - o pior defeito da primeira versao.
//
// O nivel vem de um callback (lido da LooperEngine/AudioTrack, que atualiza
// um atomic a cada bloco) e e repintado por um Timer da GUI - a thread de
// audio nunca e bloqueada.
#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

class VuMeter : public juce::Component, private juce::Timer {
public:
    enum class Style { Bar, Segments };

    VuMeter();
    ~VuMeter() override;

    void setLevelSource(std::function<float()> levelProvider) { levelProvider_ = std::move(levelProvider); }
    void setMuted(bool muted);
    void setStyle(Style style);

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    void paintBar(juce::Graphics& g, juce::Rectangle<float> area);
    void paintSegments(juce::Graphics& g, juce::Rectangle<float> area);

    std::function<float()> levelProvider_;
    bool muted_ = false;
    Style style_ = Style::Bar;

    float level_ = 0.0f;     // 0..1 ja em escala de dB
    float peak_ = 0.0f;      // retencao de pico, cai devagar
    float peakHoldMs_ = 0.0f;
};
