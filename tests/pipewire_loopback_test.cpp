// Tests of PipeWireBackend against the real PipeWire daemon.
//
// They never touch the user's devices: every stream targets a private null sink created by
// this process (TestSink below, node name treino_test_<pid>_<n>) with object.linger=false,
// so the daemon destroys it when this process disconnects, even if the test crashes. The
// sink gets the lowest session/driver priority so it can never become the default device.
// Tests are skipped (GTEST_SKIP) when no PipeWire daemon is reachable.
#include <gtest/gtest.h>

#include <pipewire/pipewire.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/session.h"
#include "audio/pipewire_backend.h"
#include "dsp/notes.h"

namespace {

using Clock = std::chrono::steady_clock;

// A private null sink (Audio/Sink with monitor ports) owned by this process's own PipeWire
// connection. remove() (or the destructor) destroys it by disconnecting.
class TestSink {
public:
    TestSink() {
        static std::atomic<int> counter{0};
        name_ = "treino_test_" + std::to_string(getpid()) + "_" + std::to_string(counter++);
        pw_init(nullptr, nullptr);
        loop_ = pw_thread_loop_new("treino-test-sink", nullptr);
        if (!loop_) return;
        context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
        if (!context_ || pw_thread_loop_start(loop_) != 0) return;
        pw_thread_loop_lock(loop_);
        core_ = pw_context_connect(context_, nullptr, 0);
        if (core_) {
            pw_properties* p = pw_properties_new(
                "factory.name", "support.null-audio-sink", "node.name", name_.c_str(), "node.description",
                "Treino teste (privado)", "media.class", "Audio/Sink", "audio.channels", "2", "audio.position",
                "FL,FR", "audio.rate", "48000", "object.linger", "false", "priority.session", "1",
                "priority.driver", "1", "monitor.channel-volumes", "false", "state.restore-props", "false",
                nullptr);
            proxy_ = static_cast<pw_proxy*>(
                pw_core_create_object(core_, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &p->dict, 0));
            pw_properties_free(p);
        }
        pw_thread_loop_unlock(loop_);
        if (!proxy_) return;
        // Wait until the node is visible to other clients.
        const auto end = Clock::now() + std::chrono::seconds(3);
        while (Clock::now() < end) {
            for (const auto& d : listOutputDevices())
                if (d.name == name_) ok_ = true;
            if (ok_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ~TestSink() {
        remove();
        if (loop_) {
            pw_thread_loop_stop(loop_);
            if (context_) pw_context_destroy(context_);
            pw_thread_loop_destroy(loop_);
        }
        // No pw_deinit(): the backend under test keeps PipeWire initialised for the process.
    }
    // Destroys the sink (mid-stream device removal).
    void remove() {
        if (!core_) return;
        pw_thread_loop_lock(loop_);
        if (proxy_) pw_proxy_destroy(proxy_);
        proxy_ = nullptr;
        pw_core_disconnect(core_);
        core_ = nullptr;
        pw_thread_loop_unlock(loop_);
    }
    bool ok() const { return ok_; }
    const std::string& name() const { return name_; }
    std::string monitor() const { return name_ + ".monitor"; }

private:
    std::string name_;
    pw_thread_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_proxy* proxy_ = nullptr;
    bool ok_ = false;
};

#define REQUIRE_SINK(sink)                                                                   \
    TestSink sink;                                                                           \
    if (!sink.ok()) GTEST_SKIP() << "PipeWire indisponível: não foi possível criar o sink nulo"

bool waitFor(double seconds, const std::function<bool()>& cond) {
    const auto end = Clock::now() + std::chrono::duration<double>(seconds);
    while (!cond()) {
        if (Clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

template <class T>
std::vector<T> drainAll(SpscRing<T>& r) {
    std::vector<T> v;
    T tmp[256];
    size_t n;
    while ((n = r.read(tmp, 256)) > 0) v.insert(v.end(), tmp, tmp + n);
    return v;
}

size_t countEntries(const char* dir) {
    size_t n = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator(dir)) ++n;
    return n;
}

const Instrument& bass() {
    for (const auto& in : instruments())
        if (in.id == "baixo4") return in;
    return instruments().front();
}

}  // namespace

TEST(PipeWireBackend, NowSecIsMonotonicClock) {
    PipeWireBackend b("x", "y");
    double prev = b.nowSec();
    for (int i = 0; i < 1000; ++i) {
        const double t = b.nowSec();
        EXPECT_GE(t, prev);
        prev = t;
    }
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const double mono = double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
    EXPECT_NEAR(b.nowSec(), mono, 0.01);
}

TEST(PipeWireBackend, ConnectFailureReported) {
    REQUIRE_SINK(sink);
    const char* missing = "treino_no_que_nao_existe_xyz";
    struct Case {
        std::string in, out;
    };
    for (const Case& c : {Case{missing, sink.name()}, Case{sink.monitor(), missing}, Case{missing, missing},
                          Case{sink.name() + "x.monitor", sink.name()}}) {
        PipeWireBackend b(c.in, c.out);
        std::string err;
        const auto t0 = Clock::now();
        EXPECT_FALSE(b.start(err)) << c.in << " -> " << c.out;
        const double dt = std::chrono::duration<double>(Clock::now() - t0).count();
        EXPECT_LT(dt, 3.0);
        EXPECT_FALSE(err.empty());
        EXPECT_NE(err.find("não"), std::string::npos) << err;  // pt-BR message
        b.stop();
        b.stop();  // idempotent
    }
}

TEST(PipeWireBackend, ConnectFailureWhenPipeWireIsNotRunning) {
    const char* old = std::getenv("PIPEWIRE_REMOTE");
    const std::string saved = old ? old : "";
    setenv("PIPEWIRE_REMOTE", "treino-socket-que-nao-existe", 1);
    {
        PipeWireBackend b("a", "b");
        std::string err;
        const auto t0 = Clock::now();
        EXPECT_FALSE(b.start(err));
        EXPECT_LT(std::chrono::duration<double>(Clock::now() - t0).count(), 3.0);
        EXPECT_FALSE(err.empty());
        EXPECT_TRUE(listInputDevices().empty());
        EXPECT_TRUE(listOutputDevices().empty());
    }
    if (old)
        setenv("PIPEWIRE_REMOTE", saved.c_str(), 1);
    else
        unsetenv("PIPEWIRE_REMOTE");
}

TEST(PipeWireDevices, ListsNullSinkAndExcludesMonitors) {
    REQUIRE_SINK(sink);
    for (int rep = 0; rep < 5; ++rep) {
        const auto outs = listOutputDevices();
        const auto ins = listInputDevices();
        bool found = false;
        for (const auto& d : outs)
            if (d.name == sink.name()) {
                found = true;
                EXPECT_EQ(d.description, "Treino teste (privado)");
            }
        EXPECT_TRUE(found);
        for (const auto& d : ins) {
            EXPECT_FALSE(d.name.ends_with(".monitor")) << d.name;
            EXPECT_NE(d.name, sink.name());
            EXPECT_FALSE(d.description.empty()) << d.name;
        }
        for (const auto& d : outs) EXPECT_FALSE(d.description.empty()) << d.name;
    }
    // Also safe while a backend runs.
    PipeWireBackend b(sink.monitor(), sink.name());
    std::string err;
    ASSERT_TRUE(b.start(err)) << err;
    bool found = false;
    for (const auto& d : listOutputDevices()) found = found || d.name == sink.name();
    EXPECT_TRUE(found);
    b.stop();
}

TEST(PipeWireBackend, StreamsDeliverTimePointsAndRestart) {
    REQUIRE_SINK(sink);
    PipeWireBackend b(sink.monitor(), sink.name());
    std::atomic<int64_t> rendered{0};
    const size_t fds0 = countEntries("/proc/self/fd");
    const size_t threads0 = countEntries("/proc/self/task");
    for (int rep = 0; rep < 10; ++rep) {
        rendered = 0;
        b.setRenderer([&rendered](float* out, size_t frames, int64_t first) {
            std::fill(out, out + 2 * frames, 0.0f);
            rendered.store(first + int64_t(frames), std::memory_order_relaxed);
        });
        drainAll(b.input());
        drainAll(b.inputTimes());
        drainAll(b.outputTimes());
        std::string err;
        const double before = b.nowSec();
        ASSERT_TRUE(b.start(err)) << "rep " << rep << ": " << err;
        ASSERT_TRUE(waitFor(3.0, [&] { return b.outputTimes().readable() >= 4 && b.inputTimes().readable() >= 4; }))
            << "rep " << rep;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        b.stop();
        const auto out = drainAll(b.outputTimes());
        const auto in = drainAll(b.inputTimes());
        const size_t samples = drainAll(b.input()).size();
        ASSERT_FALSE(out.empty());
        ASSERT_FALSE(in.empty());
        // Frame domains restart at 0 on every start().
        EXPECT_EQ(out.front().frame, 0) << "rep " << rep;
        EXPECT_GT(in.front().frame, 0);
        EXPECT_LE(in.front().frame, 8192);
        EXPECT_EQ(in.back().frame, int64_t(samples)) << "rep " << rep;
        EXPECT_EQ(b.inputDroppedFrames(), 0u);
        EXPECT_GE(rendered.load(), out.back().frame);
        for (size_t i = 1; i < out.size(); ++i) {
            EXPECT_GT(out[i].frame, out[i - 1].frame);
            EXPECT_GT(out[i].tSec, out[i - 1].tSec);
        }
        for (size_t i = 1; i < in.size(); ++i) {
            EXPECT_GT(in[i].frame, in[i - 1].frame);
            EXPECT_GT(in[i].tSec, in[i - 1].tSec);
        }
        // Times are on nowSec()'s clock: audible output is in the near future of the
        // callback, capture is in the near past of it.
        EXPECT_GT(out.front().tSec, before);
        EXPECT_LT(out.back().tSec, b.nowSec() + 0.5);
        EXPECT_GT(in.front().tSec, before - 0.5);
        EXPECT_LT(in.back().tSec, b.nowSec());
        // Rate of both clocks is ~48 kHz.
        if (out.size() > 8) {
            const double rate = double(out.back().frame - out.front().frame) / (out.back().tSec - out.front().tSec);
            EXPECT_NEAR(rate, 48000.0, 480.0);
        }
        std::string ferr;
        EXPECT_FALSE(b.failed(ferr)) << ferr;
        b.setRenderer(nullptr);
    }
    // Nothing leaked: same descriptors and threads as before (the TestSink's are older).
    EXPECT_EQ(countEntries("/proc/self/fd"), fds0);
    EXPECT_EQ(countEntries("/proc/self/task"), threads0);
}

TEST(PipeWireBackend, FailedAfterDeviceRemoved) {
    REQUIRE_SINK(sink);
    PipeWireBackend b(sink.monitor(), sink.name());
    std::string err;
    ASSERT_TRUE(b.start(err)) << err;
    ASSERT_TRUE(waitFor(3.0, [&] { return b.outputTimes().readable() >= 4; }));
    EXPECT_FALSE(b.failed(err));
    sink.remove();
    const auto t0 = Clock::now();
    EXPECT_TRUE(waitFor(3.0, [&] { return b.failed(err); }));
    std::printf("[ measured ] failure detected after %.0f ms\n",
                1000.0 * std::chrono::duration<double>(Clock::now() - t0).count());
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("desconectado"), std::string::npos) << err;
    const auto t1 = Clock::now();
    b.stop();
    EXPECT_LT(std::chrono::duration<double>(Clock::now() - t1).count(), 2.0);
    b.stop();
}

TEST(PipeWireBackend, InputDropsCountOnlyWrittenFrames) {
    REQUIRE_SINK(sink);
    PipeWireBackend::Options o;
    o.inputCapacity = 1024;  // ~21 ms: overflows while the consumer stalls
    PipeWireBackend b(sink.monitor(), sink.name(), o);
    std::string err;
    ASSERT_TRUE(b.start(err)) << err;
    std::vector<float> buf(4096);
    int64_t consumed = 0;  // frames read from input()
    std::vector<TimePoint> pts;
    double maxJumpExcess = 0.0;   // largest (time jump - frame step) between points, frames
    double unexplained = 0.0;     // largest jump excess not covered by the drop counter
    auto consume = [&] {
        size_t n;
        while ((n = b.input().read(buf.data(), buf.size())) > 0) consumed += int64_t(n);
        TimePoint tp;
        while (b.inputTimes().read(&tp, 1) == 1) {
            // Read the counter AFTER the point: it covers every drop before that point.
            const double dropped = double(b.inputDroppedFrames());
            if (!pts.empty()) {
                const TimePoint& p = pts.back();
                EXPECT_GT(tp.frame, p.frame);
                const double excess = (tp.tSec - p.tSec) * 48000.0 - double(tp.frame - p.frame);
                maxJumpExcess = std::max(maxJumpExcess, excess);
                // The time skipped so far must already be covered by the counter (it is
                // updated before the point that follows a drop is pushed), within jitter.
                const TimePoint& f = pts.front();
                const double cumExcess = (tp.tSec - f.tSec) * 48000.0 - double(tp.frame - f.frame);
                unexplained = std::max(unexplained, cumExcess - dropped);
            }
            pts.push_back(tp);
        }
    };
    // Stalled consumer: 60 ms pauses overflow the 21 ms ring.
    int burst = 0;
    for (auto end = Clock::now() + std::chrono::milliseconds(1200); Clock::now() < end; ++burst) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        consume();
    }
    // Then a prompt consumer, so that the last point comes after the last drop.
    const uint64_t droppedBefore = b.inputDroppedFrames();
    for (auto end = Clock::now() + std::chrono::milliseconds(300); Clock::now() < end;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        consume();
    }
    const uint64_t droppedAfter = b.inputDroppedFrames();
    b.stop();
    consume();
    EXPECT_LT(unexplained, 960.0) << "a time jump was seen before the drop counter covered it";
    ASSERT_GE(pts.size(), 10u);
    const uint64_t dropped = b.inputDroppedFrames();
    // The residual check below needs no drop after the last point (prompt phase).
    if (dropped != droppedAfter || droppedAfter != droppedBefore)
        std::printf("[ note ] drops during the prompt phase: %llu\n", (unsigned long long)(dropped - droppedBefore));
    EXPECT_GT(dropped, 0u);
    EXPECT_GT(b.xruns(), 0u);
    // TimePoint frames count exactly the frames written to input().
    EXPECT_EQ(pts.back().frame, consumed);
    // Time elapsed between first and last point = written + dropped frames (within jitter).
    const double spanFrames = (pts.back().tSec - pts.front().tSec) * 48000.0;
    const double written = double(pts.back().frame - pts.front().frame);
    const double residual = spanFrames - written - double(dropped);
    EXPECT_NEAR(residual, 0.0, 960.0) << "span " << spanFrames << " written " << written << " dropped " << dropped;
    EXPECT_GT(maxJumpExcess, 1000.0);  // drops are visible as time jumps
    std::printf("[ measured ] bursts=%d points=%zu written=%lld dropped=%llu xruns=%u residual=%.0f frames\n", burst,
                pts.size(), (long long)consumed, (unsigned long long)dropped, b.xruns(), residual);
}

TEST(PipeWireLoopback, MeasuresStableOffset) {
    REQUIRE_SINK(sink);
    auto backend = std::make_unique<PipeWireBackend>(sink.monitor(), sink.name());
    PipeWireBackend* pb = backend.get();
    Session s(std::move(backend));
    SessionConfig cfg;
    cfg.mode = Mode::Training;
    cfg.bpm = 120;
    cfg.countIn = 4;
    cfg.subdivision = 1;
    cfg.instrument = &bass();
    cfg.tuning = cfg.instrument->tunings.front();
    std::string err;
    ASSERT_TRUE(s.start(cfg, err)) << err;
    std::vector<UiEvent> events;
    auto slotDone = [&](int64_t slot) {
        for (const auto& e : events)
            if ((e.type == UiEvent::Note || e.type == UiEvent::Miss) && e.n >= slot) return true;
        return false;
    };
    const bool done = waitFor(40.0, [&] {
        s.pollEvents(events);
        return slotDone(20);
    });
    const uint32_t xruns = pb->xruns();
    s.stop();
    s.pollEvents(events);
    ASSERT_TRUE(done);

    std::vector<double> errs(20, NAN);
    int misses = 0;
    for (const auto& e : events) {
        if (e.type == UiEvent::Note && e.n >= 0 && e.n < 20) errs[size_t(e.n)] = e.a;
        if (e.type == UiEvent::Miss && e.n < 20) ++misses;
        if (e.type == UiEvent::Error) ADD_FAILURE() << e.text;
    }
    EXPECT_EQ(misses, 0);
    double sum = 0, sum2 = 0;
    int hits = 0;
    for (size_t k = 0; k < errs.size(); ++k) {
        if (std::isnan(errs[k])) {
            ADD_FAILURE() << "slot " << k << " not detected";
            continue;
        }
        ++hits;
        sum += errs[k];
        sum2 += errs[k] * errs[k];
    }
    ASSERT_EQ(hits, 20);
    const double mean = sum / hits;
    const double sd = std::sqrt(std::max(0.0, sum2 / hits - mean * mean));
    std::printf("[ measured ] loopback mean=%.2f ms std=%.2f ms min=%.2f max=%.2f xruns=%u\n", mean, sd,
                *std::min_element(errs.begin(), errs.end()), *std::max_element(errs.begin(), errs.end()), xruns);
    EXPECT_LT(std::fabs(mean), 25.0);
    EXPECT_LT(sd, 5.0);
    EXPECT_EQ(xruns, 0u);
}
