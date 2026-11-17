#pragma once
#include <array>
#include <cstddef>
#include <vector>

#include "dsp/notes.h"
#include "dsp/yin.h"

struct TunerReading {
    bool voiced;
    int stringIndex;  // index into Tuning::strings; -1 when not voiced (other fields 0)
    double hz, targetHz, cents;
};

// Tuner service for the analysis thread (single-threaded). Keeps a sliding window of the
// last Yin::requiredSamples() input samples and analyses it every 50 ms of input
// (>= 20 readings per second), starting as soon as the window is first full.
// Readings that are unvoiced, outside [minHz, maxHz], below probability 0.8 or more than
// 100 cents from every string are reported as voiced=false. Up to 16 readings are queued
// (oldest dropped). The constructor allocates; feed()/poll() do not.
class Tuner {
public:
    Tuner(double sampleRate, const Instrument& instrument, const Tuning& tuning, double a4);

    void feed(const float* x, size_t n);
    bool poll(TunerReading& out);

private:
    void analyse();
    void push(const TunerReading& r);

    Yin yin_;
    double minHz_, maxHz_, a4_;
    Tuning tuning_;
    size_t hop_;

    std::vector<float> ring_;    // circular sliding window, size N = requiredSamples
    std::vector<float> linear_;  // unrolled copy handed to Yin
    size_t writePos_ = 0, filled_ = 0, sinceLast_ = 0;
    bool primed_ = false;

    std::array<double, 3> history_{};  // last voiced raw pitches (median of 3)
    size_t historyCount_ = 0, historyPos_ = 0;

    static constexpr size_t kQueue = 16;
    std::array<TunerReading, kQueue> queue_{};
    size_t qHead_ = 0, qSize_ = 0;
};
