#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Streaming onset detector for the analysis thread (single-threaded). C++ port of the
// prototype `Detector`: 5 ms RMS window, 1 ms hop, adaptive threshold, >6 dB rise within
// 15 ms, 120 ms refractory, walk-back to the start of the rise. Decisions (threshold, rise)
// use a 36-step (36 ms) peak-hold of the 5 ms RMS, flat on low notes down to ~30 Hz; the
// walk-back uses the raw 5 ms RMS. Threshold = max(5 x noise floor, 0.12 x p99 of the held
// envelope), both over the last 10 s, where the noise floor is p3 of the raw 5 ms RMS. While
// the player plays non-stop the window holds no silence and p3 becomes the inter-note level (it
// climbs there in one jump for loud notes, in small steps for weak notes or over hum), so while
// there is evidence of playing (3 onsets within 3 s, kept until 3 s without any onset) the floor
// term is capped at 0.25 x p99 but never below 5 x the floor heard in the last stationary window
// (silence, noise or hum; judged on the held envelope, which hum does not ripple). Without that
// evidence it is the plain 5 x p3 (see onset.cpp, kGateRatio).
// Known limits:
//  - Re-plucks over an undecayed tail: a note is missed when the combined level does not rise
//    > 6 dB, e.g. a same-pitch note summing with ringing tails at a random phase (up to ~17% of
//    such notes in overlap/let-ring playing with a pure tone, ~6% with harmonic notes) or a
//    re-pluck after a mute shorter than ~20 ms; those detected are reported 2-4 ms late on
//    average (p95 up to ~19 ms for harmonic notes below 60 Hz), because the walk-back stops on
//    the ripple of the 5 ms RMS.
//  - Notes more than ~18 dB below the loudest of the last 10 s are under 0.12 x p99: after a
//    drop of ~20 dB in playing level there is a blind spot of up to ~10 s, which outlasts the 3 s
//    release of the playing evidence: a soft continuous section after a >= 20 dB drop collapses
//    as at 0be7735 (a -12 dB drop recovers in 1-2 s).
//  - Notes less than ~14 dB above the noise floor (5 x p3) are not detected, in every version
//    of this detector: e.g. 164.8 Hz harmonic and 55 Hz sine notes at -40 dBFS peak over -55 dBFS
//    noise, or -34 dBFS over -45 dBFS 60 Hz hum (whose 5 ms RMS dips only ~3 dB, so 5 x p3 sits
//    ~11 dB above the hum) are missed even with silence in the window; the tests use -37 / -31.
//  - Over hum, the walk-back stops on the hum ripple: pure tones at <= -31 dBFS over hum have a
//    median timing error of 6-16 ms (not asserted by the tests).
//  - A decaying low note beating against mains hum (e.g. G1/G#1 against 60 Hz with harmonics)
//    can produce false onsets; this predates the gate and the gate does not add to it.
//  - If the floor rises while playing non-stop (a fan, a gain change), the older floor is kept
//    until a stationary window or 10 s without onsets: detection holds, but after the playing
//    stops low events over the new floor can fire, and each false onset re-arms the playing
//    evidence. With a fan +12 dB and handling at >= ~1 low event per second the lowered floor
//    term can survive ~60-90 s, with 41-66 false onsets per 120 s (0-3 at 0.5 events/s; a +12 dB
//    gain step behaves as 0be7735).
//  - A hum that rises while playing freezes the floor for as long as sparse playing continues
//    (gaps < 10 s): low notes beating against that hum (e.g. G1/G#1) then give ~10 more false
//    onsets per 45 notes than 0be7735 for the whole take, not only after the playing stops.
//  - At the edge of the 5 x floor guard (notes < ~14 dB above it, e.g. -40 dBFS peaks over
//    -55 dBFS noise) detection is patchy from the start; a long run of misses (3 s) drops the
//    playing evidence and the take can then collapse as at 0be7735.
//  - A session that starts in the middle of non-stop playing has no silence to take the floor
//    from and collapses as with a plain 5 x p3 floor: the app must give a count-in / silence.
//  - Follow-up idea (deferred): measure the floor in the gaps between detected onsets instead
//    of from p3, which would remove the playing-evidence heuristic and its trade-offs.
// No onsets are reported in the first second (warm-up). Onsets are absolute sample indices
// since construction and do not depend on how the stream is chunked. The constructor allocates;
// feed() does not (apart from growth of the caller's `onsetsOut`).
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
    // Floor (p3) of the last stationary window (< 0 until the first update), the steps of the
    // last 3 onsets and the playing evidence derived from them. See kGateRatio in onset.cpp.
    double anchor_ = -1.0;
    int64_t recent_[3] = {-1000000, -1000000, -1000000};
    int recentPos_ = 0;
    bool playing_ = false;
};
