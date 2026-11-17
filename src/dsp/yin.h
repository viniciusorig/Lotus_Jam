#pragma once
#include <cstddef>
#include <vector>

struct PitchResult {
    double hz;           // 0 = no pitch
    double probability;  // 1 - CMNDF value at the chosen lag (0 when nothing usable)
};

// YIN pitch detector (de Cheveigné & Kawahara 2002) in the time domain.
// Not thread-safe: process() uses an internal preallocated scratch buffer.
class Yin {
public:
    static constexpr size_t kWindow = 2048;  // W, integration window

    Yin(double sampleRate, double minHz, double maxHz, double threshold = 0.12);

    // W + tauMax, with tauMax = ceil(sampleRate / minHz).
    size_t requiredSamples() const { return kWindow + tauMax_; }

    // Analyses exactly requiredSamples() samples starting at x.
    PitchResult process(const float* x) const;

private:
    double sampleRate_, minHz_, maxHz_, threshold_;
    size_t tauMax_;
    mutable std::vector<double> diff_;   // d(tau), tau in [0, tauMax]
    mutable std::vector<double> cmndf_;  // d'(tau)
};
