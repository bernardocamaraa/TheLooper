#include "LoopFile.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "UiText.h"

namespace {
constexpr const char* kMagic = "PEDALLP1";
constexpr int kMagicLength = 8;

// Teto de sanidade para o cabecalho: um XML de meia duzia de linhas nunca
// chega perto disso, e o limite impede que um arquivo corrompido peca uma
// alocacao absurda antes de falhar.
constexpr uint32_t kMaxMetaLength = 64 * 1024;

// Idem para o audio: kMaxLoopSeconds ja e o teto do proprio motor.
int64_t maxLengthSamples() {
    return static_cast<int64_t>(config::kMaxLoopSeconds * config::kMaxSupportedSampleRate);
}
} // namespace

bool LoopSession::hasAudio() const {
    for (const auto& track : tracks) {
        if (!track.audio.empty()) {
            return true;
        }
    }
    return false;
}

namespace loopfile {

juce::String save(const juce::File& file, const LoopSession& session) {
    if (!session.hasAudio() || session.lengthSamples <= 0) {
        return ui::utf8("Não há nada gravado para salvar.");
    }

    juce::XmlElement meta("PedalLoop");
    meta.setAttribute("sampleRate", session.sampleRate);
    meta.setAttribute("lengthSamples", static_cast<int>(session.lengthSamples));
    meta.setAttribute("channels", config::kNumChannels);
    meta.setAttribute("tracks", config::kNumTracks);

    for (int i = 0; i < config::kNumTracks; ++i) {
        const auto& track = session.tracks[static_cast<size_t>(i)];
        auto* node = meta.createNewChildElement("Track");
        node->setAttribute("index", i);
        node->setAttribute("name", track.name);
        node->setAttribute("inputMask", static_cast<int>(track.inputMask));
        node->setAttribute("gain", track.gain);
        node->setAttribute("muted", track.muted);
        node->setAttribute("hasAudio", !track.audio.empty());
    }

    const juce::String metaText = meta.toString();
    const auto metaBytes = metaText.toRawUTF8();
    const uint32_t metaLength = static_cast<uint32_t>(std::strlen(metaBytes));

    // Escreve num temporario e so depois substitui o arquivo final: se faltar
    // energia ou disco no meio, o .loop anterior continua inteiro em vez de
    // virar um arquivo pela metade.
    juce::TemporaryFile temp(file);
    {
        juce::FileOutputStream out(temp.getFile());
        if (!out.openedOk()) {
            return ui::utf8("Não consegui escrever em ") + temp.getFile().getFullPathName();
        }
        out.write(kMagic, kMagicLength);
        out.writeInt(static_cast<int>(metaLength));
        out.write(metaBytes, metaLength);

        for (const auto& track : session.tracks) {
            if (!track.audio.empty()) {
                out.write(track.audio.data(), track.audio.size() * sizeof(float));
            }
        }

        if (out.getStatus().failed()) {
            return ui::utf8("Erro ao gravar o arquivo: ") + out.getStatus().getErrorMessage();
        }
    }

    if (!temp.overwriteTargetFileWithTemporary()) {
        return ui::utf8("Não consegui substituir ") + file.getFullPathName();
    }
    return {};
}

juce::String load(const juce::File& file, LoopSession& session) {
    juce::FileInputStream in(file);
    if (!in.openedOk()) {
        return ui::utf8("Não consegui abrir ") + file.getFullPathName();
    }

    char magic[kMagicLength + 1] = {};
    if (in.read(magic, kMagicLength) != kMagicLength || juce::String(magic) != kMagic) {
        return ui::utf8("Este arquivo não é um .loop do Pedal Looper.");
    }

    const int metaLength = in.readInt();
    if (metaLength <= 0 || static_cast<uint32_t>(metaLength) > kMaxMetaLength) {
        return ui::utf8("Arquivo .loop corrompido (cabeçalho inválido).");
    }

    juce::MemoryBlock metaBlock;
    if (in.readIntoMemoryBlock(metaBlock, metaLength) != metaLength) {
        return ui::utf8("Arquivo .loop incompleto (cabeçalho).");
    }

    auto meta = juce::parseXML(metaBlock.toString());
    if (meta == nullptr || !meta->hasTagName("PedalLoop")) {
        return ui::utf8("Arquivo .loop corrompido (cabeçalho ilegível).");
    }

    const int channels = meta->getIntAttribute("channels", config::kNumChannels);
    if (channels != config::kNumChannels) {
        return ui::utf8("Este .loop tem ") + juce::String(channels) +
               ui::utf8(" canais e esta versão trabalha com ") +
               juce::String(config::kNumChannels) + ".";
    }

    session = LoopSession{};
    session.sampleRate = meta->getDoubleAttribute("sampleRate", config::kPreferredSampleRate);
    session.lengthSamples = static_cast<int64_t>(meta->getIntAttribute("lengthSamples", 0));

    if (session.lengthSamples <= 0 || session.lengthSamples > maxLengthSamples()) {
        return ui::utf8("Arquivo .loop corrompido (comprimento inválido).");
    }

    std::array<bool, config::kNumTracks> hasAudio{};
    for (auto* node : meta->getChildWithTagNameIterator("Track")) {
        const int index = node->getIntAttribute("index", -1);
        if (index < 0 || index >= config::kNumTracks) {
            continue;
        }
        auto& track = session.tracks[static_cast<size_t>(index)];
        track.name = node->getStringAttribute("name");
        track.inputMask = static_cast<uint32_t>(node->getIntAttribute(
            "inputMask", static_cast<int>(config::kDefaultInputMask)));
        track.gain = static_cast<float>(node->getDoubleAttribute("gain", config::kDefaultTrackGain));
        track.muted = node->getBoolAttribute("muted", false);
        hasAudio[static_cast<size_t>(index)] = node->getBoolAttribute("hasAudio", false);
    }

    const size_t samplesPerTrack =
        static_cast<size_t>(session.lengthSamples) * static_cast<size_t>(config::kNumChannels);
    for (int i = 0; i < config::kNumTracks; ++i) {
        if (!hasAudio[static_cast<size_t>(i)]) {
            continue;
        }
        auto& audio = session.tracks[static_cast<size_t>(i)].audio;
        audio.resize(samplesPerTrack);
        const int wanted = static_cast<int>(samplesPerTrack * sizeof(float));
        if (in.read(audio.data(), wanted) != wanted) {
            return ui::utf8("Arquivo .loop incompleto (áudio da track ") + juce::String(i + 1) + ").";
        }
    }

    return {};
}

void resampleTo(LoopSession& session, double targetSampleRate) {
    if (session.sampleRate <= 0.0 || targetSampleRate <= 0.0 ||
        juce::approximatelyEqual(session.sampleRate, targetSampleRate) || !session.hasAudio()) {
        return;
    }

    const double ratio = session.sampleRate / targetSampleRate;
    const int64_t outFrames =
        static_cast<int64_t>(std::llround(static_cast<double>(session.lengthSamples) / ratio));
    if (outFrames <= 0) {
        return;
    }

    // Interpolador de Lagrange, um por CANAL: o estado dele depende das
    // amostras anteriores daquele canal, entao um interpolador so, alimentado
    // com audio intercalado, misturaria os dois.
    for (auto& track : session.tracks) {
        if (track.audio.empty()) {
            continue;
        }

        const int64_t inFrames = session.lengthSamples;
        std::vector<float> resampled(static_cast<size_t>(outFrames) * config::kNumChannels, 0.0f);
        std::vector<float> channelIn(static_cast<size_t>(inFrames));
        std::vector<float> channelOut(static_cast<size_t>(outFrames));

        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            for (int64_t i = 0; i < inFrames; ++i) {
                channelIn[static_cast<size_t>(i)] =
                    track.audio[static_cast<size_t>(i) * config::kNumChannels + ch];
            }

            juce::LagrangeInterpolator interpolator;
            interpolator.process(ratio, channelIn.data(), channelOut.data(),
                                  static_cast<int>(outFrames));

            for (int64_t i = 0; i < outFrames; ++i) {
                resampled[static_cast<size_t>(i) * config::kNumChannels + ch] =
                    channelOut[static_cast<size_t>(i)];
            }
        }
        track.audio = std::move(resampled);
    }

    session.lengthSamples = outFrames;
    session.sampleRate = targetSampleRate;
}

juce::String readInfo(const juce::File& file, LoopInfo& info) {
    juce::FileInputStream in(file);
    if (!in.openedOk()) {
        return ui::utf8("Não consegui abrir ") + file.getFullPathName();
    }
    char magic[kMagicLength + 1] = {};
    if (in.read(magic, kMagicLength) != kMagicLength || juce::String(magic) != kMagic) {
        return ui::utf8("Não é um .loop do The Looper.");
    }
    const int metaLength = in.readInt();
    if (metaLength <= 0 || static_cast<uint32_t>(metaLength) > kMaxMetaLength) {
        return ui::utf8("Cabeçalho inválido.");
    }
    juce::MemoryBlock metaBlock;
    if (in.readIntoMemoryBlock(metaBlock, metaLength) != metaLength) {
        return ui::utf8("Cabeçalho incompleto.");
    }
    auto meta = juce::parseXML(metaBlock.toString());
    if (meta == nullptr || !meta->hasTagName("PedalLoop")) {
        return ui::utf8("Cabeçalho ilegível.");
    }

    info = LoopInfo{};
    info.sampleRate = meta->getDoubleAttribute("sampleRate", config::kPreferredSampleRate);
    info.lengthSamples = static_cast<int64_t>(meta->getIntAttribute("lengthSamples", 0));
    for (auto* node : meta->getChildWithTagNameIterator("Track")) {
        const int index = node->getIntAttribute("index", -1);
        if (index < 0 || index >= config::kNumTracks) {
            continue;
        }
        info.names[static_cast<size_t>(index)] = node->getStringAttribute("name");
        info.hasAudio[static_cast<size_t>(index)] = node->getBoolAttribute("hasAudio", false);
    }
    return {};
}

namespace {
juce::String writeWav(const juce::File& file, const std::vector<float>& interleaved, int64_t frames,
                      double sampleRate) {
    file.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream>(file);
    if (!fileStream->openedOk()) {
        return ui::utf8("Não consegui escrever ") + file.getFullPathName();
    }
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(sampleRate)
                                                  .withNumChannels(config::kNumChannels)
                                                  .withBitsPerSample(24));
    if (writer == nullptr) {
        return ui::utf8("Não consegui criar o WAV ") + file.getFileName();
    }
    const int count = static_cast<int>(frames);
    juce::AudioBuffer<float> buffer(config::kNumChannels, count);
    for (int ch = 0; ch < config::kNumChannels; ++ch) {
        float* dest = buffer.getWritePointer(ch);
        for (int i = 0; i < count; ++i) {
            dest[i] = juce::jlimit(-1.0f, 1.0f, interleaved[static_cast<size_t>(i) * config::kNumChannels + ch]);
        }
    }
    if (!writer->writeFromAudioSampleBuffer(buffer, 0, count)) {
        return ui::utf8("Erro ao gravar ") + file.getFileName();
    }
    return {};
}
} // namespace

juce::String exportStems(const LoopSession& session, const juce::File& folder, const juce::String& baseName,
                         juce::Array<juce::File>* written) {
    if (!session.hasAudio() || session.lengthSamples <= 0) {
        return ui::utf8("Esta sessão não tem áudio para exportar.");
    }
    if (!folder.createDirectory()) {
        return ui::utf8("Não consegui criar a pasta ") + folder.getFullPathName();
    }

    const size_t total = static_cast<size_t>(session.lengthSamples) * config::kNumChannels;
    std::vector<float> mix(total, 0.0f);

    for (int t = 0; t < config::kNumTracks; ++t) {
        const auto& track = session.tracks[static_cast<size_t>(t)];
        if (track.audio.empty()) {
            continue;
        }
        const juce::String name = track.name.isNotEmpty() ? track.name : "Track " + juce::String(t + 1);
        const juce::File file = folder.getChildFile(
            juce::File::createLegalFileName(baseName + " - " + juce::String(t + 1) + " " + name) + ".wav");
        const juce::String error = writeWav(file, track.audio, session.lengthSamples, session.sampleRate);
        if (error.isNotEmpty()) {
            return error;
        }
        if (written != nullptr) {
            written->add(file);
        }
        if (!track.muted) {
            const float gain = track.gain * config::kTrackGain;
            for (size_t i = 0; i < total && i < track.audio.size(); ++i) {
                mix[i] += track.audio[i] * gain;
            }
        }
    }

    // Mono, como a saida do app (ver config::kMonoOutput).
    for (size_t i = 0; i + 1 < total; i += config::kNumChannels) {
        float sum = 0.0f;
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            sum += mix[i + static_cast<size_t>(ch)];
        }
        for (int ch = 0; ch < config::kNumChannels; ++ch) {
            mix[i + static_cast<size_t>(ch)] = sum / static_cast<float>(config::kNumChannels);
        }
    }
    const juce::File mixFile = folder.getChildFile(juce::File::createLegalFileName(baseName + " - Mix") + ".wav");
    const juce::String error = writeWav(mixFile, mix, session.lengthSamples, session.sampleRate);
    if (error.isEmpty() && written != nullptr) {
        written->add(mixFile);
    }
    return error;
}

} // namespace loopfile

int LoopInfo::tracksWithAudio() const {
    int count = 0;
    for (bool has : hasAudio) {
        count += has ? 1 : 0;
    }
    return count;
}
