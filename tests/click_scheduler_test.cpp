#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio/click_scheduler.h"

namespace {
constexpr double kSr = 48000.0;

std::vector<float> renderAll(ClickScheduler& s, size_t total, size_t block) {
    std::vector<float> all(total * 2), buf(block * 2);
    for (size_t pos = 0; pos < total; pos += block) {
        const size_t n = std::min(block, total - pos);
        s.render(buf.data(), n, static_cast<int64_t>(pos));
        std::copy(buf.begin(), buf.begin() + n * 2, all.begin() + pos * 2);
    }
    return all;
}

double energy(const std::vector<float>& v, int64_t from, int64_t to) {
    double e = 0;
    for (int64_t f = from; f < to; ++f) e += double(v[2 * f]) * v[2 * f];
    return e;
}
}  // namespace

TEST(ClickScheduler, BeatPositions) {
    ClickScheduler s(kSr, 120, 4);
    const int64_t total = s.firstBeatFrame() + 10 * 24000 + 100;
    std::vector<BeatMark> got;
    int64_t pos = 0;
    const size_t sizes[] = {1, 480, 960, 777, 4096};
    for (size_t k = 0; pos < total; ++k) {
        const size_t b = sizes[k % 5];
        BeatMark m[8];
        const size_t n = s.beatsIn(pos, b, m, 8);
        for (size_t i = 0; i < n; ++i) got.push_back(m[i]);
        pos += static_cast<int64_t>(b);
    }
    ASSERT_EQ(got.size(), 11u);
    for (size_t n = 0; n < got.size(); ++n) {
        EXPECT_EQ(got[n].beat, static_cast<int64_t>(n));
        EXPECT_EQ(got[n].frame, std::llround(s.firstBeatFrame() + n * s.framesPerBeat()));
    }
    EXPECT_NEAR(s.framesPerBeat(), 24000.0, 1e-9);
}

TEST(ClickScheduler, NonIntegerBeatLengthAndZeroCountIn) {
    ClickScheduler s(44100.0, 97, 0);
    BeatMark m[4];
    ASSERT_EQ(s.beatsIn(s.firstBeatFrame(), 1, m, 4), 1u);
    EXPECT_EQ(m[0].beat, 0);
    EXPECT_EQ(s.beatsIn(s.firstBeatFrame() + 1, 100, m, 4), 0u);
    EXPECT_EQ(s.countIn(), 0);
    EXPECT_EQ(s.beatsIn(-1000000000000LL, 10, m, 4), 0u);  // huge/negative: no crash
}

TEST(ClickScheduler, UniformClicks) {
    ClickScheduler s(kSr, 100, 8);
    const auto out = renderAll(s, s.firstBeatFrame() + 9 * 28800, 960);
    std::vector<double> e;
    for (int n = 0; n < 9; ++n) {
        const int64_t a = std::llround(s.firstBeatFrame() + n * s.framesPerBeat());
        e.push_back(energy(out, a, a + 1440));
    }
    ASSERT_GT(e[0], 1.0);
    for (double x : e) EXPECT_NEAR(x, e[0], e[0] * 1e-9);
}

TEST(ClickScheduler, StartCueThenSilenceThenCount) {
    ClickScheduler s(kSr, 120, 4);
    const int64_t cue = std::llround(0.38 * kSr);
    EXPECT_EQ(s.firstBeatFrame(), cue + std::llround(0.7 * kSr));
    const auto out = renderAll(s, s.firstBeatFrame() + 2000, 480);
    const double ec = energy(out, 0, cue);
    EXPECT_GT(ec, 50.0);
    EXPECT_LT(ec, 2000.0);
    EXPECT_EQ(energy(out, cue + 2000, s.firstBeatFrame()), 0.0);  // tail decayed to ~0 (exp(-9*0.38) leaves < exact end)
    for (float v : out) EXPECT_LE(std::fabs(v), 1.0f);
    EXPECT_GT(energy(out, s.firstBeatFrame(), s.firstBeatFrame() + 1440), 1.0);
}

TEST(ClickScheduler, BlockBoundaryClicks) {
    ClickScheduler a(kSr, 240, 4), b(kSr, 240, 4), c(kSr, 240, 4);
    const size_t total = static_cast<size_t>(a.firstBeatFrame()) + 3 * 12000 + 500;
    const auto o1 = renderAll(a, total, 1), o480 = renderAll(b, total, 480), o960 = renderAll(c, total, 960);
    EXPECT_EQ(o1, o480);
    EXPECT_EQ(o480, o960);
}

TEST(ClickScheduler, RenderOverwritesAndHandlesExtremeFrames) {
    ClickScheduler s(kSr, 120, 4);
    std::vector<float> buf(64, 7.0f);
    s.render(buf.data(), 32, -1000);
    for (float v : buf) EXPECT_EQ(v, 0.0f);
    s.render(buf.data(), 32, int64_t{1} << 60);
    s.render(buf.data(), 0, 0);
}
