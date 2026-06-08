#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>

namespace rook::core {

template <typename T>
class SpScRingBuffer {
public:
    static constexpr std::size_t default_capacity = 65536;

    explicit SpScRingBuffer(std::size_t capacity = default_capacity)
        : m_capacity(next_power_of_two(capacity))
        , m_mask(m_capacity - 1)
        , m_buffer(std::make_unique<T[]>(m_capacity))
    {
        m_write_pos.store(0, std::memory_order_relaxed);
        m_read_pos.store(0, std::memory_order_relaxed);
    }

    ~SpScRingBuffer() = default;

    SpScRingBuffer(const SpScRingBuffer&) = delete;
    SpScRingBuffer& operator=(const SpScRingBuffer&) = delete;
    SpScRingBuffer(SpScRingBuffer&&) = delete;
    SpScRingBuffer& operator=(SpScRingBuffer&&) = delete;

    std::size_t capacity() const { return m_capacity; }

    std::size_t available() const {
        auto w = m_write_pos.load(std::memory_order_acquire);
        auto r = m_read_pos.load(std::memory_order_relaxed);
        return w - r;
    }

    std::size_t space() const {
        auto r = m_read_pos.load(std::memory_order_acquire);
        auto w = m_write_pos.load(std::memory_order_relaxed);
        return m_capacity - (w - r);
    }

    void clear() {
        auto w = m_write_pos.load(std::memory_order_relaxed);
        m_read_pos.store(w, std::memory_order_release);
    }

    std::size_t write(const T* data, std::size_t count) {
        auto r = m_read_pos.load(std::memory_order_acquire);
        auto w = m_write_pos.load(std::memory_order_relaxed);
        auto avail = m_capacity - (w - r);
        auto n = std::min(count, avail);
        if (n == 0) return 0;

        auto wi = w & m_mask;
        auto first = std::min(n, m_capacity - wi);
        std::copy_n(data, first, m_buffer.get() + wi);
        if (first < n)
            std::copy_n(data + first, n - first, m_buffer.get());

        m_write_pos.store(w + n, std::memory_order_release);
        return n;
    }

    std::size_t read(T* data, std::size_t count) {
        auto w = m_write_pos.load(std::memory_order_acquire);
        auto r = m_read_pos.load(std::memory_order_relaxed);
        auto avail = w - r;
        auto n = std::min(count, avail);
        if (n == 0) return 0;

        auto ri = r & m_mask;
        auto first = std::min(n, m_capacity - ri);
        std::copy_n(m_buffer.get() + ri, first, data);
        if (first < n)
            std::copy_n(m_buffer.get(), n - first, data + first);

        m_read_pos.store(r + n, std::memory_order_release);
        return n;
    }

    std::size_t drain(std::size_t count) {
        auto w = m_write_pos.load(std::memory_order_acquire);
        auto r = m_read_pos.load(std::memory_order_relaxed);
        auto avail = w - r;
        auto n = std::min(count, avail);
        if (n == 0) return 0;
        m_read_pos.store(r + n, std::memory_order_release);
        return n;
    }

private:
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
    std::unique_ptr<T[]> m_buffer;

    alignas(64) std::atomic<std::size_t> m_write_pos;
    alignas(64) std::atomic<std::size_t> m_read_pos;
};

} // namespace rook::core
