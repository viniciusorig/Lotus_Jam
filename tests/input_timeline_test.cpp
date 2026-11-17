#include <gtest/gtest.h>

#include <cstdint>

#include "app/input_timeline.h"

namespace {
constexpr double kSr = 48000.0;
constexpr int64_t kBlock = 480;
}  // namespace

TEST(InputTimeline, ContinuousStreamMapsExactly) {
    InputTimeline tl(kSr);
    EXPECT_TRUE(tl.empty());
    // Before the ClockMap is ready (1 point) the nominal rate from the first point is used.
    EXPECT_FALSE(tl.add({kBlock, 10.0}, 0));
    EXPECT_FALSE(tl.empty());
    EXPECT_NEAR(tl.timeOf(0), 10.0 - kBlock / kSr, 1e-9);
    for (int64_t k = 2; k <= 100; ++k) EXPECT_FALSE(tl.add({k * kBlock, 10.0 + double((k - 1) * kBlock) / kSr}, 0));
    EXPECT_EQ(tl.segments(), 1u);
    EXPECT_NEAR(tl.timeOf(24000), 10.0 + (24000.0 - kBlock) / kSr, 1e-7);
}

TEST(InputTimeline, DroppedFramesStartANewSegment) {
    InputTimeline tl(kSr);
    // 50 continuous blocks; frame F (delivered) captured at t = 5 + F/sr.
    int64_t f = 0;
    double t = 5.0;
    for (int k = 0; k < 50; ++k) {
        f += kBlock;
        t += kBlock / kSr;
        EXPECT_FALSE(tl.add({f, t}, 0));
    }
    const int64_t boundary = f;
    // 2000 frames are dropped by the backend: the next point's time jumps by 2000/sr.
    const uint64_t dropped = 2000;
    t += double(dropped) / kSr;
    bool split = false;
    for (int k = 0; k < 50; ++k) {
        f += kBlock;
        t += kBlock / kSr;
        split = tl.add({f, t}, dropped) || split;
    }
    EXPECT_TRUE(split);
    EXPECT_EQ(tl.segments(), 2u);
    // Frames before the gap keep the old mapping, frames after it use the new one.
    EXPECT_NEAR(tl.timeOf(double(boundary - 100)), 5.0 + double(boundary - 100) / kSr, 1e-6);
    EXPECT_NEAR(tl.timeOf(double(boundary + 100)), 5.0 + double(boundary + 100 + int64_t(dropped)) / kSr, 1e-6);
    EXPECT_NEAR(tl.timeOf(double(f)), t, 1e-6);
}

TEST(InputTimeline, FirstPointsAfterGapUseNominalRate) {
    InputTimeline tl(kSr);
    int64_t f = 0;
    double t = 1.0;
    for (int k = 0; k < 30; ++k) tl.add({f += kBlock, t += kBlock / kSr}, 0);
    t += 0.5;  // 24000 frames dropped
    EXPECT_TRUE(tl.add({f += kBlock, t += kBlock / kSr}, 24000));
    // Only one point in the new segment: frames just after the boundary are still exact.
    EXPECT_NEAR(tl.timeOf(double(f - kBlock + 10)), t - double(kBlock - 10) / kSr, 1e-9);
}

TEST(InputTimeline, TimingJitterWithoutDropsIsNotAGap) {
    InputTimeline tl(kSr);
    int64_t f = 0;
    for (int k = 1; k <= 200; ++k) {
        f += kBlock;
        const double jitter = (k % 3 == 0 ? 2e-3 : -1e-3);  // +-2 ms wake-up jitter
        EXPECT_FALSE(tl.add({f, 3.0 + double(f) / kSr + jitter}, 0));
    }
    EXPECT_EQ(tl.segments(), 1u);
}
