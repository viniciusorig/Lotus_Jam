#include "dsp/tuner.h"

#include <algorithm>
#include <cmath>

namespace {
// Readings must come from a clearly periodic window (YIN probability = 1 - d').
constexpr double kMinProbability = 0.8;

// Second half of the window at least 15 dB (power x30) above the first: onset transient.
constexpr double kOnsetRatio = 30.0;

TunerReading unvoiced() { return {false, -1, 0.0, 0.0, 0.0}; }

double median3(double a, double b, double c) {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}
}  // namespace

Tuner::Tuner(double sampleRate, const Instrument& instrument, const Tuning& tuning, double a4,
             double minRms)
    : yin_(sampleRate, instrument.minHz, instrument.maxHz), minHz_(instrument.minHz),
      maxHz_(instrument.maxHz), a4_(a4), minRms_(minRms), tuning_(tuning),
      hop_(size_t(std::max(1.0, std::floor(sampleRate / 20.0)))),
      ring_(yin_.requiredSamples(), 0.0f), linear_(yin_.requiredSamples(), 0.0f) {}

void Tuner::feed(const float* x, size_t n) {
    const size_t N = ring_.size();
    for (size_t i = 0; i < n; ++i) {
        ring_[writePos_] = x[i];
        writePos_ = writePos_ + 1 == N ? 0 : writePos_ + 1;
        if (filled_ < N) ++filled_;
        ++sinceLast_;
        // First analysis as soon as the window is full, then one every hop.
        if (filled_ == N && (!primed_ || sinceLast_ >= hop_)) {
            primed_ = true;
            sinceLast_ = 0;
            analyse();
        }
    }
}

bool Tuner::poll(TunerReading& out) {
    if (qSize_ == 0) return false;
    out = queue_[qHead_];
    qHead_ = (qHead_ + 1) % kQueue;
    --qSize_;
    return true;
}

void Tuner::push(const TunerReading& r) {
    if (qSize_ == kQueue) {  // consumer is behind: drop the oldest reading
        qHead_ = (qHead_ + 1) % kQueue;
        --qSize_;
    }
    queue_[(qHead_ + qSize_) % kQueue] = r;
    ++qSize_;
}

void Tuner::analyse() {
    // Unroll the circular window, oldest sample first (writePos_ is the oldest).
    const size_t N = ring_.size();
    std::copy(ring_.begin() + std::ptrdiff_t(writePos_), ring_.end(), linear_.begin());
    std::copy(ring_.begin(), ring_.begin() + std::ptrdiff_t(writePos_),
              linear_.begin() + std::ptrdiff_t(N - writePos_));

    // Level gate: silence, noise and mains hum never become a reading. Also skip a window that
    // starts quiet and ends loud (a pluck in progress): its pitch estimate is unreliable and
    // often an octave off (seen on a real bass: 33 Hz instead of 66 Hz at note onsets).
    double sqA = 0.0, sqB = 0.0;
    for (size_t i = 0; i < N / 2; ++i) sqA += double(linear_[i]) * double(linear_[i]);
    for (size_t i = N / 2; i < N; ++i) sqB += double(linear_[i]) * double(linear_[i]);
    const bool quiet = std::sqrt((sqA + sqB) / double(N)) < minRms_;
    const bool onset = sqB > kOnsetRatio * sqA;
    if (quiet || onset) {
        prevQuiet_ = true;
        historyCount_ = 0;
        confirmed_ = false;
        candidateHz_ = 0.0;
        push(unvoiced());
        return;
    }

    PitchResult p = yin_.process(linear_.data());
    // Never hand hz <= 0 / NaN to nearestString (it would silently answer string 0).
    bool ok = p.hz > 0.0 && std::isfinite(p.hz) && p.hz >= minHz_ && p.hz <= maxHz_ &&
              p.probability >= kMinProbability;
    if (!ok) {
        historyCount_ = 0;  // a gap in the note restarts the smoothing
        confirmed_ = false;
        candidateHz_ = 0.0;
        push(unvoiced());
        return;
    }
    if (!confirmed_ && !prevQuiet_) confirmed_ = true;  // steady signal: no extra latency
    prevQuiet_ = false;
    if (!confirmed_) {
        if (candidateHz_ > 0.0 && std::fabs(1200.0 * std::log2(p.hz / candidateHz_)) < 50.0) {
            confirmed_ = true;
        } else {
            candidateHz_ = p.hz;
            push(unvoiced());
            return;
        }
    }

    history_[historyPos_] = p.hz;
    historyPos_ = (historyPos_ + 1) % history_.size();
    if (historyCount_ < history_.size()) ++historyCount_;
    // Median of the last 3 voiced pitches; with fewer, the latest one (no added latency).
    double hz = historyCount_ == 3 ? median3(history_[0], history_[1], history_[2]) : p.hz;

    StringMatch m = nearestString(tuning_, hz, a4_);
    push({true, m.stringIndex, hz, m.targetHz, m.cents});
}
