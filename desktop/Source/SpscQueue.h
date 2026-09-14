// Fila lock-free single-producer/single-consumer, capacidade fixa alocada na
// construcao (nunca realoca depois) - segura para uso a partir de um callback
// de audio realtime em qualquer uma das pontas (push OU pop), desde que haja
// exatamente um thread produtor e um consumidor por instancia.
//
// Capacidade util = Capacity - 1 (um slot fica sempre vazio para distinguir
// fila cheia de fila vazia sem precisar de um contador separado).
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity>
class SpscQueue {
public:
    static_assert(Capacity >= 2, "Capacity precisa ser >= 2");

    bool push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t nextHead = advance(head);
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            return false; // cheia
        }
        buffer_[head] = item;
        head_.store(nextHead, std::memory_order_release);
        return true;
    }

    bool pop(T& outItem) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // vazia
        }
        outItem = buffer_[tail];
        tail_.store(advance(tail), std::memory_order_release);
        return true;
    }

private:
    static size_t advance(size_t idx) { return (idx + 1) % Capacity; }

    std::array<T, Capacity> buffer_{};
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};
