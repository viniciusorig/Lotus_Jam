#include "core/ring_buffer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

TEST(SpscRing, WriteReadRoundTrip) {
    SpscRing<int> ring(16);
    int in[10];
    for (int i = 0; i < 10; ++i) in[i] = i * 3 + 1;
    EXPECT_EQ(ring.write(in, 10), 10u);
    EXPECT_EQ(ring.readable(), 10u);
    int out[10] = {};
    EXPECT_EQ(ring.read(out, 10), 10u);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(out[i], in[i]);
    EXPECT_EQ(ring.readable(), 0u);
    EXPECT_EQ(ring.writable(), 16u);
}

TEST(SpscRing, WrapAround) {
    SpscRing<int> ring(8);
    int next = 0, expect = 0;
    for (int round = 0; round < 20; ++round) {
        int in[5];
        for (int& v : in) v = next++;
        ASSERT_EQ(ring.write(in, 5), 5u);
        int out[5];
        ASSERT_EQ(ring.read(out, 5), 5u);
        for (int v : out) EXPECT_EQ(v, expect++);
    }
}

TEST(SpscRing, FullRejectsExtra) {
    SpscRing<int> ring(8);
    int in[12];
    for (int i = 0; i < 12; ++i) in[i] = i;
    EXPECT_EQ(ring.write(in, 12), 8u);
    EXPECT_EQ(ring.writable(), 0u);
    EXPECT_EQ(ring.write(in, 1), 0u);
    int out[12] = {};
    EXPECT_EQ(ring.read(out, 12), 8u);
    for (int i = 0; i < 8; ++i) EXPECT_EQ(out[i], i);
}

TEST(SpscRing, ConcurrentStress) {
    constexpr uint32_t kTotal = 10'000'000;
    SpscRing<uint32_t> ring(1024);
    std::thread producer([&] {
        uint32_t buf[64];
        uint32_t next = 0;
        while (next < kTotal) {
            size_t n = std::min<uint32_t>(64, kTotal - next);
            for (size_t i = 0; i < n; ++i) buf[i] = next + static_cast<uint32_t>(i);
            size_t w = 0;
            while (w < n) {
                size_t k = ring.write(buf + w, n - w);
                if (k == 0) std::this_thread::yield();
                w += k;
            }
            next += static_cast<uint32_t>(n);
        }
    });
    uint32_t expect = 0;
    bool ok = true;
    uint32_t buf[64];
    while (expect < kTotal) {
        size_t n = ring.read(buf, 64);
        if (n == 0) { std::this_thread::yield(); continue; }
        for (size_t i = 0; i < n; ++i) {
            if (buf[i] != expect++) ok = false;
        }
    }
    producer.join();
    EXPECT_TRUE(ok);
    EXPECT_EQ(expect, kTotal);
}
