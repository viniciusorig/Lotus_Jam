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

// Plucked notes of fundamental hz with partials `harm` (partial h decays h/2 times faster than
// the fundamental), 4 ms linear attack, exponential decay tau, a random start phase per partial
// and note. replace = same string: the previous note is damped linearly over 8 ms from the next
// pluck; otherwise notes ring on and sum (different strings / let-ring).
void addPlucks(std::vector<float>& x, const std::vector<int64_t>& starts, double peak, double hz,
               const std::vector<double>& harm, double tau, bool replace, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> ph(0.0, 2 * kPi);
  const int64_t attack = int64_t(0.004 * kSr), damp = int64_t(0.008 * kSr);
  std::vector<double> phs(harm.size());
  for (size_t n = 0; n < starts.size(); ++n) {
    const int64_t s = starts[n];
    const bool cut = replace && n + 1 < starts.size();
    // Without a cut a note rings for 12 tau (-104 dB) and is then dropped, to bound run time.
    const int64_t end = cut ? starts[n + 1] + damp : s + int64_t(12.0 * tau * kSr);
    for (auto& p : phs) p = ph(rng);
    for (int64_t i = 0; s + i < end && s + i < int64_t(x.size()); ++i) {
      double t = double(i) / kSr;
      double env = (i < attack ? double(i) / double(attack) : 1.0) * std::exp(-t / tau);
      if (cut && s + i >= starts[n + 1]) env *= 1.0 - double(s + i - starts[n + 1]) / double(damp);
      double v = 0;
      for (size_t h = 0; h < harm.size(); ++h)
        v += harm[h] * std::exp(-t * double(h) / (2 * tau)) * std::sin(2 * kPi * hz * double(h + 1) * t + phs[h]);
      x[size_t(s + i)] += float(peak * env * v);
    }
  }
}

const std::vector<double> kSine = {1.0};
const std::vector<double> kHarmonic = {1.0, 0.5, 0.3, 0.2, 0.1};

// Bounds for notes played over a still-ringing tail (long takes, LowNotes `_harm`). They are
// measured limits of the method, not slack; isolated notes keep the 3 ms bound of the tests above.
// Each bound is the WORST single 60 s take over the full f0 x timbre x mode x peak matrix
// (tau 0.4 s, 0.5 s spacing, 10 seeds), since each test checks one take.
//
// Timing: on a tail only ~11 dB down, the walk-back over the raw 5 ms RMS stops early on the
// ripple of the 5 ms RMS (f0 below ~110 Hz) and on the dip that damping plus destructive
// interference makes in the first ms (masking by the ringing tail), so re-plucks are reported
// 2-4 ms late on average, with a spread that grows as f0 falls and when partials ring on.
// Tolerance = smallest window holding >= 95% of the notes in every take (worst p95 |err|):
// 10 ms for f0 >= 98 Hz, 12 ms for 55-98 Hz and 41.2 Hz sine, 16 ms for harmonic notes below
// 60 Hz. Median |err| <= 5 ms in every take (<= 3 ms in all but harmonic below 60 Hz).
constexpr double kTailTolMs = 10.0;         // f0 >= 98 Hz
constexpr double kTailTolLowMs = 12.0;      // 55-98 Hz, and 41.2 Hz pure sine
constexpr double kTailTolHarmLowMs = 16.0;  // harmonic timbre below 60 Hz
constexpr double kTailInTolPct = 95.0;
constexpr double kTailMedianMs = 5.0;
//
// Detection: a re-pluck is seen only if the level rises > 6 dB within 15 ms (the rise
// criterion). In overlap mode with a pure sine, the new note adds to the ringing tails of the
// same frequency with a random phase (same-pitch phasor sum), and ~5-14% of notes rise < 6 dB
// in TRUE amplitude (worst take 85.8% overall, worst 10 s segment 15/20). Harmonic notes below
// 60 Hz lose some re-plucks the same way while the tail is still loud (worst take 95.0%, worst
// segment 17/20), and harmonic overlap at 98 Hz rarely one (worst segment 19/20). Everything
// else: 100% detected. 0 false onsets everywhere.
constexpr int kDetPctDefault = 95;           // per 10 s segment (19 of 20)
constexpr int kDetPctOverlapSineTotal = 85;  // same-pitch phasor sum, overall
constexpr int kDetPctOverlapSineSeg = 75;    // ... per segment
constexpr int kDetPctHarmLowTotal = 95;      // harmonic below 60 Hz, overall
constexpr int kDetPctHarmLowSeg = 85;        // ... per segment

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
  auto x = noise(1.5 + p.spacing * kNotes + 1.0, 20.0 / 32768.0, 21);
  if (p.harmonic)
    addPlucks(x, starts, 0.3, p.hz, kHarmonic, p.tau, false, 22);
  else
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
  // combined level start rising a few ms late, so allow 8 ms there. The harmonic case rings on
  // (notes 0.5 s apart over a tau 0.4 s tail), so it gets the tail tolerances (see kTail*).
  if (p.harmonic) {
    EXPECT_LE(worst, kTailTolHarmLowMs);
    std::vector<double> errs;
    for (int64_t g : got) {
      double best = 1e9;
      for (int64_t s : starts) best = std::min(best, std::abs(double(g - s)) / kSr * 1000.0);
      errs.push_back(best);
    }
    EXPECT_LE(medianOf(errs), kTailMedianMs);
  } else {
    EXPECT_LT(worst, lenient ? 8.0 : 3.0);
  }
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
  const double tol = harmLow ? kTailTolHarmLowMs : (p.hz >= 98.0 ? kTailTolMs : kTailTolLowMs);
  const int segPct = overlapSine ? kDetPctOverlapSineSeg : harmLow ? kDetPctHarmLowSeg : kDetPctDefault;
  const int totPct = overlapSine ? kDetPctOverlapSineTotal : harmLow ? kDetPctHarmLowTotal : kDetPctDefault;
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

// Representative subset (the full f0 x timbre x mode x peak matrix runs in a throwaway program,
// see the Task 5d report): every f0, both timbres, both modes and both peaks appear.
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
