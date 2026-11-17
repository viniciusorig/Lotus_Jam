#include "dsp/yin.h"

#include <cmath>

namespace {
// Absolute tolerance used by the octave-safe minimum choice (see process()).
constexpr double kOctaveTolerance = 0.03;
// Below this per-sample energy the window is treated as silence.
constexpr double kSilenceEnergy = 1e-10;
}  // namespace

Yin::Yin(double sampleRate, double minHz, double maxHz, double threshold)
    : sampleRate_(sampleRate), minHz_(minHz), maxHz_(maxHz), threshold_(threshold),
      tauMax_(size_t(std::ceil(sampleRate / minHz))), diff_(tauMax_ + 1), cmndf_(tauMax_ + 1) {}

PitchResult Yin::process(const float* x) const {
    const size_t W = kWindow;

    double energy = 0;
    for (size_t j = 0; j < W; ++j) energy += double(x[j]) * x[j];
    if (energy / double(W) < kSilenceEnergy) return {0.0, 0.0};

    // Step 2: difference function d(tau) = sum_j (x[j] - x[j + tau])^2, j in [0, W).
    diff_[0] = 0;
    for (size_t tau = 1; tau <= tauMax_; ++tau) {
        double s = 0;
        const float* y = x + tau;
        for (size_t j = 0; j < W; ++j) {
            double d = double(x[j]) - double(y[j]);
            s += d * d;
        }
        diff_[tau] = s;
    }

    // Step 3: cumulative-mean-normalised difference d'(tau).
    cmndf_[0] = 1;
    double running = 0;
    for (size_t tau = 1; tau <= tauMax_; ++tau) {
        running += diff_[tau];
        cmndf_[tau] = running > 0 ? diff_[tau] * double(tau) / running : 1.0;
    }

    // Step 4, octave-safe variant. Plain YIN takes the first dip below the threshold, which
    // picks the half period when the 2nd harmonic dominates a weak fundamental (bass E1 with
    // fundamental 0.2 / 2nd harmonic 1.0 gives d'(T/2) ~= 0.077 < 0.12 while d'(T) ~= 0).
    // Rule used here:
    //   1. Candidates are the local minima of d' in [2, tauMax - 1] below the threshold.
    //      The search starts at lag 2 (not at sampleRate/maxHz) so that a pitch above maxHz
    //      is found at its true period and rejected, instead of being reported at a
    //      sub-multiple frequency that happens to fall inside the range.
    //   2. best = the smallest candidate value (the deepest dip, usually T, 2T, ...).
    //   3. Choose the SHORTEST-lag candidate whose value is within kOctaveTolerance of best.
    //      Shortest avoids reporting 2T/3T (sub-octaves, whose dips are as deep as T's);
    //      the tolerance rejects the shallower T/2 dip of a weak fundamental.
    double best = 2.0;
    for (size_t tau = 2; tau < tauMax_; ++tau)
        if (cmndf_[tau] < threshold_ && cmndf_[tau] < cmndf_[tau - 1] &&
            cmndf_[tau] <= cmndf_[tau + 1] && cmndf_[tau] < best)
            best = cmndf_[tau];
    if (best >= threshold_) return {0.0, 0.0};

    size_t chosen = 0;
    for (size_t tau = 2; tau < tauMax_; ++tau)
        if (cmndf_[tau] < cmndf_[tau - 1] && cmndf_[tau] <= cmndf_[tau + 1] &&
            cmndf_[tau] <= best + kOctaveTolerance && cmndf_[tau] < threshold_) {
            chosen = tau;
            break;
        }

    // Step 5: parabolic interpolation around the chosen lag. Done on the raw difference
    // d(tau), which is close to a parabola near a periodic minimum; d' carries the tau /
    // cumulative-mean factor that biases the vertex.
    double a = diff_[chosen - 1], b = diff_[chosen], c = diff_[chosen + 1];
    double denom = a - 2 * b + c;
    double shift = denom > 0 ? 0.5 * (a - c) / denom : 0.0;
    if (shift > 0.5) shift = 0.5;
    if (shift < -0.5) shift = -0.5;
    double period = double(chosen) + shift;

    double probability = 1.0 - cmndf_[chosen];
    double hz = sampleRate_ / period;
    if (!(hz >= minHz_ && hz <= maxHz_)) return {0.0, probability};
    return {hz, probability};
}
