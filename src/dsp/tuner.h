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
    int midi = -1;  // chromatic mode: the nearest note (MIDI number); -1 in string mode
};

// Tuner service for the analysis thread (single-threaded). Keeps a sliding window of the
// last Yin::requiredSamples() input samples and analyses it every 50 ms of input
// (>= 20 readings per second), starting as soon as the window is first full.
// With a tuning that has no strings ("Cromático") every note is reported against the nearest
// semitone (stringIndex = -1, midi = note) and the range extends up to 3.5x the instrument's.
// Readings that are unvoiced, outside [minHz, maxHz], below probability 0.8, or from a window
// whose RMS is below minRms (silence, mains hum, noise) are reported as voiced=false. A pitch
// far from every string (a slack string, or the wrong tuning selected) is NOT rejected: it
// is reported against the nearest string with its large cents error, which is exactly what
// the player needs to tune it. Up to 16 readings are queued (oldest dropped). The constructor
// allocates; feed()/poll() do not.
class Tuner {
public:
    // Window RMS (in the units of the samples given to feed()) below which nothing is reported:
    // -38 dBFS, well above the PCM2902 hum/noise floor (about -48 dBFS or lower) and well below
    // a played note (about -15 to -25 dBFS). Scale it with any gain applied before feed().
    static constexpr double kDefaultMinRms = 0.0125;

    Tuner(double sampleRate, const Instrument& instrument, const Tuning& tuning, double a4,
          double minRms = kDefaultMinRms);

    void feed(const float* x, size_t n);
    bool poll(TunerReading& out);

private:
    void analyse();
    void push(const TunerReading& r);

    Yin yin_;
    double minHz_, maxHz_, a4_, minRms_;
    bool chromatic_;
    Tuning tuning_;
    size_t hop_;

    std::vector<float> ring_;    // circular sliding window, size N = requiredSamples
    std::vector<float> linear_;  // unrolled copy handed to Yin
    size_t writePos_ = 0, filled_ = 0, sinceLast_ = 0;
    bool primed_ = false;

    // The first voiced reading right after a quiet window must be confirmed by the next one (within 50 cents)
    // before it is shown: single wild readings at pluck onsets are dropped.
    bool confirmed_ = false;
    bool prevQuiet_ = false;  // the previous window was below the level gate / an onset
    double candidateHz_ = 0.0;

    std::array<double, 3> history_{};  // last voiced raw pitches (median of 3)
    size_t historyCount_ = 0, historyPos_ = 0;

    static constexpr size_t kQueue = 16;
    std::array<TunerReading, kQueue> queue_{};
    size_t qHead_ = 0, qSize_ = 0;
};
