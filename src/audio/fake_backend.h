#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "audio/backend.h"

// Test double for IAudioBackend. A driver thread produces 480-frame blocks (10 ms of virtual
// time) paced at `speed` virtual seconds per wall second: for each block it renders the output
// through the RenderFn, synthesises the input (scheduled notes + Gaussian noise), pushes the
// TimePoints and then advances the virtual clock.
//
// Virtual clock: nowSec() starts at Options::startSec and only advances while running (it is
// frozen while stopped, so the value read before start() is the stream origin t0).
// For a stream started at t0, block k (frames [480k, 480k+480)):
//  - output frame f is audible at t0 + f/48000 + outputLatencySec;
//  - produced input frame i was captured at t0 + i/48000 - inputLatencySec. When input is
//    dropped (ring full, or dropInput()), the TimePoints count frames WRITTEN to input()
//    (IAudioBackend contract) and carry the capture time of the produced frame at that
//    boundary, so their time jumps forward by the dropped duration; no TimePoint is pushed for
//    a block of which nothing was written;
//  - after block k is produced, nowSec() = t0 + 480(k+1)/48000.
// Deterministic for a given seed and schedule apart from thread scheduling (with
// `backpressure` the data is fully deterministic).
//
// The driver thread plays the role of the real-time callbacks: it never locks or allocates.
class FakeBackend : public IAudioBackend {
public:
    struct Options {
        double speed = 1.0;  // virtual seconds per wall second
        double inputLatencySec = 0.02;
        double outputLatencySec = 0.02;
        double noiseSigma = 0.0;
        uint64_t seed = 1;
        double startSec = 1000.0;  // initial virtual time (not 0, to catch origin assumptions)
        // When true the driver waits (instead of dropping input and counting an xrun) while
        // the input ring has no room for a block; the virtual clock simply waits too.
        bool backpressure = false;
        size_t inputCapacity = size_t(1) << 17;  // samples (~2.7 s), power of two
        size_t captureOutputFrames = 0;  // stereo frames of output kept for capturedOutput()
    };

    static constexpr size_t kBlock = 480;
    static constexpr double kSampleRate = 48000.0;

    explicit FakeBackend(const Options& o);
    ~FakeBackend() override;

    bool start(std::string& err) override;
    void stop() override;
    void setRenderer(RenderFn fn) override;
    SpscRing<float>& input() override { return input_; }
    SpscRing<TimePoint>& inputTimes() override { return inTimes_; }
    SpscRing<TimePoint>& outputTimes() override { return outTimes_; }
    uint32_t xruns() const override { return xruns_.load(std::memory_order_relaxed); }
    uint64_t inputDroppedFrames() const override { return dropped_.load(std::memory_order_acquire); }
    bool failed(std::string& err) const override;
    double nowSec() const override { return now_.load(std::memory_order_acquire); }

    // Adds a decaying sine (4 ms linear attack, tau 0.25 s, 3 s long) starting at capture time
    // tSec (virtual clock). Single producer: call from one thread (any time). At most 1024
    // pending notes; extra notes are ignored.
    void scheduleNote(double tSec, double hz, double amp);
    // Test hook: input produced from capture time atSec on is discarded as if the input ring
    // were full, for `frames` frames extended to the end of the block they end in (like a full
    // ring, which keeps the head of a block and drops the rest). May be called several times
    // (up to kMaxDrops windows; more are ignored). Call only while stopped; the windows apply
    // to the next start() only (a window before that stream's start is ignored).
    void dropInput(double atSec, size_t frames) {
        if (dropCount_ < kMaxDrops && frames > 0) dropReq_[dropCount_++] = {atSec, frames};
    }
    static constexpr size_t kMaxDrops = 1024;
    // The device "disappears": the driver stops delivering data and failed() becomes true
    // until the next start().
    void simulateFailure() { failed_.store(true, std::memory_order_release); }
    // Interleaved stereo output captured since the last start() (at most captureOutputFrames).
    // Only valid while stopped.
    const std::vector<float>& capturedOutput() const { return captured_; }
    double streamStartSec() const { return t0_; }

private:
    struct Note {
        double t, hz, amp;
    };
    void run();
    void renderInput(float* x, size_t n, int64_t firstFrame);
    double gaussian();

    Options o_;
    SpscRing<float> input_;
    SpscRing<TimePoint> inTimes_{1024}, outTimes_{1024};
    SpscRing<Note> pending_{1024};
    static constexpr size_t kVoices = 1024;
    std::array<Note, kVoices> voices_{};
    std::array<bool, kVoices> used_{};

    RenderFn render_;
    std::thread thread_;
    std::atomic<bool> stopReq_{false};
    std::atomic<bool> failed_{false};
    std::atomic<uint32_t> xruns_{0};
    std::atomic<uint64_t> dropped_{0};
    struct DropReq {
        double atSec;
        size_t frames;
    };
    struct DropWin {
        int64_t begin, end;  // produced frames [begin, end), end on a block boundary
    };
    std::array<DropReq, kMaxDrops> dropReq_{};
    std::array<DropWin, kMaxDrops> dropWin_{};
    size_t dropCount_ = 0, winCount_ = 0;  // set while stopped, read by the driver
    std::atomic<double> now_;
    double t0_ = 0.0;  // written by start() before the driver runs
    uint64_t rng_;
    std::vector<float> out_, in_, captured_;
    bool running_ = false;
};
