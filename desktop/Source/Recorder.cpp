#include "Recorder.h"

namespace {
constexpr int kBitsPerSample = 24; // compativel com qualquer DAW, sem o peso do float
constexpr int kFifoBlocks = 32768; // folga generosa entre o callback e o disco
} // namespace

Recorder::Recorder() {
    backgroundThread_.startThread();
}

Recorder::~Recorder() {
    stop();
    backgroundThread_.stopThread(2000);
}

void Recorder::prepare(double sampleRate, int numChannels) {
    if (sampleRate > 0.0) {
        sampleRate_ = sampleRate;
    }
    if (numChannels > 0) {
        numChannels_ = numChannels;
    }
}

bool Recorder::start(const juce::File& folder) {
    stop();

    if (!folder.exists() && !folder.createDirectory()) {
        return false;
    }

    const juce::String stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S");
    juce::File file = folder.getChildFile("Pedal Looper " + stamp + ".wav");

    // Nao sobrescreve nada: se ja existir (duas gravacoes no mesmo segundo),
    // o JUCE acrescenta um sufixo.
    file = file.getNonexistentSibling();

    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    if (stream == nullptr || !stream->openedOk()) {
        return false;
    }

    juce::WavAudioFormat wav;
    auto streamAsBase = std::unique_ptr<juce::OutputStream>(std::move(stream));
    auto writer = wav.createWriterFor(streamAsBase, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate(sampleRate_)
                                                        .withNumChannels(numChannels_)
                                                        .withBitsPerSample(kBitsPerSample));
    if (writer == nullptr) {
        return false;
    }

    samplesWritten_.store(0, std::memory_order_relaxed);
    ownedWriter_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(),
                                                                              backgroundThread_,
                                                                              kFifoBlocks);
    lastFile_ = file;
    writer_.store(ownedWriter_.get(), std::memory_order_release);
    return true;
}

void Recorder::stop() {
    if (writer_.load(std::memory_order_acquire) == nullptr) {
        ownedWriter_.reset();
        return;
    }

    // 1) O callback para de usar o writer a partir do proximo bloco.
    writer_.store(nullptr, std::memory_order_release);

    // 2) Espera o callback confirmar que ja passou depois do passo 1. Sem
    //    isso o writer poderia ser destruido no meio de um write(). Dois
    //    blocos garantem que qualquer callback que ainda tivesse o ponteiro
    //    antigo em maos terminou.
    //
    //    Se o device de audio estiver parado o contador nunca avanca - dai o
    //    timeout, que e seguro justamente porque nao ha callback rodando.
    const uint32_t start = blockCounter_.load(std::memory_order_acquire);
    const juce::uint32 deadline = juce::Time::getMillisecondCounter() + 300;
    while (blockCounter_.load(std::memory_order_acquire) - start < 2) {
        if (juce::Time::getMillisecondCounter() > deadline) {
            break;
        }
        juce::Thread::sleep(1);
    }

    // 3) O destrutor do ThreadedWriter esvazia o FIFO e fecha o cabecalho WAV.
    ownedWriter_.reset();
}

double Recorder::recordedSeconds() const {
    return sampleRate_ > 0.0
               ? static_cast<double>(samplesWritten_.load(std::memory_order_relaxed)) / sampleRate_
               : 0.0;
}

void Recorder::writeBlock(const float* const* channels, int numSamples) {
    // channels == nullptr acontece quando o device nao expoe todos os canais
    // esperados: nesse caso nao ha o que gravar, mas o contador abaixo ainda
    // precisa avancar.
    if (channels != nullptr && numSamples > 0) {
        if (auto* w = writer_.load(std::memory_order_acquire)) {
            w->write(channels, numSamples);
            samplesWritten_.fetch_add(numSamples, std::memory_order_relaxed);
        }
    }
    // Incrementado SEMPRE, inclusive quando nao ha gravacao: e o sinal que
    // stop() espera para saber que o callback ja passou por aqui.
    blockCounter_.fetch_add(1, std::memory_order_release);
}
