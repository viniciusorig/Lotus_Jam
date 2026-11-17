#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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

// ---------------------------------------------------------------- fix round 2: many gaps

namespace {
// Feeds blocks like a backend whose input ring keeps the head of a block and drops `tail`
// frames: written frames are counted, times follow the produced timeline.
struct Feed {
    explicit Feed(InputTimeline& t) : tl(t) {}
    InputTimeline& tl;
    double base = 2.0;
    int64_t produced = 0, delivered = 0;
    uint64_t dropped = 0;
    // Capture time of delivered frame d, recorded per written block for checking.
    struct Run {
        int64_t d0, d1;
        double t0;
    };
    std::vector<Run> runs;
    void block(int64_t tail) {
        const int64_t written = kBlock - tail;
        runs.push_back({delivered, delivered + written, base + double(produced) / kSr});
        delivered += written;
        dropped += uint64_t(tail);
        tl.add({delivered, base + double(produced + written) / kSr}, dropped);
        produced += kBlock;
    }
    // True capture time of delivered frame d.
    double truth(int64_t d) const {
        for (const auto& r : runs)
            if (d >= r.d0 && d < r.d1) return r.t0 + double(d - r.d0) / kSr;
        return -1;
    }
};
}  // namespace

TEST(InputTimeline, RepeatedPartialDropsKeepEveryFrameExact) {
    // The reviewer's probe: 40 clean blocks, then 10 blocks each dropping 100 frames. Nothing
    // was analysed yet (no prune), so every segment must be kept and every frame stay exact.
    InputTimeline tl(kSr);
    Feed f(tl);
    for (int k = 0; k < 40; ++k) f.block(0);
    for (int k = 0; k < 10; ++k) f.block(100);
    for (int k = 0; k < 40; ++k) f.block(0);
    EXPECT_EQ(tl.segments(), 11u);
    for (const auto& r : f.runs)
        for (int64_t d = r.d0; d < r.d1; d += 37) ASSERT_NEAR(tl.timeOf(double(d)), f.truth(d), 1e-6) << "frame " << d;
}

TEST(InputTimeline, PrunesOnlySegmentsBehindTheAnalysis) {
    InputTimeline tl(kSr);
    Feed f(tl);
    for (int k = 0; k < 40; ++k) f.block(0);
    for (int k = 0; k < 100; ++k) f.block(k % 2 ? 100 : 0);  // 50 drops; the last one's jump
    EXPECT_EQ(tl.segments(), 50u);                             // is not seen yet (no later point)
    // Analysis is at the start of the gappy region: nothing may be pruned that it still needs.
    const int64_t analysed = f.runs[45].d0;
    tl.prune(analysed);
    for (const auto& r : f.runs) {
        if (r.d1 <= analysed - InputTimeline::kKeepFrames) continue;
        for (int64_t d = std::max(r.d0, analysed - InputTimeline::kKeepFrames); d < r.d1; d += 53)
            ASSERT_NEAR(tl.timeOf(double(d)), f.truth(d), 1e-6) << "frame " << d;
    }
    // Far ahead: old segments go, frames behind the kept span are refused (NaN), never guessed.
    const int64_t late = f.runs[130].d0;
    tl.prune(late);
    EXPECT_LT(tl.segments(), 50u);
    EXPECT_TRUE(std::isnan(tl.timeOf(double(f.runs[50].d0))));
    for (int64_t d = late - InputTimeline::kKeepFrames; d < f.delivered; d += 59)
        ASSERT_NEAR(tl.timeOf(double(d)), f.truth(d), 1e-6) << "frame " << d;
    EXPECT_EQ(tl.evictions(), 0u);
}

TEST(InputTimeline, CapEvictsAndRefusesEvictedFrames) {
    InputTimeline tl(kSr);
    Feed f(tl);
    for (int k = 0; k < 30; ++k) f.block(0);
    for (int k = 0; k < 200; ++k) f.block(k % 2 ? 50 : 0);  // 100 gaps, never pruned
    EXPECT_LE(tl.segments(), InputTimeline::kMaxSegments);
    EXPECT_GT(tl.evictions(), 0u);
    EXPECT_TRUE(std::isnan(tl.timeOf(double(f.runs[10].d0))));  // evicted: refused
    const int64_t recent = f.runs[200].d0;
    for (int64_t d = recent; d < f.delivered; d += 41) ASSERT_NEAR(tl.timeOf(double(d)), f.truth(d), 1e-6);
}

TEST(InputTimeline, SubThresholdDropDoesNotStayPending) {
    InputTimeline tl(kSr);
    Feed f(tl);
    for (int k = 0; k < 30; ++k) f.block(0);
    f.block(10);  // 10 frames (0.2 ms) dropped
    for (int k = 0; k < 30; ++k) f.block(0);
    const size_t segs = tl.segments();
    // Later timestamp jitter of +-1 ms with no further drops must never split the timeline.
    int64_t d = f.delivered;
    double t = f.base + double(f.produced) / kSr;
    for (int k = 1; k <= 100; ++k) {
        d += kBlock;
        t += kBlock / kSr;
        EXPECT_FALSE(tl.add({d, t + (k % 2 ? 1e-3 : -1e-3)}, f.dropped)) << "block " << k;
    }
    EXPECT_EQ(tl.segments(), segs);
}
