#include "core/clock_map.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <random>

TEST(ClockMap, RecoversLinearMapping) {
    ClockMap m;
    for (int i = 0; i < 100; ++i) {
        const int64_t f = static_cast<int64_t>(i) * 4800;
        m.add({f, 1000.0 + static_cast<double>(f) / 48000.0});
    }
    ASSERT_TRUE(m.ready());
    EXPECT_NEAR(m.timeOf(480000), 1010.0, 1e-9);
    EXPECT_NEAR(m.frameAt(1010.0), 480000.0, 1e-4);
}

TEST(ClockMap, AveragesOutJitter) {
    ClockMap m;
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.002);
    for (int i = 0; i < 300; ++i) {
        const int64_t f = static_cast<int64_t>(i) * 4800;
        m.add({f, 1000.0 + static_cast<double>(f) / 48000.0 + noise(rng)});
    }
    ASSERT_TRUE(m.ready());
    for (double f : {0.0, 480000.0, 1000000.0}) {
        EXPECT_LT(std::abs(m.timeOf(f) - (1000.0 + f / 48000.0)), 0.0005) << f;
    }
}

TEST(ClockMap, DetectsClockDrift) {
    ClockMap m;
    const double rate = 48000.5;
    for (int i = 0; i < 100; ++i) {
        const int64_t f = static_cast<int64_t>(i) * 4800;
        m.add({f, 1000.0 + static_cast<double>(f) / rate});
    }
    ASSERT_TRUE(m.ready());
    const double slope = (m.timeOf(1000000.0) - m.timeOf(0.0)) / 1000000.0;
    EXPECT_NEAR(slope, 1.0 / rate, 1e-6);
    EXPECT_NEAR(m.timeOf(480000), 1000.0 + 480000.0 / rate, 1e-9);
}

TEST(ClockMap, NotReadyBeforeTwentyPoints) {
    ClockMap m;
    for (int i = 0; i < 19; ++i) {
        m.add({i * 4800, 1000.0 + i * 0.1});
        EXPECT_FALSE(m.ready());
    }
    m.add({19 * 4800, 1000.0 + 19 * 0.1});
    EXPECT_TRUE(m.ready());
}
