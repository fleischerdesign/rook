#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <new>
#include <utility>

namespace rook::core {

template <typename T>
class SpScQueue {
public:
    explicit SpScQueue(std::size_t capacity)
        : m_capacity(next_power_of_two(capacity))
        , m_mask(m_capacity - 1)
        , m_buffer(static_cast<Slot*>(
              ::operator new(m_capacity * sizeof(Slot), static_cast<std::align_val_t>(alignof(Slot)))))
    {
        for (std::size_t i = 0; i < m_capacity; ++i) {
            std::construct_at(&m_buffer[i].sequence, i);
            std::construct_at(&m_buffer[i].data);
        }
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }

    ~SpScQueue() {
        while (pop_internal()) {}
        for (std::size_t i = 0; i < m_capacity; ++i)
            std::destroy_at(&m_buffer[i]);
        ::operator delete(m_buffer, static_cast<std::align_val_t>(alignof(Slot)));
    }

    SpScQueue(const SpScQueue&) = delete;
    SpScQueue& operator=(const SpScQueue&) = delete;
    SpScQueue(SpScQueue&&) = delete;
    SpScQueue& operator=(SpScQueue&&) = delete;

    bool push(T item) {
        auto head = m_head.load(std::memory_order_relaxed);
        for (;;) {
            auto& slot = m_buffer[head & m_mask];
            auto seq = slot.sequence.load(std::memory_order_acquire);
            auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(head);

            if (diff == 0) {
                if (m_head.compare_exchange_weak(head, head + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    slot.data.emplace(std::move(item));
                    slot.sequence.store(head + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                head = m_head.load(std::memory_order_relaxed);
            }
        }
    }

    std::optional<T> pop() {
        auto tail = m_tail.load(std::memory_order_relaxed);
        for (;;) {
            auto& slot = m_buffer[tail & m_mask];
            auto seq = slot.sequence.load(std::memory_order_acquire);
            auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(tail + 1);

            if (diff == 0) {
                if (m_tail.compare_exchange_weak(tail, tail + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    T item = std::move(*slot.data);
                    slot.data.reset();
                    slot.sequence.store(tail + m_mask + 1, std::memory_order_release);
                    return item;
                }
            } else if (diff < 0) {
                return std::nullopt;
            } else {
                tail = m_tail.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct Slot {
        std::atomic<std::size_t> sequence;
        std::optional<T> data;
    };

    static constexpr std::size_t next_power_of_two(std::size_t v) {
        if (v <= 1) return 2;
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        return v + 1;
    }

    std::optional<T> pop_internal() {
        return pop();
    }

    std::size_t m_capacity;
    std::size_t m_mask;
    Slot* m_buffer;
    std::atomic<std::size_t> m_head;
    std::atomic<std::size_t> m_tail;
};

template <typename T>
class MpMcQueue {
public:
    explicit MpMcQueue(std::size_t capacity)
        : m_capacity(next_power_of_two(capacity))
        , m_mask(m_capacity - 1)
        , m_buffer(static_cast<Slot*>(
              ::operator new(m_capacity * sizeof(Slot), static_cast<std::align_val_t>(alignof(Slot)))))
    {
        for (std::size_t i = 0; i < m_capacity; ++i) {
            std::construct_at(&m_buffer[i].sequence, i);
            std::construct_at(&m_buffer[i].data);
        }
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }

    ~MpMcQueue() {
        while (pop()) {}
        for (std::size_t i = 0; i < m_capacity; ++i) {
            std::destroy_at(&m_buffer[i].data);
            std::destroy_at(&m_buffer[i].sequence);
        }
        ::operator delete(m_buffer, static_cast<std::align_val_t>(alignof(Slot)));
    }

    MpMcQueue(const MpMcQueue&) = delete;
    MpMcQueue& operator=(const MpMcQueue&) = delete;
    MpMcQueue(MpMcQueue&&) = delete;
    MpMcQueue& operator=(MpMcQueue&&) = delete;

    bool push(T item) {
        auto head = m_head.load(std::memory_order_acquire);
        for (;;) {
            auto& slot = m_buffer[head & m_mask];
            auto seq = slot.sequence.load(std::memory_order_acquire);
            auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(head);

            if (diff == 0) {
                if (m_head.compare_exchange_weak(head, head + 1,
                        std::memory_order_release, std::memory_order_relaxed)) {
                    slot.data.emplace(std::move(item));
                    slot.sequence.store(head + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                head = m_head.load(std::memory_order_acquire);
            }
        }
    }

    std::optional<T> pop() {
        auto tail = m_tail.load(std::memory_order_acquire);
        for (;;) {
            auto& slot = m_buffer[tail & m_mask];
            auto seq = slot.sequence.load(std::memory_order_acquire);
            auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(tail + 1);

            if (diff == 0) {
                if (m_tail.compare_exchange_weak(tail, tail + 1,
                        std::memory_order_release, std::memory_order_relaxed)) {
                    T item = std::move(*slot.data);
                    slot.sequence.store(tail + m_mask + 1, std::memory_order_release);
                    return item;
                }
            } else if (diff < 0) {
                return std::nullopt;
            } else {
                tail = m_tail.load(std::memory_order_acquire);
            }
        }
    }

private:
    struct Slot {
        std::atomic<std::size_t> sequence;
        std::optional<T> data;
    };

    static constexpr std::size_t next_power_of_two(std::size_t v) {
        if (v <= 1) return 2;
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        return v + 1;
    }

    std::size_t m_capacity;
    std::size_t m_mask;
    Slot* m_buffer;
    alignas(64) std::atomic<std::size_t> m_head;
    alignas(64) std::atomic<std::size_t> m_tail;
};

} // namespace rook::core
