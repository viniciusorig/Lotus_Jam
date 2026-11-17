#pragma once
// Synthetic test signals for the onset detector, shared by tests/onset_test.cpp and the offline
// measurement programs (so a matrix run renders exactly what the tests render). All randomness
// is derived from raw std::mt19937 output (whose sequence the standard fixes) with explicit
// transforms, never from std::*_distribution (implementation-defined algorithms), so the signals
// are the same across standard libraries up to last-bit differences of the libm functions
// (log, cos, sin, exp), which can still move a decision that sits exactly at a threshold.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace onsetsig {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

inline double dbToAmp(double db) { return std::pow(10.0, db / 20.0); }

class Rng {
 public:
  explicit Rng(unsigned seed) : mt_(seed) {}
  // Uniform in (0, 1), never 0 or 1.
  double uniform() { return (double(mt_()) + 0.5) * (1.0 / 4294967296.0); }
  double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
  // Standard normal (Box-Muller, cosine branch only: one mt19937 pair per value).
  double gauss() {
    double u1 = uniform(), u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
  }
  // Exponential with the given rate (events per second).
  double exponential(double rate) { return -std::log(uniform()) / rate; }

 private:
  std::mt19937 mt_;
};

inline std::vector<float> noise(double seconds, double sigma, unsigned seed) {
  Rng rng(seed);
  std::vector<float> x(size_t(seconds * kSr));
  for (auto& v : x) v = float(sigma * rng.gauss());
  return x;
}

const std::vector<double> kSine = {1.0};
const std::vector<double> kHarmonic = {1.0, 0.5, 0.3, 0.2, 0.1};

// Plucked notes of fundamental hz with partials `harm` (partial h decays h/2 times faster than
// the fundamental), 4 ms linear attack, exponential decay tau, a random start phase per partial
// and note. replace = same string: the previous note is damped linearly over 8 ms from the next
// pluck; otherwise notes ring on and sum (different strings / let-ring). Partials are run as
// decaying complex phasors (one multiply per sample) to keep long renders cheap.
inline void addPlucks(std::vector<float>& x, const std::vector<int64_t>& starts, double peak, double hz,
                      const std::vector<double>& harm, double tau, bool replace, unsigned seed) {
  Rng rng(seed);
  const int64_t attack = int64_t(0.004 * kSr), damp = int64_t(0.008 * kSr);
  const size_t nh = harm.size();
  std::vector<double> re(nh), im(nh), cr(nh), ci(nh);
  for (size_t h = 0; h < nh; ++h) {  // per-sample rotation and decay of partial h
    const double w = 2 * kPi * hz * double(h + 1) / kSr, g = std::exp(-double(h) / (2 * tau * kSr));
    cr[h] = g * std::cos(w);
    ci[h] = g * std::sin(w);
  }
  const double gEnv = std::exp(-1.0 / (tau * kSr));
  for (size_t n = 0; n < starts.size(); ++n) {
    const int64_t s = starts[n];
    const bool cut = replace && n + 1 < starts.size();
    // Without a cut a note rings for 12 tau (-104 dB) and is then dropped, to bound run time.
    const int64_t end = cut ? starts[n + 1] + damp : s + int64_t(12.0 * tau * kSr);
    for (size_t h = 0; h < nh; ++h) {
      const double ph = rng.uniform(0.0, 2 * kPi);
      re[h] = harm[h] * std::cos(ph);
      im[h] = harm[h] * std::sin(ph);
    }
    double decay = 1.0;
    for (int64_t i = 0; s + i < end && s + i < int64_t(x.size()); ++i) {
      double env = (i < attack ? double(i) / double(attack) : 1.0) * decay;
      if (cut && s + i >= starts[n + 1]) env *= 1.0 - double(s + i - starts[n + 1]) / double(damp);
      double v = 0;
      for (size_t h = 0; h < nh; ++h) {
        v += im[h];  // = harm * exp(-t h / 2tau) * sin(2 pi f (h+1) t + phase)
        const double r = re[h] * cr[h] - im[h] * ci[h];
        im[h] = re[h] * ci[h] + im[h] * cr[h];
        re[h] = r;
      }
      x[size_t(s + i)] += float(peak * env * v);
      decay *= gEnv;
    }
  }
}

// Mains hum: fundamental f plus 2nd and 3rd harmonics (relative amplitudes h2, h3, fixed
// phases), scaled to rmsDbfs total RMS.
inline void addHum(std::vector<float>& x, double f, double h2, double h3, double rmsDbfs) {
  const double a = dbToAmp(rmsDbfs) * std::sqrt(2.0) / std::sqrt(1.0 + h2 * h2 + h3 * h3);
  for (size_t i = 0; i < x.size(); ++i) {
    const double t = double(i) / kSr;
    x[i] += float(a * (std::sin(2 * kPi * f * t) + h2 * std::sin(2 * kPi * 2 * f * t + 1.0) +
                       h3 * std::sin(2 * kPi * 3 * f * t + 2.0)));
  }
}

// Hand contact in a pause, at Poisson times (rate per second) in [from, to) seconds, peak
// sqrt(2) * baseRms * 10^(overDb/20):
//   thump = body/handling knock, 25 Hz decaying with tau 40 ms (200 ms long);
//   tick  = muted-string touch, 98 Hz, 2 ms attack, tau 30 ms (150 ms long).
inline void addTouches(std::vector<float>& x, double from, double to, double rate, double baseRms, double overDb,
                       bool thump, unsigned seed) {
  Rng rng(seed);
  const double a = baseRms * dbToAmp(overDb) * std::sqrt(2.0);
  for (double t = from + rng.exponential(rate); t < to; t += rng.exponential(rate)) {
    const size_t s = size_t(t * kSr), len = size_t((thump ? 0.2 : 0.15) * kSr);
    for (size_t i = 0; i < len && s + i < x.size(); ++i) {
      const double tt = double(i) / kSr;
      x[s + i] += float(thump ? a * std::exp(-tt / 0.04) * std::sin(2 * kPi * 25 * tt)
                              : a * std::min(1.0, tt / 0.002) * std::exp(-tt / 0.03) * std::sin(2 * kPi * 98 * tt));
    }
  }
}

// Hiss bursts: x (unit-variance noise expected) is scaled to baseRms, and raised by gainDb for
// bursts of `dur` seconds with linear `rise` ramps, separated by uniform 0.5..3 s gaps.
inline void applyBursts(std::vector<float>& x, double baseRms, double gainDb, double rise, double dur, unsigned seed) {
  Rng rng(seed);
  std::vector<double> g(x.size(), 1.0);
  for (double t = 1.5; t < double(x.size()) / kSr - 1.0; t += dur + rng.uniform(0.5, 3.0))
    for (size_t i = size_t(t * kSr); i < size_t((t + dur + rise) * kSr) && i < x.size(); ++i) {
      const double tt = double(i) / kSr - t;
      const double r = std::clamp(tt < rise ? tt / rise : (tt > dur ? 1.0 - (tt - dur) / rise : 1.0), 0.0, 1.0);
      g[i] = 1.0 + r * (dbToAmp(gainDb) - 1.0);
    }
  for (size_t i = 0; i < x.size(); ++i) x[i] *= float(baseRms * g[i]);
}

}  // namespace onsetsig
