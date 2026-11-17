#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct BeatMark {
    int64_t beat;   // 0-based; beats 0..countIn-1 are the count-in, the scored grid starts at countIn
    int64_t frame;  // absolute output frame where the beat's click starts
};

// Start cue + uniform metronome clicks, for the PipeWire output callback. Port of the
// prototype `som_partida` / `Motor._tocar`. All waveforms are pre-rendered in the
// constructor; render/beatsIn/firstBeatFrame/framesPerBeat are noexcept, lock-free and
// allocation-free. render() is a pure function of (firstFrame, frames): any block
// partition yields identical samples. Beat n starts at round(firstBeatFrame + n*framesPerBeat).
// bpm is clamped to [30, 240]; countIn < 0 is treated as 0. At 240 BPM beats are 250 ms apart
// and a click lasts 30 ms, so clicks never overlap each other (nor the cue, which ends
// 0.7 s before the first beat).
class ClickScheduler {
public:
    ClickScheduler(double sampleRate, double bpm, int countIn);

    // Overwrites `stereoInterleaved` (frames*2 floats, L=R) with samples in [-1, 1].
    // firstFrame: absolute output frame index of the first frame of the block (may be negative).
    void render(float* stereoInterleaved, size_t frames, int64_t firstFrame) noexcept;
    // Beats whose start frame lies in [firstFrame, firstFrame+frames); returns how many were
    // written to `out` (at most maxOut).
    size_t beatsIn(int64_t firstFrame, size_t frames, BeatMark* out, size_t maxOut) const noexcept;
    int64_t firstBeatFrame() const noexcept { return firstBeat_; }
    double framesPerBeat() const noexcept { return fpb_; }
    int countIn() const noexcept { return countIn_; }

private:
    int64_t beatStart(int64_t n) const noexcept;
    int64_t beatBelow(int64_t frame) const noexcept;  // largest n with beatStart(n) <= frame (may be < 0)

    double fpb_;
    int countIn_;
    int64_t firstBeat_;
    std::vector<float> cue_, click_;
};
