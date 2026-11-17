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
//  - input frames: mono float samples at 48 kHz delivered through input(), counted from 0 at
//    stream start. inputTimes() carries {input frames delivered so far, capture time (s) of
//    that frame boundary}, i.e. the moment the sound at that frame reached the input jack.
//  - output frames: the `firstFrame` passed to the RenderFn, counted from 0 at stream start.
//    outputTimes() carries {output frame, audible time (s) of that frame}.
// The callbacks push one TimePoint per period. If a ring is full, data is dropped (an input
// overflow counts as an xrun); the callbacks never wait.
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
    // True (with a pt-BR message in `err`) once the device failed/disappeared mid-stream.
    virtual bool failed(std::string& err) const = 0;
    virtual double nowSec() const = 0;
};
