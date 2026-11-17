#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/backend.h"

// PipeWireBackend: IAudioBackend on PipeWire (libpipewire-0.3).
//
// Streams. Two pw_streams on a private pw_thread_loop (own context and core connection,
// created in start(), destroyed in stop(), so the object is restartable):
//  - capture: mono float32 48 kHz ("treino-entrada", Stream/Input/Audio);
//  - playback: stereo interleaved float32 48 kHz ("treino-saida", Stream/Output/Audio).
// Both use PW_STREAM_FLAG_AUTOCONNECT | MAP_BUFFERS | RT_PROCESS | DONT_RECONNECT, so the
// `process` callbacks run on PipeWire's real-time data thread and a stream is never moved to
// another device when its target goes away. PipeWire's adapter converts channels/rate.
//
// Property keys: target.object = node name (an input name "<sink>.monitor" is resolved to
// the sink node plus stream.capture.sink=true), node.latency = "256/48000" (a request: the
// graph may run another quantum, every callback handles whatever size it gets; the graph
// rate is not forced, the stream's adapter resamples to/from 48 kHz), node.dont-reconnect =
// true (WirePlumber destroys the stream instead of moving it to the default device),
// state.restore-props/restore-target = false (WirePlumber stores nothing for these streams),
// media.type = Audio, media.category = Capture/Playback, node.name = treino-entrada/saida.
// Before connecting, start() lists the registry (pw_registry + pw_core_sync, with a timeout)
// and fails if a node is missing: a stream with an unknown target.object would silently be
// linked to the default device instead. The registry listener stays alive while running: the
// removal of a target node makes failed() true ("O dispositivo de áudio foi desconectado.").
// So do a stream entering the error/unconnected state and a core error (daemon gone).
//
// Time conversion. pw_time.now and pw_buffer.time are CLOCK_MONOTONIC nanoseconds (the graph
// cycle time), the clock of nowSec(). pw_time.delay is in units of pw_time.rate (graph
// clock), queued/buffered in stream frames (48 kHz; pw_buffer.size is set in frames).
//  - output: the first frame of the buffer filled in cycle `now` is audible at
//      now + delay * rate + (queued + buffered) / 48000
//    (the "total delay for the first sample in a newly queued buffer" of pw_time's docs).
//  - input: the last frame of a buffer queued in cycle `time` (pw_buffer.time, or pw_time.now
//    when 0) was captured at
//      time - delay * rate - buffered / 48000
//    so the boundary after the written frames is that minus the dropped tail (see below).
// One TimePoint per period: input {frames written so far, capture time of that boundary},
// output {first output frame of the period, its audible time}.
//
// Xruns (xruns(), since the last start()) count:
//  - a graph discontinuity seen by a stream: pw_time.ticks advanced by more than 1.5 times
//    the previous period (a cycle where this stream got no data / produced none);
//  - an output period with no free buffer (pw_stream_dequeue_buffer returned NULL);
//  - an input overflow (input() full: the head that fits is kept, the tail is dropped and
//    added to inputDroppedFrames() BEFORE that period's TimePoint is pushed; no TimePoint
//    for a period entirely dropped). pw_stream has no xrun event; spa_io_clock's xrun flag
//    is graph-internal, so these are the observable symptoms.
//
// Real-time rules for the process callbacks: no allocation, no locks, no I/O, no exceptions,
// no std::function construction (the RenderFn is only called). They only dequeue/queue
// buffers, call pw_stream_get_time_n (RT safe), copy into the SPSC rings and update atomics.
// A callback first increments an atomic `busy` counter and checks `active`; stop() clears
// `active` and waits for `busy` to drop to 0 before destroying anything, so no callback can
// touch the renderer or counters after stop() returns (this handshake is also what makes the
// ordering visible to ThreadSanitizer, which cannot see PipeWire's internal synchronisation).
// Non-RT events (state, registry, core errors) run on the thread loop and only set atomics.
//
// Threading: start/stop/setRenderer from one controlling thread (setRenderer only while
// stopped); nowSec/xruns/failed/inputDroppedFrames are thread-safe.
struct AudioDevice {
    std::string name;         // PipeWire node.name (the value for PipeWireBackend's constructor)
    std::string description;  // node.description (human readable)
};

// Short-lived registry listings (own connection, ~2 s timeout). Inputs are Audio/Source nodes
// (sink monitors excluded), outputs Audio/Sink nodes. Empty when PipeWire is unavailable.
// Safe to call from any thread, repeatedly and while a backend runs.
std::vector<AudioDevice> listInputDevices();
std::vector<AudioDevice> listOutputDevices();

class PipeWireBackend : public IAudioBackend {
public:
    struct Options {
        size_t inputCapacity = size_t(1) << 17;  // samples (~2.7 s), power of two
        unsigned quantum = 256;                  // requested frames per period
        double connectTimeoutSec = 2.5;          // registry listing + streams running
    };
    static constexpr double kSampleRate = 48000.0;

    PipeWireBackend(std::string inputNode, std::string outputNode);
    PipeWireBackend(std::string inputNode, std::string outputNode, const Options& o);
    ~PipeWireBackend() override;
    PipeWireBackend(const PipeWireBackend&) = delete;
    PipeWireBackend& operator=(const PipeWireBackend&) = delete;

    bool start(std::string& err) override;
    void stop() override;
    void setRenderer(RenderFn fn) override;
    SpscRing<float>& input() override { return input_; }
    SpscRing<TimePoint>& inputTimes() override { return inTimes_; }
    SpscRing<TimePoint>& outputTimes() override { return outTimes_; }
    uint32_t xruns() const override { return xruns_.load(std::memory_order_relaxed); }
    uint64_t inputDroppedFrames() const override { return dropped_.load(std::memory_order_acquire); }
    bool failed(std::string& err) const override;
    double nowSec() const override;

    struct Impl;  // PipeWire objects and callbacks (pipewire_backend.cpp)

private:
    friend struct Impl;
    std::string inName_, outName_;
    Options o_;
    SpscRing<float> input_;
    SpscRing<TimePoint> inTimes_{1024}, outTimes_{1024};
    RenderFn render_;
    std::unique_ptr<Impl> impl_;  // non-null while started
    std::atomic<uint32_t> xruns_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<int> failure_{0};  // 0 = none, else a message code
};
