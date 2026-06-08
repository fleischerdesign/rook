#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "rook/core/lockfree_ring_buffer.hpp"

using namespace rook::core;

TEST(SpScRingBufferTest, DefaultCapacity) {
    SpScRingBuffer<int> rb;
    EXPECT_EQ(rb.capacity(), 65536);
}

TEST(SpScRingBufferTest, CapacityRounding) {
    SpScRingBuffer<int> rb(100);
    EXPECT_EQ(rb.capacity(), 128);

    SpScRingBuffer<int> rb2(1);
    EXPECT_EQ(rb2.capacity(), 2);
}

TEST(SpScRingBufferTest, EmptyBuffer) {
    SpScRingBuffer<int> rb(16);
    EXPECT_EQ(rb.available(), 0);
    EXPECT_EQ(rb.space(), 16);

    int val = 42;
    EXPECT_EQ(rb.read(&val, 1), 0);
    EXPECT_EQ(val, 42);
}

TEST(SpScRingBufferTest, SingleElementWriteRead) {
    SpScRingBuffer<int> rb(16);
    int val = 42;
    EXPECT_EQ(rb.write(&val, 1), 1);
    EXPECT_EQ(rb.available(), 1);

    int out = 0;
    EXPECT_EQ(rb.read(&out, 1), 1);
    EXPECT_EQ(out, 42);
    EXPECT_EQ(rb.available(), 0);
}

TEST(SpScRingBufferTest, BulkWriteRead) {
    SpScRingBuffer<int> rb(16);
    std::vector<int> data = {1, 2, 3, 4, 5};
    EXPECT_EQ(rb.write(data.data(), 5), 5);
    EXPECT_EQ(rb.available(), 5);

    std::vector<int> out(5, 0);
    EXPECT_EQ(rb.read(out.data(), 5), 5);
    EXPECT_EQ(out, data);
}

TEST(SpScRingBufferTest, PartialWriteFull) {
    SpScRingBuffer<int> rb(16);
    std::vector<int> data(20, 1);
    EXPECT_EQ(rb.write(data.data(), 20), 16);
    EXPECT_EQ(rb.available(), 16);
    EXPECT_EQ(rb.space(), 0);
    EXPECT_EQ(rb.write(data.data(), 1), 0);
}

TEST(SpScRingBufferTest, PartialRead) {
    SpScRingBuffer<int> rb(16);
    std::vector<int> data = {1, 2, 3};
    rb.write(data.data(), 3);

    std::vector<int> out(10, 0);
    EXPECT_EQ(rb.read(out.data(), 10), 3);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    EXPECT_EQ(out[2], 3);
    EXPECT_EQ(rb.available(), 0);
}

TEST(SpScRingBufferTest, WrapAround) {
    SpScRingBuffer<int> rb(8);

    std::vector<int> first(6, 42);
    EXPECT_EQ(rb.write(first.data(), 6), 6);
    EXPECT_EQ(rb.read(first.data(), 4), 4);

    std::vector<int> second = {1, 2, 3, 4, 5, 6};
    EXPECT_EQ(rb.write(second.data(), 6), 6);

    std::vector<int> out(8, 0);
    EXPECT_EQ(rb.read(out.data(), 8), 8);
    EXPECT_EQ(out[0], 42);
    EXPECT_EQ(out[1], 42);
    EXPECT_EQ(out[2], 1);
    EXPECT_EQ(out[3], 2);
    EXPECT_EQ(out[4], 3);
    EXPECT_EQ(out[5], 4);
    EXPECT_EQ(out[6], 5);
    EXPECT_EQ(out[7], 6);
}

TEST(SpScRingBufferTest, Drain) {
    SpScRingBuffer<int> rb(16);
    std::vector<int> data = {1, 2, 3, 4, 5};
    rb.write(data.data(), 5);

    EXPECT_EQ(rb.drain(3), 3);
    EXPECT_EQ(rb.available(), 2);

    int out = 0;
    EXPECT_EQ(rb.read(&out, 1), 1);
    EXPECT_EQ(out, 4);
}

TEST(SpScRingBufferTest, DrainMoreThanAvailable) {
    SpScRingBuffer<int> rb(16);
    int val = 1;
    rb.write(&val, 1);
    EXPECT_EQ(rb.drain(10), 1);
    EXPECT_EQ(rb.available(), 0);
}

TEST(SpScRingBufferTest, Clear) {
    SpScRingBuffer<int> rb(16);
    std::vector<int> data = {1, 2, 3};
    rb.write(data.data(), 3);
    EXPECT_EQ(rb.available(), 3);

    rb.clear();
    EXPECT_EQ(rb.available(), 0);
    EXPECT_EQ(rb.space(), 16);
}

TEST(SpScRingBufferTest, AlternatingWriteRead) {
    SpScRingBuffer<int> rb(64);
    for (int batch = 0; batch < 10; ++batch) {
        std::vector<int> data(5, batch);
        EXPECT_EQ(rb.write(data.data(), 5), 5);
        std::vector<int> out(5, 0);
        EXPECT_EQ(rb.read(out.data(), 5), 5);
        EXPECT_EQ(out, data);
    }
}

TEST(SpScRingBufferTest, ThreadSafety) {
    SpScRingBuffer<int> rb(262144);
    constexpr int total = 10'000'000;
    std::atomic<bool> reader_done{false};
    std::atomic<int64_t> reader_sum{0};
    std::atomic<int64_t> writer_sum{0};

    std::thread writer([&]() {
        for (int i = 0; i < total; ) {
            int batch[256];
            int j = 0;
            for (; j < 256 && i + j < total; ++j)
                batch[j] = i + j;
            i += j;
            while (rb.write(batch, j) < static_cast<std::size_t>(j)) {
                std::this_thread::yield();
            }
            writer_sum.fetch_add(j, std::memory_order_relaxed);
        }
    });

    std::thread reader([&]() {
        while (!reader_done.load(std::memory_order_acquire)) {
            int batch[256];
            auto n = rb.read(batch, 256);
            int64_t sum = 0;
            for (std::size_t k = 0; k < n; ++k)
                sum += batch[k];
            reader_sum.fetch_add(sum, std::memory_order_relaxed);
            if (n == 0) {
                if (writer_sum.load(std::memory_order_acquire) >= total &&
                    rb.available() == 0) {
                    break;
                }
                std::this_thread::yield();
            }
        }
        reader_done.store(true, std::memory_order_release);
    });

    writer.join();
    reader.join();

    int64_t expected = static_cast<int64_t>(total) * (total - 1) / 2;
    EXPECT_EQ(reader_sum.load(), expected);
}
