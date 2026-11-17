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
void addNotes(std::vector<float>& x, const std::vector<int64_t>& starts, double peak) {
  const size_t attack = size_t(0.004 * kSr);
  for (int64_t s : starts)
    for (size_t i = 0; s + int64_t(i) < int64_t(x.size()) && i < size_t(0.8 * kSr); ++i) {
      double t = double(i) / kSr;
      double env = (i < attack ? double(i) / double(attack) : 1.0) * std::exp(-t / 0.25);
      x[size_t(s) + i] += float(peak * env * std::sin(2 * kPi * 55.0 * t));
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
