// Grava a saida do looper em WAV enquanto se toca.
//
// Ate aqui o projeto nao escrevia uma unica amostra em disco: fechou o app,
// perdeu tudo.
//
// A escrita em si acontece numa thread propria (juce::AudioFormatWriter::
// ThreadedWriter), que e o mecanismo do JUCE para gravar a partir de um
// callback de audio sem tocar no disco dentro dele. O callback so empurra o
// bloco num FIFO.
//
// Sobre o tempo de vida do writer: o callback de audio le um ponteiro
// atomico, sem lock nenhum (a regra deste projeto - ver AudioEngine.h). Para
// o writer nao ser destruido debaixo do callback, stop() zera o ponteiro e
// so entao ESPERA o callback confirmar que passou por ele, antes de destruir
// - ver o comentario em stop().
#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

class Recorder {
public:
    Recorder();
    ~Recorder();

    // Chamado quando o device de audio abre.
    void prepare(double sampleRate, int numChannels);

    // --- Thread da GUI ---
    // Cria um arquivo com data/hora dentro de folder. Devolve false se a
    // pasta nao puder ser criada ou o arquivo nao puder ser aberto.
    bool start(const juce::File& folder);
    void stop();

    bool isRecording() const { return writer_.load(std::memory_order_acquire) != nullptr; }
    juce::File lastFile() const { return lastFile_; }
    double recordedSeconds() const;

    // --- Thread de audio (RT-safe: sem lock, sem alocacao) ---
    void writeBlock(const float* const* channels, int numSamples);

private:
    juce::TimeSliceThread backgroundThread_{"Gravacao em disco"};
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> ownedWriter_;

    // Lido pelo callback de audio; escrito pela GUI.
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> writer_{nullptr};

    // Avanca a cada bloco processado. stop() usa para saber que o callback
    // ja enxergou writer_ == nullptr.
    std::atomic<uint32_t> blockCounter_{0};

    std::atomic<int64_t> samplesWritten_{0};

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    juce::File lastFile_;
};
