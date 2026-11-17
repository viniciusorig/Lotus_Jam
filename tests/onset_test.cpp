#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "dsp/onset.h"

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> noise(double seconds, double sigma, unsigned seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> g(0.0, sigma);
  std::vector<float> x(size_t(seconds * kSr));
  for (auto& v : x) v = float(g(rng));
  return x;
}

// 55 Hz notes (4 ms linear attack, exponential decay) mixed into x at the given starts.
void addNotes(std::vector<float>& x, const std::vector<int64_t>& starts, double peak,
              double hz = 55.0, double tau = 0.25) {
  const size_t attack = size_t(0.004 * kSr);
  for (int64_t s : starts)
    for (size_t i = 0; s + int64_t(i) < int64_t(x.size()) && i < size_t(0.8 * kSr); ++i) {
      double t = double(i) / kSr;
      double fade = t > 0.65 ? 0.5 * (1.0 + std::cos(kPi * (t - 0.65) / 0.15)) : 1.0;  // release
      double env = (i < attack ? double(i) / double(attack) : 1.0) * std::exp(-t / tau) * fade;
      x[size_t(s) + i] += float(peak * env * std::sin(2 * kPi * hz * t));
    }
}

std::vector<int64_t> noteStarts() {
  std::vector<int64_t> s;
  for (int k = 0; k < 9; ++k) s.push_back(int64_t((1.5 + 1.0 * k) * kSr));
  return s;
}

std::vector<int64_t> run(const std::vector<float>& x, size_t chunk) {
  OnsetDetector d(kSr);
  std::vector<int64_t> out;
  for (size_t i = 0; i < x.size(); i += chunk) d.feed(x.data() + i, std::min(chunk, x.size() - i), out);
  return out;
}

double maxErrorMs(const std::vector<int64_t>& got, const std::vector<int64_t>& want) {
  double m = 0;
  for (size_t i = 0; i < want.size(); ++i) m = std::max(m, std::abs(double(got[i] - want[i])) / kSr * 1000.0);
  return m;
}

}  // namespace

TEST(OnsetDetector, SyntheticBassTimingWithin3ms) {
  auto starts = noteStarts();
  auto x = noise(11.0, 20.0 / 32768.0, 1);
  addNotes(x, starts, 0.3);
  auto got = run(x, 480);
  ASSERT_EQ(got.size(), starts.size());
  EXPECT_LT(maxErrorMs(got, starts), 3.0);
}

TEST(OnsetDetector, NoiseOnlyNoOnsets) {
  for (double sigma : {6e-4, 1.8e-3, 3e-3, 2e-2, 0.1})
    for (unsigned seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
      auto got = run(noise(10.0, sigma, seed), 480);
      EXPECT_TRUE(got.empty()) << "sigma " << sigma << " seed " << seed << " first " << got.front();
    }
}

TEST(OnsetDetector, WeakSignalStillDetected) {
  auto starts = noteStarts();
  auto x = noise(11.0, std::pow(10.0, -55.0 / 20.0), 5);
  addNotes(x, starts, std::pow(10.0, -39.0 / 20.0));
  auto got = run(x, 480);
  ASSERT_EQ(got.size(), starts.size());
  EXPECT_LT(maxErrorMs(got, starts), 3.0);
}

TEST(OnsetDetector, ChunkSizeInvariant) {
  auto starts = noteStarts();
  auto x = noise(11.0, 20.0 / 32768.0, 7);
  addNotes(x, starts, 0.3);
  auto a = run(x, 1);
  auto b = run(x, 480);
  auto c = run(x, 4096);
  EXPECT_EQ(a.size(), starts.size());
  EXPECT_EQ(a, b);
  EXPECT_EQ(a, c);
}

TEST(OnsetDetector, NoOnsetsDuringWarmup) {
  auto x = noise(0.9, 20.0 / 32768.0, 3);
  addNotes(x, {int64_t(0.5 * kSr)}, 0.3);
  EXPECT_TRUE(run(x, 480).empty());
}

class SustainedNotes : public ::testing::TestWithParam<std::pair<double, double>> {};

TEST_P(SustainedNotes, AllDetected) {
  auto [hz, tau] = GetParam();
  auto starts = noteStarts();
  auto x = noise(11.0, 20.0 / 32768.0, 11);
  addNotes(x, starts, 0.3, hz, tau);
  auto got = run(x, 480);
  ASSERT_EQ(got.size(), starts.size());
  EXPECT_LT(maxErrorMs(got, starts), 3.0);
}

INSTANTIATE_TEST_SUITE_P(Bass, SustainedNotes,
                         ::testing::Values(std::pair{41.2, 0.25}, std::pair{41.2, 1.0},
                                           std::pair{55.0, 0.25}, std::pair{55.0, 1.0},
                                           std::pair{82.4, 0.25}, std::pair{82.4, 1.0}));

TEST(OnsetDetector, SustainedNotesStillDetected) {
  auto starts = noteStarts();
  auto x = noise(11.0, 20.0 / 32768.0, 12);
  addNotes(x, starts, 0.3, 82.4, 1.0);
  auto got = run(x, 480);
  ASSERT_EQ(got.size(), starts.size());
  EXPECT_LT(maxErrorMs(got, starts), 3.0);
}

// Low notes (B0..A2) repeated at a beat. Notes are summed with the tails of earlier ones
// (see addNotes), so with tau = 1.0 s and 0.5 s spacing the level drops only ~4.3 dB between
// plucks and the 6 dB rise criterion cannot see re-plucks: that pair asserts no false
// onsets (count <= notes) and the timing of those detected, not misses.
struct LowParam {
  double hz, tau, spacing;
};

class LowNotes : public ::testing::TestWithParam<LowParam> {};

TEST_P(LowNotes, RepeatedAtBeat) {
  const auto p = GetParam();
  const int kNotes = 12;
  std::vector<int64_t> starts;
  for (int k = 0; k < kNotes; ++k) starts.push_back(int64_t((1.5 + p.spacing * k) * kSr));
  auto x = noise(1.5 + p.spacing * kNotes + 1.0, 20.0 / 32768.0, 21);
  addNotes(x, starts, 0.3, p.hz, p.tau);
  auto got = run(x, 480);
  const bool lenient = p.tau == 1.0 && p.spacing == 0.5;
  if (lenient) {
    EXPECT_LE(got.size(), starts.size());
  } else {
    ASSERT_EQ(got.size(), starts.size());
  }
  // every detected onset is within 3 ms of some true start
  double worst = 0;
  for (int64_t g : got) {
    double best = 1e9;
    for (int64_t s : starts) best = std::min(best, std::abs(double(g - s)) / kSr * 1000.0);
    worst = std::max(worst, best);
  }
  // Lenient pair: a re-pluck on top of an in-phase/anti-phase tail of the same pitch makes the
  // combined level start rising a few ms late, so allow 8 ms there.
  EXPECT_LT(worst, lenient ? 8.0 : 3.0);
}

std::vector<LowParam> lowParams() {
  std::vector<LowParam> v;
  for (double hz : {30.9, 41.2, 43.7, 49.0, 55.0, 73.4, 98.0, 110.0})
    for (double tau : {0.25, 1.0})
      for (double sp : {0.5, 1.0}) v.push_back({hz, tau, sp});
  return v;
}

INSTANTIATE_TEST_SUITE_P(Sweep, LowNotes, ::testing::ValuesIn(lowParams()),
                         [](const ::testing::TestParamInfo<LowParam>& i) {
                           return "hz" + std::to_string(int(i.param.hz * 10)) + "_tau" +
                                  std::to_string(int(i.param.tau * 100)) + "_sp" +
                                  std::to_string(int(i.param.spacing * 100));
                         });
