// Guarda-volumes das camadas: a peca que permite camadas INFINITAS sem a
// thread de audio alocar, zerar ou esperar por nada.
//
// Divisao de trabalho:
//
// - A thread de AUDIO (AudioTrack/LooperEngine) so troca ponteiros: pega
//   buffers prontos (takeFull/takeLoop), devolve os que nao usa mais
//   (release), manda as camadas antigas para ca (archive) e pede de volta as
//   que o desfazer vai precisar (requestRefill). Tudo por filas SPSC.
// - A thread do LAYERSTORE (propria, criada em start()) aloca e zera buffers,
//   guarda a pilha funda de camadas de cada track e, quando ela passa do
//   orcamento de RAM, despeja as mais antigas em arquivos temporarios. Quando
//   o desfazer chega nelas, le do disco de volta.
//
// A thread de audio e a UNICA produtora das requisicoes e a UNICA consumidora
// dos buffers prontos e das camadas devolvidas. A excecao sao as operacoes
// nao-RT da GUI (abrir/salvar sessao), que so rodam com o callback de audio
// desligado - nunca ao mesmo tempo que ele.
//
// Sem JUCE de proposito: o motor tem de compilar tambem no teste de linha de
// comando (Tests/LooperEngineTests.cpp).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <vector>

#include "Config.h"
#include "SpscQueue.h"

// Um buffer de audio intercalado (kNumChannels amostras por frame). Criado e
// destruido fora da thread de audio; ela so passa o ponteiro adiante.
struct LayerBuffer {
    explicit LayerBuffer(int64_t frameCount)
        : frames(frameCount),
          samples(static_cast<size_t>(frameCount) * config::kNumChannels, 0.0f) {}

    float* data() { return samples.data(); }
    const float* data() const { return samples.data(); }
    size_t bytes() const { return samples.size() * sizeof(float); }

    int64_t frames = 0;
    std::vector<float> samples;
};

// Camada devolvida pelo LayerStore (requestRefill). generation e a mesma da
// requisicao: se a track foi limpa nesse meio tempo, a camada e descartada.
struct LayerRestore {
    int track = 0;
    uint32_t generation = 0;
    LayerBuffer* buffer = nullptr;
};

class LayerStore {
public:
    LayerStore();
    ~LayerStore();

    LayerStore(const LayerStore&) = delete;
    LayerStore& operator=(const LayerStore&) = delete;

    // Nao-RT. fullFrames = tamanho dos buffers cheios (a soma de cada track).
    // Limpa sobras de despejo de uma execucao anterior e sobe a thread.
    void start(int64_t fullFrames);
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // Nao-RT: aloca um buffer ja zerado direto, sem passar pela thread.
    static LayerBuffer* allocate(int64_t frames) { return new LayerBuffer(frames); }

    int64_t fullFrames() const { return fullFrames_; }

    // --- Lado da thread de audio (RT-safe: nenhuma chamada aloca ou bloqueia) ---

    // Buffer cheio zerado, ou nullptr se nao ha nenhum pronto.
    LayerBuffer* takeFull();
    // Buffer zerado com pelo menos minFrames: tenta os do tamanho do loop e,
    // se nao houver, usa um cheio. nullptr se nao ha nenhum pronto.
    LayerBuffer* takeLoop(int64_t minFrames);

    // Devolve um buffer que nao e mais usado (ele e zerado e reaproveitado,
    // ou destruido). Aceita nullptr.
    void release(LayerBuffer* buffer);

    // Entrega a camada mais antiga das recentes de uma track para guardar.
    // Retorna false se a fila estiver cheia (a track fica com ela por
    // enquanto e tenta de novo no proximo bloco).
    bool archive(int track, uint32_t generation, LayerBuffer* buffer);

    // Pede de volta as `count` camadas guardadas mais novas da track, que
    // chegam por popRestore() na ordem da mais nova para a mais antiga.
    bool requestRefill(int track, uint32_t generation, int count);

    // A track foi limpa: descarta tudo o que esta guardado dela.
    void clearDeep(int track);

    // O comprimento do loop mestre mudou (0 = nao ha loop). Descarta os buffers
    // prontos do tamanho antigo e passa a preparar do tamanho novo.
    void setLoopFrames(int64_t frames);

    bool popRestore(LayerRestore& out);

    // --- Qualquer thread ---

    // Orcamento de RAM das camadas guardadas aqui (acima dele, as mais
    // antigas vao para o disco). Pode mudar com o app rodando.
    void setRamBudgetBytes(int64_t bytes) { ramBudgetBytes_.store(bytes, std::memory_order_relaxed); }
    int64_t ramBytes() const { return ramBytes_.load(std::memory_order_relaxed); }
    int64_t diskBytes() const { return diskBytes_.load(std::memory_order_relaxed); }

    // Para o teste: quantos buffers cheios estao prontos agora.
    int readyFullBuffers() const { return fullInQueue_.load(std::memory_order_relaxed); }

private:
    struct Request {
        enum class Kind : uint8_t { Release, Archive, Refill, ClearDeep, SetLoopFrames };
        Kind kind = Kind::Release;
        int track = 0;
        uint32_t generation = 0;
        LayerBuffer* buffer = nullptr;
        int64_t value = 0;
    };

    // Uma camada guardada: na RAM (buffer != nullptr) ou em disco (path).
    struct DeepLayer {
        LayerBuffer* buffer = nullptr;
        std::filesystem::path path;
        int64_t frames = 0;
    };

    void run();
    void handle(const Request& request);
    void recycle(LayerBuffer* buffer);
    void topUpPools();
    void spillIfOverBudget();
    bool spill(int track, DeepLayer& layer);
    void ensureInRam(DeepLayer& layer);
    void dropDeep(DeepLayer& layer);
    void cleanStaleSpillDirs(const std::filesystem::path& base);

    std::thread thread_;
    std::atomic<bool> running_{false};
    int64_t fullFrames_ = 0;

    // audio -> LayerStore
    SpscQueue<Request, 8192> requests_;
    // LayerStore -> audio
    SpscQueue<LayerBuffer*, 16> fullPool_;
    SpscQueue<LayerBuffer*, 16> loopPool_;
    SpscQueue<LayerRestore, 512> restores_;
    std::atomic<int> fullInQueue_{0};
    std::atomic<int> loopInQueue_{0};

    // So a thread do LayerStore toca nestes.
    int64_t loopFrames_ = 0;
    std::vector<LayerBuffer*> freeFull_;
    std::vector<LayerBuffer*> freeLoop_;
    std::vector<DeepLayer> deep_[config::kNumTracks]; // frente = mais antiga
    std::filesystem::path spillDir_;                  // vazio = sem disco, tudo na RAM
    uint64_t spillCounter_ = 0;

    std::atomic<int64_t> ramBudgetBytes_{config::kDefaultLayerRamBudgetBytes};
    std::atomic<int64_t> ramBytes_{0};
    std::atomic<int64_t> diskBytes_{0};
};
