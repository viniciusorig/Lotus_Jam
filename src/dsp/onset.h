#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Streaming onset detector for the analysis thread (single-threaded). C++ port of the
// prototype `Detector`: 5 ms RMS window, 1 ms hop, adaptive threshold, >6 dB rise within
// 15 ms, 120 ms refractory, walk-back to the start of the rise. Decisions (threshold, rise)
// use a 36-step (36 ms) peak-hold of the 5 ms RMS, flat on low notes down to ~30 Hz; the
// walk-back uses the raw 5 ms RMS. Threshold = max(5 x noise floor, 0.12 x p99 of the held
// envelope), both over the last 10 s, where the noise floor is p3 of the raw 5 ms RMS; when
// that window holds note energy well above anything stationary noise produces (continuous
// playing, no real silence) the floor term is capped at 0.25 x p99 so that the inter-note
// level of a long take cannot be mistaken for noise.
// Known limits (re-plucks over an undecayed tail): a note is missed when the combined level does
// not rise > 6 dB, e.g. a same-pitch note summing with ringing tails at a random phase (~5-14% of
// such notes in overlap/let-ring playing) or a re-pluck after a mute shorter than ~20 ms; those
// detected are reported 2-4 ms late on average (up to ~16 ms for harmonic notes below 60 Hz),
// because the walk-back stops on the ripple of the 5 ms RMS. No onsets are reported in the first
// second (warm-up). Onsets are absolute sample indices since construction and do
// not depend on how the stream is chunked. The constructor allocates; feed() does not
// (apart from growth of the caller's `onsetsOut`).
class OnsetDetector {
public:
    explicit OnsetDetector(double sampleRate = 48000);

    // Appends detected onsets (absolute sample index) to onsetsOut.
    void feed(const float* x, size_t n, std::vector<int64_t>& onsetsOut);

private:
    void step(std::vector<int64_t>& out);
    void updateThreshold();
    double percentile(const float* data, size_t n, double p);

    // warmup_ and refractory_ are in steps of 1 ms regardless of sampleRate.
    size_t win_, hop_, warmup_ = 1000;
    int64_t refractory_ = 120;

    // DC removal (per-sample one-pole, initialised from the first sample).
    bool dcInit_ = false;
    double dc_ = 0.0, dcAlpha_;

    // Running sum of squares over the last win_ samples.
    std::vector<double> sq_;
    size_t sqPos_ = 0;
    double sum_ = 0.0;
    size_t untilStep_;  // samples until the next hop

    // History of envelope (long) and dB (short), indexed by absolute step number.
    std::vector<float> env_, rawHist_, db_, raw_, holdDb_, lin_, scratch_;
    int64_t steps_ = 0;  // number of steps produced so far
    int64_t lastOnset_ = -10000;
    double thr_ = 0.0;
    bool haveThr_ = false;
};
