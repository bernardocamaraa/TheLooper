#include "LayerStore.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

LayerStore::LayerStore() = default;

LayerStore::~LayerStore() {
    stop();

    // Com as duas threads paradas, esvaziar as filas daqui e seguro. Toda
    // requisicao que carrega buffer (Release/Archive) e dona dele.
    LayerBuffer* buffer = nullptr;
    while (fullPool_.pop(buffer)) {
        delete buffer;
    }
    while (loopPool_.pop(buffer)) {
        delete buffer;
    }
    LayerRestore restore;
    while (restores_.pop(restore)) {
        delete restore.buffer;
    }
    Request request;
    while (requests_.pop(request)) {
        delete request.buffer;
    }
    for (auto* b : freeFull_) {
        delete b;
    }
    for (auto* b : freeLoop_) {
        delete b;
    }
    for (auto& list : deep_) {
        for (auto& layer : list) {
            dropDeep(layer);
        }
    }
    if (!spillDir_.empty()) {
        std::error_code ec;
        fs::remove_all(spillDir_, ec);
    }
}

void LayerStore::start(int64_t fullFrames) {
    if (running()) {
        return;
    }
    fullFrames_ = fullFrames;

    // Uma pasta por instancia: o FL pode abrir o plugin mais de uma vez, e uma
    // instancia nao pode apagar as camadas da outra.
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / "TheLooper";
    if (!ec) {
        cleanStaleSpillDirs(base);
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        spillDir_ = base / ("camadas-" + std::to_string(stamp) + "-" +
                            std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(spillDir_, ec);
    }
    if (ec) {
        spillDir_.clear(); // sem pasta temporaria: tudo fica na RAM
    }

    // Enche as reservas antes de subir a thread, para a primeira gravacao ja
    // encontrar buffer pronto.
    topUpPools();

    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void LayerStore::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

// Pastas de despejo de execucoes que terminaram sem limpar (queda, taskkill).
// So as com mais de um dia: uma instancia viva do plugin pode estar usando a
// dela agora.
void LayerStore::cleanStaleSpillDirs(const fs::path& base) {
    std::error_code ec;
    if (!fs::exists(base, ec)) {
        return;
    }
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24);
    for (const auto& entry : fs::directory_iterator(base, ec)) {
        std::error_code entryEc;
        if (entry.is_directory(entryEc) && fs::last_write_time(entry.path(), entryEc) < cutoff) {
            fs::remove_all(entry.path(), entryEc);
        }
    }
}

// ---------------------------------------------------------------------------
// Lado da thread de audio
// ---------------------------------------------------------------------------

LayerBuffer* LayerStore::takeFull() {
    LayerBuffer* buffer = nullptr;
    if (fullPool_.pop(buffer)) {
        fullInQueue_.fetch_sub(1, std::memory_order_relaxed);
        return buffer;
    }
    return nullptr;
}

LayerBuffer* LayerStore::takeLoop(int64_t minFrames) {
    LayerBuffer* buffer = nullptr;
    while (loopPool_.pop(buffer)) {
        loopInQueue_.fetch_sub(1, std::memory_order_relaxed);
        if (buffer->frames >= minFrames) {
            return buffer;
        }
        release(buffer); // de um loop anterior, mais curto
    }
    // Sem buffer do tamanho do loop (acabou de ser definido, por exemplo): um
    // cheio serve, so ocupa mais memoria.
    return takeFull();
}

void LayerStore::release(LayerBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    Request request;
    request.kind = Request::Kind::Release;
    request.buffer = buffer;
    // Se a fila (8192 pedidos) estiver cheia, o buffer vaza: travar a thread
    // de audio esperando vaga seria pior. Na pratica ela nunca enche.
    requests_.push(request);
}

bool LayerStore::archive(int track, uint32_t generation, LayerBuffer* buffer) {
    Request request;
    request.kind = Request::Kind::Archive;
    request.track = track;
    request.generation = generation;
    request.buffer = buffer;
    return requests_.push(request);
}

bool LayerStore::requestRefill(int track, uint32_t generation, int count) {
    Request request;
    request.kind = Request::Kind::Refill;
    request.track = track;
    request.generation = generation;
    request.value = count;
    return requests_.push(request);
}

void LayerStore::clearDeep(int track) {
    Request request;
    request.kind = Request::Kind::ClearDeep;
    request.track = track;
    requests_.push(request);
}

void LayerStore::setLoopFrames(int64_t frames) {
    // Os prontos do tamanho antigo voltam ja: quem consome esta fila e esta
    // thread, entao e ela que tem de esvazia-la.
    LayerBuffer* buffer = nullptr;
    while (loopPool_.pop(buffer)) {
        loopInQueue_.fetch_sub(1, std::memory_order_relaxed);
        release(buffer);
    }
    Request request;
    request.kind = Request::Kind::SetLoopFrames;
    request.value = frames;
    requests_.push(request);
}

bool LayerStore::popRestore(LayerRestore& out) {
    return restores_.pop(out);
}

// ---------------------------------------------------------------------------
// Thread do LayerStore
// ---------------------------------------------------------------------------

void LayerStore::run() {
    // Sem condition variable de proposito: a thread de audio nao pode
    // sinalizar nada (seria uma chamada de sistema no callback), entao esta
    // thread olha as filas a cada 2 ms.
    while (running_.load(std::memory_order_acquire)) {
        bool worked = false;
        Request request;
        while (requests_.pop(request)) {
            handle(request);
            worked = true;
        }
        topUpPools();
        spillIfOverBudget();
        if (!worked) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

void LayerStore::handle(const Request& request) {
    switch (request.kind) {
        case Request::Kind::Release:
            recycle(request.buffer);
            break;

        case Request::Kind::Archive: {
            if (request.buffer == nullptr) {
                break;
            }
            DeepLayer layer;
            layer.buffer = request.buffer;
            layer.frames = request.buffer->frames;
            ramBytes_.fetch_add(static_cast<int64_t>(layer.buffer->bytes()), std::memory_order_relaxed);
            deep_[request.track].push_back(std::move(layer));
            break;
        }

        case Request::Kind::Refill: {
            auto& list = deep_[request.track];
            for (int64_t i = 0; i < request.value && !list.empty(); ++i) {
                DeepLayer& layer = list.back();
                ensureInRam(layer);
                LayerRestore restore;
                restore.track = request.track;
                restore.generation = request.generation;
                restore.buffer = layer.buffer;
                if (!restores_.push(restore)) {
                    break; // fila cheia: o resto fica guardado aqui
                }
                ramBytes_.fetch_sub(static_cast<int64_t>(layer.buffer->bytes()), std::memory_order_relaxed);
                list.pop_back();
            }
            break;
        }

        case Request::Kind::ClearDeep:
            for (auto& layer : deep_[request.track]) {
                dropDeep(layer);
            }
            deep_[request.track].clear();
            break;

        case Request::Kind::SetLoopFrames:
            loopFrames_ = request.value;
            for (auto* b : freeLoop_) {
                delete b;
            }
            freeLoop_.clear();
            break;
    }
}

void LayerStore::recycle(LayerBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    // Reaproveita (zerado) o que cabe numa das reservas; o resto e destruido.
    if (buffer->frames == fullFrames_ && static_cast<int>(freeFull_.size()) < config::kSpareFullBuffers) {
        std::fill(buffer->samples.begin(), buffer->samples.end(), 0.0f);
        freeFull_.push_back(buffer);
    } else if (loopFrames_ > 0 && buffer->frames == loopFrames_ &&
               static_cast<int>(freeLoop_.size()) < config::kSpareLoopBuffers) {
        std::fill(buffer->samples.begin(), buffer->samples.end(), 0.0f);
        freeLoop_.push_back(buffer);
    } else {
        delete buffer;
    }
}

void LayerStore::topUpPools() {
    // O contador sobe ANTES do push: a thread de audio pode pegar o buffer e
    // descontar logo depois, e o contador nunca pode ficar abaixo do real.
    while (fullFrames_ > 0 && fullInQueue_.load(std::memory_order_relaxed) < config::kSpareFullBuffers) {
        LayerBuffer* buffer = nullptr;
        if (!freeFull_.empty()) {
            buffer = freeFull_.back();
            freeFull_.pop_back();
        } else {
            buffer = allocate(fullFrames_);
        }
        fullInQueue_.fetch_add(1, std::memory_order_relaxed);
        if (!fullPool_.push(buffer)) {
            fullInQueue_.fetch_sub(1, std::memory_order_relaxed);
            freeFull_.push_back(buffer);
            break;
        }
    }
    while (loopFrames_ > 0 && loopInQueue_.load(std::memory_order_relaxed) < config::kSpareLoopBuffers) {
        LayerBuffer* buffer = nullptr;
        if (!freeLoop_.empty()) {
            buffer = freeLoop_.back();
            freeLoop_.pop_back();
        } else {
            buffer = allocate(loopFrames_);
        }
        loopInQueue_.fetch_add(1, std::memory_order_relaxed);
        if (!loopPool_.push(buffer)) {
            loopInQueue_.fetch_sub(1, std::memory_order_relaxed);
            freeLoop_.push_back(buffer);
            break;
        }
    }
}

void LayerStore::spillIfOverBudget() {
    if (spillDir_.empty()) {
        return;
    }
    // As mais antigas de cada track vao primeiro: sao as ultimas que o
    // desfazer vai precisar.
    while (ramBytes_.load(std::memory_order_relaxed) > ramBudgetBytes_.load(std::memory_order_relaxed)) {
        bool spilled = false;
        for (int t = 0; t < config::kNumTracks && !spilled; ++t) {
            for (auto& layer : deep_[t]) {
                if (layer.buffer != nullptr) {
                    if (!spill(t, layer)) {
                        return; // disco cheio ou sem permissao: fica na RAM
                    }
                    spilled = true;
                    break;
                }
            }
        }
        if (!spilled) {
            return; // nada mais na RAM para despejar
        }
    }
}

bool LayerStore::spill(int track, DeepLayer& layer) {
    const fs::path path =
        spillDir_ / ("t" + std::to_string(track) + "_" + std::to_string(spillCounter_++) + ".f32");
    const auto bytes = static_cast<std::streamsize>(layer.buffer->bytes());
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(layer.buffer->data()), bytes);
        if (!out) {
            out.close();
            std::error_code ec;
            fs::remove(path, ec);
            return false;
        }
    }
    ramBytes_.fetch_sub(bytes, std::memory_order_relaxed);
    diskBytes_.fetch_add(bytes, std::memory_order_relaxed);
    delete layer.buffer;
    layer.buffer = nullptr;
    layer.path = path;
    return true;
}

void LayerStore::ensureInRam(DeepLayer& layer) {
    if (layer.buffer != nullptr) {
        return;
    }
    layer.buffer = allocate(layer.frames);
    const auto bytes = static_cast<std::streamsize>(layer.buffer->bytes());
    {
        std::ifstream in(layer.path, std::ios::binary);
        in.read(reinterpret_cast<char*>(layer.buffer->data()), bytes);
        // Se a leitura falhar a camada volta em silencio: o desfazer tira
        // "nada" da soma e a camada continua soando - ruim, mas nao trava o
        // pedal no meio do show. So acontece se alguem apagar a pasta
        // temporaria com o app aberto.
    }
    std::error_code ec;
    fs::remove(layer.path, ec);
    layer.path.clear();
    diskBytes_.fetch_sub(bytes, std::memory_order_relaxed);
    ramBytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void LayerStore::dropDeep(DeepLayer& layer) {
    if (layer.buffer != nullptr) {
        ramBytes_.fetch_sub(static_cast<int64_t>(layer.buffer->bytes()), std::memory_order_relaxed);
        delete layer.buffer;
        layer.buffer = nullptr;
    } else if (!layer.path.empty()) {
        std::error_code ec;
        fs::remove(layer.path, ec);
        diskBytes_.fetch_sub(layer.frames * config::kNumChannels * static_cast<int64_t>(sizeof(float)),
                             std::memory_order_relaxed);
        layer.path.clear();
    }
}
