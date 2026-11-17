#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "dsp/notes.h"
#include "dsp/yin.h"

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Deterministic uniform noise with the requested RMS (portable LCG).
struct Noise {
  uint32_t s = 12345u;
  double next(double rms) {
    s = s * 1664525u + 1013904223u;
    double u = (s >> 8) / double(1u << 24) * 2.0 - 1.0;  // uniform [-1, 1), rms 1/sqrt(3)
    return u * rms * std::sqrt(3.0);
  }
};

// Sum of harmonics: amps[k] is the amplitude of harmonic k+1.
std::vector<float> tone(double hz, const std::vector<double>& amps, size_t n, double scale,
                        double noiseDbfs) {
  std::vector<float> x(n);
  Noise noise;
  double noiseRms = std::pow(10.0, noiseDbfs / 20.0);
  for (size_t i = 0; i < n; ++i) {
    double v = 0;
    for (size_t k = 0; k < amps.size(); ++k)
      v += amps[k] * std::sin(2 * kPi * hz * double(k + 1) * double(i) / kSr + 0.3 * double(k));
    x[i] = float(scale * v + noise.next(noiseRms));
  }
  return x;
}

}  // namespace

TEST(Yin, EachStringWithinOneCent) {
  double worst = 0;
  for (const auto& inst : instruments()) {
    Yin yin(kSr, inst.minHz, inst.maxHz);
    std::set<int> midis;
    for (const auto& t : inst.tunings) midis.insert(t.strings.begin(), t.strings.end());
    for (int m : midis) {
      double target = midiToHz(m);
      auto x = tone(target, {1.0, 0.5, 0.3, 0.2}, yin.requiredSamples(), 0.4, -50.0);
      PitchResult r = yin.process(x.data());
      ASSERT_GT(r.hz, 0.0) << inst.id << " " << noteName(m);
      double c = centsBetween(r.hz, target);
      EXPECT_LT(std::fabs(c), 1.0) << inst.id << " " << noteName(m) << " got " << r.hz;
      EXPECT_GE(r.probability, 0.8) << inst.id << " " << noteName(m);
      worst = std::max(worst, std::fabs(c));
    }
  }
  EXPECT_LT(worst, 1.0);
}

TEST(Yin, NoOctaveJumpOnWeakFundamental) {
  const Instrument* bass = nullptr;
  for (const auto& i : instruments())
    if (i.id == "baixo4") bass = &i;
  ASSERT_NE(bass, nullptr);
  Yin yin(kSr, bass->minHz, bass->maxHz);
  auto x = tone(41.2034, {0.2, 1.0}, yin.requiredSamples(), 0.5, -50.0);
  PitchResult r = yin.process(x.data());
  EXPECT_LT(std::fabs(centsBetween(r.hz, 41.2034)), 1.0) << "got " << r.hz;
}

TEST(Yin, SilenceIsUnvoiced) {
  Yin yin(kSr, 27.0, 400.0);
  std::vector<float> zeros(yin.requiredSamples(), 0.0f);
  EXPECT_EQ(yin.process(zeros.data()).hz, 0.0);
  // Low-level noise only (the PCM2902 floor is about -39 dBFS) is also unvoiced.
  auto noise = tone(100.0, {0.0}, yin.requiredSamples(), 0.0, -39.0);
  EXPECT_EQ(yin.process(noise.data()).hz, 0.0);
}

TEST(Yin, RequiredSamplesIsWindowPlusMaxLag) {
  Yin yin(kSr, 30.0, 400.0);
  EXPECT_EQ(yin.requiredSamples(), 2048u + 1600u);
}
