#include "dsp/tuner.h"

#include <algorithm>
#include <cmath>

namespace {
// Readings must come from a clearly periodic window (YIN probability = 1 - d').
constexpr double kMinProbability = 0.8;
// Capture range: a pitch more than a semitone away from every string of the selected tuning
// is not reported as a string. This is what keeps mains hum (60 Hz in Brazil: A1 +150 cents
// on a bass) and other non-string pitches showing "no signal" instead of a bogus reading.
constexpr double kCaptureCents = 100.0;

TunerReading unvoiced() { return {false, -1, 0.0, 0.0, 0.0}; }

double median3(double a, double b, double c) {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}
}  // namespace

Tuner::Tuner(double sampleRate, const Instrument& instrument, const Tuning& tuning, double a4)
    : yin_(sampleRate, instrument.minHz, instrument.maxHz), minHz_(instrument.minHz),
      maxHz_(instrument.maxHz), a4_(a4), tuning_(tuning),
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

    PitchResult p = yin_.process(linear_.data());
    // Never hand hz <= 0 / NaN to nearestString (it would silently answer string 0).
    bool ok = p.hz > 0.0 && std::isfinite(p.hz) && p.hz >= minHz_ && p.hz <= maxHz_ &&
              p.probability >= kMinProbability;
    if (!ok) {
        historyCount_ = 0;  // a gap in the note restarts the smoothing
        push(unvoiced());
        return;
    }

    history_[historyPos_] = p.hz;
    historyPos_ = (historyPos_ + 1) % history_.size();
    if (historyCount_ < history_.size()) ++historyCount_;
    // Median of the last 3 voiced pitches; with fewer, the latest one (no added latency).
    double hz = historyCount_ == 3 ? median3(history_[0], history_[1], history_[2]) : p.hz;

    StringMatch m = nearestString(tuning_, hz, a4_);
    if (!(std::fabs(m.cents) <= kCaptureCents)) {
        push(unvoiced());
        return;
    }
    push({true, m.stringIndex, hz, m.targetHz, m.cents});
}
