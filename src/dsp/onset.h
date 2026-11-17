#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Streaming onset detector for the analysis thread (single-threaded). C++ port of the
// prototype `Detector`: 5 ms RMS window, 1 ms hop, adaptive threshold, >6 dB rise within
// 15 ms, 120 ms refractory, walk-back to the start of the rise. Decisions (threshold, rise)
// use a 36-step (36 ms) peak-hold of the 5 ms RMS, flat on low notes down to ~30 Hz; the
// walk-back uses the raw 5 ms RMS. Threshold = max(5 x noise floor, 0.12 x p99 of the held
// envelope), both over the last 10 s, where the noise floor is p3 of the raw 5 ms RMS. When
// the window is not stationary noise (p99 > 8 dB above p3) and p3 jumped more than 6 dB above
// the last floor seen (continuous playing: the last silence has left the window, so p3 is the
// inter-note level), the floor term is capped at 0.25 x p99 but never below 5 x that remembered
// floor, so the inter-note level of a long take is not mistaken for noise while hum, bursts,
// thumps and ticks still face the full 5 x noise-floor guard (see onset.cpp, kGateRatio).
// Known limits:
//  - Re-plucks over an undecayed tail: a note is missed when the combined level does not rise
//    > 6 dB, e.g. a same-pitch note summing with ringing tails at a random phase (up to ~17% of
//    such notes in overlap/let-ring playing with a pure tone, ~6% with harmonic notes) or a
//    re-pluck after a mute shorter than ~20 ms; those detected are reported 2-4 ms late on
//    average (p95 up to ~19 ms for harmonic notes below 60 Hz), because the walk-back stops on
//    the ripple of the 5 ms RMS.
//  - Notes more than ~18 dB below the loudest of the last 10 s are under 0.12 x p99: after a
//    drop of ~20 dB in playing level there is a blind spot of up to ~10 s.
//  - Notes less than ~14 dB above the noise floor (5 x p3) are not detected.
//  - A decaying low note beating against mains hum (e.g. G1/G#1 against 60 Hz with harmonics)
//    can produce false onsets; this predates the gate and the gate does not add to it.
//  - If the noise floor rises > 6 dB during non-stop playing, the older, lower floor is kept
//    until a pause; if the detector starts in the middle of non-stop playing (no silence seen),
//    the take can collapse as with a plain 5 x p3 floor.
// No onsets are reported in the first second (warm-up). Onsets are absolute sample indices
// since construction and do not depend on how the stream is chunked. The constructor allocates; feed() does not
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
    // Remembered noise floor (see kFloorMemRiseDb in onset.cpp); < 0 until the first update.
    double floorMem_ = -1.0;
};
