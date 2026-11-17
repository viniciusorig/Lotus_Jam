#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "dsp/onset.h"

#include "onset_signals.h"

namespace {

using namespace onsetsig;

// Noise for the tests that predate onset_signals.h, kept bit-identical to what they always
// used (std::normal_distribution, whose algorithm is implementation-defined). WeakSignal sits
// 2 dB above the 5 x floor guard and misses a note on ~10-15% of noise seeds with either this or
// the portable generator, so re-drawing its noise would only trade one seed for another.
std::vector<float> legacyNoise(double seconds, double sigma, unsigned seed) {
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

// Bounds for notes played over a still-ringing tail (long takes, LowNotes `_harm`). They are
// measured limits of the method, not slack; isolated notes keep the 3 ms bound of the tests above.
// Each bound covers the WORST single 60 s take of the full matrix (f0 41.2-246.9 Hz x sine/
// harmonic x replace/overlap x peak 0.05/0.3, tau 0.4 s, 0.5 s spacing) over seeds 1-40 with
// these generators (1920 takes), since each test checks one take; peak 0.05 and 0.3 behave
// the same.
//
// Timing (masking by the ringing tail): on a tail only ~11 dB down, the walk-back over the raw
// 5 ms RMS stops early on the ripple of the 5 ms RMS (f0 below ~110 Hz) and on the dip that
// damping plus destructive interference makes in the first ms, so re-plucks are reported
// 2-4 ms late on average, with a spread that grows as f0 falls and when partials ring on.
// Tolerance = window holding >= 95% of the notes in every take (worst p95 |err| + 1 ms):
// worst 11 ms for f0 >= 98 Hz, 12 ms for 55-98 Hz and 41.2 Hz sine, 19 ms for harmonic notes
// below 60 Hz. Median |err| <= 5 ms in every take (<= 4 ms in all but harmonic below 60 Hz).
constexpr double kTailTolMs = 12.0;         // f0 >= 98 Hz
constexpr double kTailTolLowMs = 13.0;      // below 98 Hz, except harmonic below 60 Hz
constexpr double kTailTolHarmLowMs = 20.0;  // harmonic timbre below 60 Hz
constexpr double kTailInTolPct = 95.0;
constexpr double kTailMedianMs = 5.0;
//
// Detection (6 dB rise criterion): a re-pluck is seen only if the level rises > 6 dB within
// 15 ms. When notes ring on (overlap), the new note adds to the ringing tails with a random
// phase: with a pure tone at the same pitch (same-pitch phasor sum) up to ~17% of notes rise
// < 6 dB in TRUE amplitude (worst take 100/120, worst 10 s segment 13/20); with harmonic notes
// the faster partials still mark most attacks (worst take 113/120 at 41.2 Hz, worst segment
// 17/20), and harmonic replace below 60 Hz loses a rare one (worst 118/120, segment 18/20).
// Replace mode otherwise: 120/120 in every take. 0 false onsets in all 1920 takes.
constexpr int kDetPctDefault = 95;              // replace mode, per 10 s segment (19 of 20)
constexpr int kDetPctOverlapSineTotal = 80;     // same-pitch phasor sum, overall
constexpr int kDetPctOverlapSineSeg = 60;       // ... per segment (12 of 20)
constexpr int kDetPctHarmOverlapTotal = 93;     // harmonic overlap, and harmonic below 60 Hz
constexpr int kDetPctHarmOverlapSeg = 80;       // ... per segment (16 of 20)

double medianOf(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
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
  auto x = legacyNoise(11.0, 20.0 / 32768.0, 1);
  addNotes(x, starts, 0.3);
  auto got = run(x, 480);
  ASSERT_EQ(got.size(), starts.size());
  EXPECT_LT(maxErrorMs(got, starts), 3.0);
}

TEST(OnsetDetector, NoiseOnlyNoOnsets) {
  for (double sigma : {6e-4, 1.8e-3, 3e-3, 2e-2, 0.1})
    for (unsigned seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
      auto got = run(legacyNoise(10.0, sigma, seed), 480);
      EXPECT_TRUE(got.empty()) << "sigma " << sigma << " seed " << seed << " first " << got.front();
    }
}

TEST(OnsetDetector, WeakSignalStillDetected) {
  auto starts = noteStarts();
  auto x = legacyNoise(11.0, std::pow(10.0, -55.0 / 20.0), 5);
  addNotes(x, starts, std::pow(10.0, -39.0 / 20.0));
  auto got = run(x, 480);
  ASSERT_EQ(got.size(), starts.size());
  EXPECT_LT(maxErrorMs(got, starts), 3.0);
}

TEST(OnsetDetector, ChunkSizeInvariant) {
  auto starts = noteStarts();
  auto x = legacyNoise(11.0, 20.0 / 32768.0, 7);
  addNotes(x, starts, 0.3);
  auto a = run(x, 1);
  auto b = run(x, 480);
  auto c = run(x, 4096);
  EXPECT_EQ(a.size(), starts.size());
  EXPECT_EQ(a, b);
  EXPECT_EQ(a, c);
}

TEST(OnsetDetector, NoOnsetsDuringWarmup) {
  auto x = legacyNoise(0.9, 20.0 / 32768.0, 3);
  addNotes(x, {int64_t(0.5 * kSr)}, 0.3);
  EXPECT_TRUE(run(x, 480).empty());
}

class SustainedNotes : public ::testing::TestWithParam<std::pair<double, double>> {};

TEST_P(SustainedNotes, AllDetected) {
  auto [hz, tau] = GetParam();
  auto starts = noteStarts();
  auto x = legacyNoise(11.0, 20.0 / 32768.0, 11);
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
  auto x = legacyNoise(11.0, 20.0 / 32768.0, 12);
  addNotes(x, starts, 0.3, 82.4, 1.0);
  auto got = run(x, 480);
  ASSERT_EQ(got.size(), starts.size());
  EXPECT_LT(maxErrorMs(got, starts), 3.0);
}

// Low notes (B0..A2) repeated at a beat. Notes are summed with the tails of earlier ones
// (see addNotes), so with tau = 1.0 s and 0.5 s spacing the level drops only ~4.3 dB between
// plucks and the 6 dB rise criterion cannot see re-plucks: that pair asserts no false
// onsets (count <= notes) and the timing of those detected, not misses.
// Related limitation of the 36 ms peak-hold: a re-pluck preceded by a mute shorter than
// ~20 ms on a string that rings long is hidden, because the held envelope still carries the
// pre-mute level when the new note arrives (tau = 1.0 s, equal-level re-plucks every 0.5 s,
// mute 0-20 ms before the pluck: only 8-21 of 48 detected at 41.2 Hz, 5-13 at 55 Hz; 30 ms:
// 34-39 of 48; >= 50 ms: all 48).
struct LowParam {
  double hz, tau, spacing;
  bool harmonic = false;  // kHarmonic timbre with random phases (addPlucks) instead of addNotes
};

class LowNotes : public ::testing::TestWithParam<LowParam> {};

TEST_P(LowNotes, RepeatedAtBeat) {
  const auto p = GetParam();
  const int kNotes = 12;
  std::vector<int64_t> starts;
  for (int k = 0; k < kNotes; ++k) starts.push_back(int64_t((1.5 + p.spacing * k) * kSr));
  auto x = p.harmonic ? noise(1.5 + p.spacing * kNotes + 1.0, 20.0 / 32768.0, 21)
                      : legacyNoise(1.5 + p.spacing * kNotes + 1.0, 20.0 / 32768.0, 21);
  if (p.harmonic)
    addPlucks(x, starts, 0.3, p.hz, kHarmonic, p.tau, false, 22);
  else
    addNotes(x, starts, 0.3, p.hz, p.tau);
  auto got = run(x, 480);
  const bool lenient = p.tau == 1.0 && p.spacing == 0.5;
  std::vector<double> errs;  // distance of each detected onset to the nearest true start
  for (int64_t g : got) {
    double best = 1e9;
    for (int64_t s : starts) best = std::min(best, std::abs(double(g - s)) / kSr * 1000.0);
    errs.push_back(best);
  }
  const double worst = errs.empty() ? 0.0 : *std::max_element(errs.begin(), errs.end());
  if (p.harmonic) {
    // Notes ring on (0.5 s apart over a tau 0.4 s tail): tail regime, but 12 notes are too few
    // for the 95% window of the long takes (checked on 120 notes in LongPlaying). Over phase
    // seeds 1-40 this case gave 10-12 detections, median 2-8 ms, worst onset 27 ms, never a
    // false one. So: no false onsets (each within 30 ms of a start), >= 10 of 12, median <= 8 ms.
    EXPECT_LE(got.size(), starts.size());
    EXPECT_GE(got.size(), starts.size() - 2);
    EXPECT_LE(worst, 30.0);
    EXPECT_LE(medianOf(errs), 8.0);
    return;
  }
  if (lenient) {
    EXPECT_LE(got.size(), starts.size());
  } else {
    ASSERT_EQ(got.size(), starts.size());
  }
  // every detected onset is within 3 ms of some true start
  // Lenient pair: a re-pluck on top of an in-phase/anti-phase tail of the same pitch makes the
  // combined level start rising a few ms late, so allow 8 ms there.
  EXPECT_LT(worst, lenient ? 8.0 : 3.0);
}

std::vector<LowParam> lowParams() {
  std::vector<LowParam> v;
  for (double hz : {30.9, 41.2, 43.7, 49.0, 55.0, 73.4, 98.0, 110.0})
    for (double tau : {0.25, 1.0})
      for (double sp : {0.5, 1.0}) v.push_back({hz, tau, sp});
  v.push_back({41.2, 0.4, 0.5, true});  // harmonic timbre, random phases, notes ring on
  return v;
}

INSTANTIATE_TEST_SUITE_P(Sweep, LowNotes, ::testing::ValuesIn(lowParams()),
                         [](const ::testing::TestParamInfo<LowParam>& i) {
                           return "hz" + std::to_string(int(i.param.hz * 10)) + "_tau" +
                                  std::to_string(int(i.param.tau * 100)) + "_sp" +
                                  std::to_string(int(i.param.spacing * 100)) +
                                  (i.param.harmonic ? "_harm" : "");
                         });

// Continuous playing: 60 s of notes at 120 BPM (0.5 s apart), tau 0.4 s, so the level falls
// only ~11 dB between plucks and never reaches the noise. Detection must not collapse late
// (it used to after ~11 s, once the 10 s noise-floor window held no real silence): every 10 s
// segment keeps its detection rate, no false onsets anywhere, timing within the tail bounds
// (kTail* / kDetPct* above; the bounds depend on f0, timbre and mode).
struct LongParam {
  double hz;
  bool harmonic, replace;
  double peak;
};

void checkLongPlaying(const LongParam& p, unsigned seed) {
  const int kNotes = 120;
  std::vector<int64_t> starts;
  for (int k = 0; k < kNotes; ++k) starts.push_back(int64_t((1.5 + 0.5 * k) * kSr));
  auto x = noise(1.5 + 0.5 * kNotes + 1.0, 20.0 / 32768.0, seed);
  addPlucks(x, starts, p.peak, p.hz, p.harmonic ? kHarmonic : kSine, 0.4, p.replace, seed * 17 + 3);
  auto got = run(x, 480);
  const bool overlapSine = !p.replace && !p.harmonic;
  const bool harmLow = p.harmonic && p.hz < 60.0;
  const bool harmWeak = p.harmonic && (!p.replace || harmLow);
  const double tol = harmLow ? kTailTolHarmLowMs : (p.hz >= 98.0 ? kTailTolMs : kTailTolLowMs);
  const int segPct = overlapSine ? kDetPctOverlapSineSeg : harmWeak ? kDetPctHarmOverlapSeg : kDetPctDefault;
  const int totPct = overlapSine ? kDetPctOverlapSineTotal : harmWeak ? kDetPctHarmOverlapTotal : kDetPctDefault;
  int perSegment[6] = {}, withinTol = 0, matched = 0;
  std::vector<double> errs;
  std::vector<bool> used(got.size(), false);
  for (size_t n = 0; n < starts.size(); ++n) {
    int best = -1;
    double bd = 1e9;
    for (size_t g = 0; g < got.size(); ++g) {
      double d = std::abs(double(got[g] - starts[n])) / kSr * 1000.0;
      if (!used[g] && d < bd) bd = d, best = int(g);
    }
    if (best < 0 || bd > 30.0) continue;
    used[size_t(best)] = true;
    ++matched;
    ++perSegment[n / 20];
    withinTol += bd <= tol;
    errs.push_back(bd);
  }
  for (int s = 0; s < 6; ++s)
    EXPECT_GE(perSegment[s] * 100, 20 * segPct) << "segment " << s * 10 << "-" << s * 10 + 10 << " s";
  EXPECT_GE(matched * 100, kNotes * totPct) << matched << " of " << kNotes << " detected";
  EXPECT_EQ(got.size(), size_t(matched)) << "false onsets";
  EXPECT_GE(withinTol * 100.0, matched * kTailInTolPct) << withinTol << " of " << matched << " within " << tol << " ms";
  EXPECT_LE(medianOf(errs), kTailMedianMs);
}

TEST(OnsetDetector, LongContinuousPlayingStaysDetected) { checkLongPlaying({164.8, false, true, 0.3}, 1); }

class LongPlaying : public ::testing::TestWithParam<LongParam> {};

TEST_P(LongPlaying, StaysDetected) { checkLongPlaying(GetParam(), 2); }

// Representative subset (the full f0 x timbre x mode x peak matrix x 40 seeds runs in an offline
// program on the same generators, see the Task 5d report): every f0, both timbres, both modes
// and both peaks appear.
INSTANTIATE_TEST_SUITE_P(Continuous, LongPlaying,
                         ::testing::Values(LongParam{41.2, true, false, 0.05}, LongParam{55.0, false, true, 0.3},
                                           LongParam{73.4, true, true, 0.05}, LongParam{98.0, false, false, 0.3},
                                           LongParam{164.8, true, false, 0.05}, LongParam{246.9, false, true, 0.05},
                                           LongParam{246.9, true, false, 0.3}),
                         [](const ::testing::TestParamInfo<LongParam>& i) {
                           return "hz" + std::to_string(int(i.param.hz * 10)) + (i.param.harmonic ? "_harm" : "_sine") +
                                  (i.param.replace ? "_replace" : "_overlap") + "_pk" +
                                  std::to_string(int(i.param.peak * 100));
                         });

// 20 s playing, 30 s of noise only, 20 s playing: both playing phases detected, nothing in the
// pause (the floor must come back down to the noise and notes after the pause still pass).
TEST(OnsetDetector, RecoversAfterPause) {
  std::vector<int64_t> first, second;
  for (int k = 0; k < 40; ++k) first.push_back(int64_t((1.5 + 0.5 * k) * kSr));
  for (int k = 0; k < 40; ++k) second.push_back(int64_t((51.5 + 0.5 * k) * kSr));
  auto x = noise(72.0, 20.0 / 32768.0, 31);
  // Phase 1 is rendered apart and muted (8 ms linear) at 21.5 s, so the pause is really silent.
  std::vector<float> phase1(x.size(), 0.0f);
  addPlucks(phase1, first, 0.3, 98.0, kHarmonic, 0.4, true, 32);
  const size_t mute = size_t(21.5 * kSr), damp = size_t(0.008 * kSr);
  for (size_t i = mute; i < phase1.size(); ++i)
    phase1[i] *= i < mute + damp ? float(1.0 - double(i - mute) / double(damp)) : 0.0f;
  for (size_t i = 0; i < x.size(); ++i) x[i] += phase1[i];
  addPlucks(x, second, 0.3, 98.0, kHarmonic, 0.4, true, 33);
  auto got = run(x, 480);
  auto count = [&](double from, double to) {
    return std::count_if(got.begin(), got.end(), [&](int64_t g) { return g >= from * kSr && g < to * kSr; });
  };
  EXPECT_GE(count(1.4, 21.5), 39);
  EXPECT_EQ(count(21.5, 51.4), 0);
  EXPECT_GE(count(51.4, 72.0), 39);
  EXPECT_EQ(got.size(), size_t(count(1.4, 21.5) + count(51.4, 72.0)));
  for (int64_t g : got) {
    double best = 1e9;
    for (auto* v : {&first, &second})
      for (int64_t s : *v) best = std::min(best, std::abs(double(g - s)) / kSr * 1000.0);
    EXPECT_LT(best, 30.0) << "onset at " << double(g) / kSr << " s";
  }
}

// ---- Non-stationary disturbances: the continuous-playing fix must keep the noise guard. ----
// The p99/p3 gate opens for anything that is not stationary noise (hum with harmonics, bursts,
// thumps, ticks), so these assert that the floor guard still holds there: never more false
// onsets (and never fewer notes) than the plain 5 x p3 floor that preceded the gate.

// Mains hum (60 Hz with a 3rd harmonic, or with a 2nd harmonic) at -39 / -45 dBFS RMS over
// -55 dBFS noise, and plucks every 4 s at G1 / G#1, harmonic tau 1.0 s: the decaying note beats
// against the hum. The plain 5 x p3 floor already let a few beats through (pre-existing, see
// onset.h), so the bound is that floor's own count on these exact signals (oldFalse / oldHits,
// measured with the detector as of 0be7735; the uncapped gate of 56cb646 gave 0-18 more false
// onsets per case here).
struct HumParam {
  double hz, rmsDbfs, h2, h3, peak;
  int oldFalse, oldHits;  // over seeds 1 and 2, 20 notes each
};

class HumWithLowNotes : public ::testing::TestWithParam<HumParam> {};

TEST_P(HumWithLowNotes, NoMoreFalseOnsetsThanPlainFloor) {
  const auto p = GetParam();
  int falseOnsets = 0, hits = 0;
  for (unsigned seed : {1u, 2u}) {
    std::vector<int64_t> starts;
    for (int k = 0; k < 20; ++k) starts.push_back(int64_t((1.5 + 4.0 * k) * kSr));
    auto x = noise(1.5 + 4.0 * 20, dbToAmp(-55.0), seed);
    addHum(x, 60.0, p.h2, p.h3, p.rmsDbfs);
    addPlucks(x, starts, p.peak, p.hz, kHarmonic, 1.0, false, seed + 11);
    for (int64_t g : run(x, 480)) {
      double best = 1e9;
      for (int64_t s : starts) best = std::min(best, std::abs(double(g - s)) / kSr * 1000.0);
      best <= 30.0 ? ++hits : ++falseOnsets;
    }
  }
  EXPECT_LE(falseOnsets, p.oldFalse);
  EXPECT_GE(hits, p.oldHits);
}

INSTANTIATE_TEST_SUITE_P(Mains60, HumWithLowNotes,
                         ::testing::Values(HumParam{49.0, -39.0, 0.0, 0.3, 0.05, 1, 40}, HumParam{51.9, -39.0, 0.0, 0.3, 0.05, 0, 40},
                                           HumParam{49.0, -39.0, 0.0, 0.3, 0.02, 0, 18}, HumParam{51.9, -39.0, 0.0, 0.3, 0.02, 0, 17},
                                           HumParam{49.0, -45.0, 0.0, 0.3, 0.05, 0, 40}, HumParam{51.9, -45.0, 0.0, 0.3, 0.05, 0, 40},
                                           HumParam{49.0, -45.0, 0.0, 0.3, 0.02, 0, 37}, HumParam{51.9, -45.0, 0.0, 0.3, 0.02, 0, 38},
                                           HumParam{51.9, -39.0, 0.5, 0.0, 0.05, 5, 40}, HumParam{51.9, -39.0, 0.5, 0.0, 0.02, 1, 8}),
                         [](const ::testing::TestParamInfo<HumParam>& i) {
                           return "hz" + std::to_string(int(i.param.hz * 10)) + "_hum" + std::to_string(int(-i.param.rmsDbfs)) +
                                  (i.param.h2 > 0 ? "_h2" : "_h3") + "_pk" + std::to_string(int(i.param.peak * 100));
                         });

// 20 s of playing, then a pause with hand contact +6 / +12 dB over the noise floor (thumps:
// 25 Hz knocks; ticks: muted-string touches), one every ~2 s: nothing may fire in the pause.
TEST(OnsetDetector, NoOnsetsFromTouchesInPause) {
  for (bool thump : {false, true})
    for (double overDb : {6.0, 12.0}) {
      const double base = dbToAmp(-50.0);
      auto x = noise(62.0, base, 41);
      std::vector<int64_t> starts;
      for (int k = 0; k < 40; ++k) starts.push_back(int64_t((1.5 + 0.5 * k) * kSr));
      std::vector<float> play(x.size(), 0.0f);
      addPlucks(play, starts, 0.1, 98.0, kHarmonic, 0.4, true, 42);
      const size_t mute = size_t(21.5 * kSr), damp = size_t(0.008 * kSr);
      for (size_t i = mute; i < play.size(); ++i)
        play[i] *= i < mute + damp ? float(1.0 - double(i - mute) / double(damp)) : 0.0f;
      for (size_t i = 0; i < x.size(); ++i) x[i] += play[i];
      addTouches(x, 22.0, 61.0, 0.5, base, overDb, thump, 43);
      auto got = run(x, 480);
      const auto inPause = std::count_if(got.begin(), got.end(), [](int64_t g) { return g >= int64_t(21.6 * kSr); });
      EXPECT_EQ(inPause, 0) << (thump ? "thumps" : "ticks") << " +" << overDb << " dB";
      EXPECT_GE(int(got.size() - size_t(inPause)), 39) << "playing phase";
    }
}

// Hiss bursts (+8 / +10 / +12 dB, 5 ms ramps, 0.2 s and 1 s long) on noise only: no onsets.
TEST(OnsetDetector, NoOnsetsFromHissBursts) {
  for (double gainDb : {8.0, 10.0, 12.0})
    for (double dur : {0.2, 1.0}) {
      auto x = noise(40.0, 1.0, 51);
      applyBursts(x, 6e-4, gainDb, 0.005, dur, 52);
      EXPECT_TRUE(run(x, 480).empty()) << "+" << gainDb << " dB bursts of " << dur << " s";
    }
}
