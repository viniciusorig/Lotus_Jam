#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/clock_map.h"
#include "core/ring_buffer.h"

// Fills `stereo` (frames*2 interleaved floats, L R) for the output frames
// [firstFrame, firstFrame + frames). Called from the backend's real-time output callback:
// it must not lock, allocate or do I/O. Same frame domain as ClickScheduler::render.
using RenderFn = std::function<void(float* stereo, size_t frames, int64_t firstFrame)>;

// Abstract full-duplex audio device (PipeWire in production, FakeBackend in tests).
//
// Time model: every time value is in seconds on the BACKEND's clock, read with nowSec()
// (PipeWireBackend: CLOCK_MONOTONIC; FakeBackend: a virtual clock). Nothing that consumes a
// backend should use any other clock for timing decisions.
//
// Frame domains (both restart at 0 on every start()):
//  - input frames: mono float samples at 48 kHz WRITTEN to input(), counted from 0 at stream
//    start (frames the backend had to drop are not counted). inputTimes() carries {input
//    frames written so far, capture time (s) of that frame boundary}, i.e. the moment the
//    sound at that frame reached the input jack. So TimePoint frames and the analysis side's
//    sample indices (and onset indices) are always the same domain.
//  - dropped input: if input() has no room, the backend keeps the head of the period that fits,
//    drops the rest, adds the count to inputDroppedFrames() BEFORE pushing that period's
//    TimePoint (for the written head, if any), and does not count the dropped frames. The next
//    TimePoint's time therefore jumps forward by the dropped duration: the discontinuity is
//    always between two TimePoints and is visible to the consumer through the counter.
//  - output frames: the `firstFrame` passed to the RenderFn, counted from 0 at stream start.
//    outputTimes() carries {output frame, audible time (s) of that frame}.
// The callbacks push one TimePoint per period (none for an input period entirely dropped). If a
// ring is full, data is dropped (an input overflow also counts as an xrun); the callbacks never
// wait.
//
// Threading: start/stop/setRenderer are called from one controlling thread, and
// setRenderer only while stopped. input()/inputTimes()/outputTimes() each have exactly one
// consumer thread at a time (the callbacks are the producers). nowSec/xruns/failed are
// thread-safe. start() after stop() must work (the object is reused across sessions).
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    // Starts the streams. On failure returns false and sets `err` (pt-BR, user-visible).
    virtual bool start(std::string& err) = 0;
    // Stops the streams and joins the backend's threads. Idempotent.
    virtual void stop() = 0;
    virtual void setRenderer(RenderFn fn) = 0;

    virtual SpscRing<float>& input() = 0;
    virtual SpscRing<TimePoint>& inputTimes() = 0;
    virtual SpscRing<TimePoint>& outputTimes() = 0;

    virtual uint32_t xruns() const = 0;  // since the last start()
    // Input frames dropped because input() was full, since the last start(). Updated (release)
    // before the TimePoint that follows the drop is pushed.
    virtual uint64_t inputDroppedFrames() const = 0;
    // True (with a pt-BR message in `err`) once the device failed/disappeared mid-stream.
    virtual bool failed(std::string& err) const = 0;
    virtual double nowSec() const = 0;
};
