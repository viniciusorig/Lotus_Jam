#include "audio/pipewire_backend.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>
#include <time.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

constexpr double kSr = PipeWireBackend::kSampleRate;

enum Failure : int { kNone = 0, kDeviceGone = 1, kStreamError = 2, kDaemonGone = 3 };

const char* failureText(int code) {
    switch (code) {
        case kDeviceGone: return "O dispositivo de áudio foi desconectado.";
        case kStreamError: return "O dispositivo de áudio foi desconectado ou deixou de responder.";
        case kDaemonGone: return "O serviço de áudio (PipeWire) foi desconectado.";
        default: return "";
    }
}

void ensurePwInit() {
    static std::once_flag once;
    std::call_once(once, [] { pw_init(nullptr, nullptr); });
}

bool isInputClass(const std::string& c) { return c.starts_with("Audio/Source") || c == "Audio/Duplex"; }
bool isOutputClass(const std::string& c) { return c == "Audio/Sink" || c == "Audio/Duplex"; }

struct NodeInfo {
    uint32_t id;
    std::string name, description, mediaClass;
};

// A PipeWire connection on its own thread loop, with a registry listing of the nodes.
// Every member is touched with the loop lock held (or before the loop starts / after it stops).
struct Conn {
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook coreListener{}, registryListener{};
    bool started = false;
    std::vector<NodeInfo> nodes;
    int syncSeq = -1;
    bool synced = false;
    bool coreError = false;
    // Removal watch while a backend runs: a watched node going away (or the daemon) sets
    // *failure (first cause wins).
    std::atomic<int>* failure = nullptr;
    uint32_t watch[2] = {SPA_ID_INVALID, SPA_ID_INVALID};

    static void setFailure(Conn* c, int code) {
        if (!c->failure) return;
        int expected = kNone;
        c->failure->compare_exchange_strong(expected, code, std::memory_order_acq_rel);
    }

    static void onCoreDone(void* data, uint32_t id, int seq) {
        auto* c = static_cast<Conn*>(data);
        if (id == PW_ID_CORE && seq == c->syncSeq) {
            c->synced = true;
            pw_thread_loop_signal(c->loop, false);
        }
    }
    static void onCoreError(void* data, uint32_t id, int /*seq*/, int res, const char* /*message*/) {
        auto* c = static_cast<Conn*>(data);
        if (id != PW_ID_CORE) return;  // object errors are reported through the streams' state
        c->coreError = true;
        if (res == -EPIPE) setFailure(c, kDaemonGone);
        pw_thread_loop_signal(c->loop, false);
    }
    static void onGlobal(void* data, uint32_t id, uint32_t /*permissions*/, const char* type, uint32_t /*version*/,
                         const spa_dict* props) {
        auto* c = static_cast<Conn*>(data);
        if (!props || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
        const char* cls = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        if (!cls || !name) return;
        const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        if (!desc) desc = spa_dict_lookup(props, PW_KEY_NODE_NICK);
        c->nodes.push_back(NodeInfo{id, name, desc ? desc : name, cls});
    }
    static void onGlobalRemove(void* data, uint32_t id) {
        auto* c = static_cast<Conn*>(data);
        std::erase_if(c->nodes, [id](const NodeInfo& n) { return n.id == id; });
        if (id == c->watch[0] || id == c->watch[1]) setFailure(c, kDeviceGone);
    }

    // Connects and lists the registry. False when PipeWire is unreachable or does not answer
    // within `timeoutSec`. The loop is left LOCKED on success (callers continue under it).
    bool open(double timeoutSec) {
        static const pw_core_events coreEvents = [] {
            pw_core_events e{};
            e.version = PW_VERSION_CORE_EVENTS;
            e.done = onCoreDone;
            e.error = onCoreError;
            return e;
        }();
        static const pw_registry_events registryEvents = [] {
            pw_registry_events e{};
            e.version = PW_VERSION_REGISTRY_EVENTS;
            e.global = onGlobal;
            e.global_remove = onGlobalRemove;
            return e;
        }();
        ensurePwInit();
        loop = pw_thread_loop_new("treino-pipewire", nullptr);
        if (!loop) return false;
        context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
        if (!context) return false;
        if (pw_thread_loop_start(loop) != 0) return false;
        started = true;
        pw_thread_loop_lock(loop);
        core = pw_context_connect(context, nullptr, 0);
        if (!core) {
            pw_thread_loop_unlock(loop);
            return false;
        }
        pw_core_add_listener(core, &coreListener, &coreEvents, this);
        registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        if (!registry) {
            pw_thread_loop_unlock(loop);
            return false;
        }
        pw_registry_add_listener(registry, &registryListener, &registryEvents, this);
        syncSeq = pw_core_sync(core, PW_ID_CORE, 0);
        if (!waitUntil(timeoutSec, [this] { return synced || coreError; }) || coreError) {
            pw_thread_loop_unlock(loop);
            return false;
        }
        return true;  // locked
    }

    // Waits (loop locked) until cond() or timeout; false on timeout.
    template <class F>
    bool waitUntil(double timeoutSec, F cond) {
        timespec abstime{};
        pw_thread_loop_get_time(loop, &abstime, int64_t(timeoutSec * 1e9));
        while (!cond())
            if (pw_thread_loop_timed_wait_full(loop, &abstime) != 0) return cond();
        return true;
    }

    const NodeInfo* find(const std::string& name, bool (*classOk)(const std::string&)) const {
        for (const auto& n : nodes)
            if (n.name == name && classOk(n.mediaClass)) return &n;
        return nullptr;
    }

    // Unlocked on entry. Destroys everything; safe after a partial open().
    void close() {
        if (loop && started) pw_thread_loop_lock(loop);
        if (registry) {
            spa_hook_remove(&registryListener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
            registry = nullptr;
        }
        if (core) {
            spa_hook_remove(&coreListener);
            pw_core_disconnect(core);
            core = nullptr;
        }
        if (loop && started) {
            pw_thread_loop_unlock(loop);
            pw_thread_loop_stop(loop);
            started = false;
        }
        if (context) pw_context_destroy(context);
        context = nullptr;
        if (loop) pw_thread_loop_destroy(loop);
        loop = nullptr;
    }
};

std::vector<AudioDevice> listDevices(bool inputs) {
    std::vector<AudioDevice> out;
    Conn c;
    if (c.open(2.0)) {
        for (const auto& n : c.nodes) {
            if (inputs ? !isInputClass(n.mediaClass) || n.name.ends_with(".monitor") : !isOutputClass(n.mediaClass))
                continue;
            out.push_back(AudioDevice{n.name, n.description});
        }
        pw_thread_loop_unlock(c.loop);
    }
    c.close();
    std::sort(out.begin(), out.end(), [](const AudioDevice& a, const AudioDevice& b) { return a.name < b.name; });
    return out;
}

double monoNow() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

}  // namespace

std::vector<AudioDevice> listInputDevices() { return listDevices(true); }
std::vector<AudioDevice> listOutputDevices() { return listDevices(false); }

// ------------------------------------------------------------------------------------------

struct PipeWireBackend::Impl {
    explicit Impl(PipeWireBackend& b) : be(b) {}

    PipeWireBackend& be;
    Conn conn;
    pw_stream* inStream = nullptr;
    pw_stream* outStream = nullptr;
    spa_hook inListener{}, outListener{};
    pw_stream_state inState = PW_STREAM_STATE_UNCONNECTED;  // loop thread, under the lock
    pw_stream_state outState = PW_STREAM_STATE_UNCONNECTED;

    // RT handshake (see the header).
    std::atomic<bool> active{false};
    std::atomic<int> busy{0};

    // Real-time state: written by start() before `active` is released, then only by the
    // data thread's callbacks.
    int64_t delivered = 0;  // input frames written to input()
    int64_t outFrame = 0;   // next output frame
    struct TickWatch {
        bool have = false;
        uint64_t ticks = 0;
        double frames = 0;  // previous period, in graph ticks
        // True if the graph skipped cycles since the previous period of this stream.
        bool discontinuity(const pw_time& t, size_t n) {
            if (t.rate.denom == 0 || t.rate.num == 0) return false;
            bool gap = false;
            if (have && frames > 0 && t.ticks > ticks) gap = double(t.ticks - ticks) > 1.5 * frames;
            if (have && t.ticks == ticks) return false;  // same cycle (several buffers)
            have = true;
            ticks = t.ticks;
            frames = double(n) * double(t.rate.denom) / (double(t.rate.num) * kSr);
            return gap;
        }
    } inTicks, outTicks;

    struct Busy {
        std::atomic<int>& b;
        explicit Busy(std::atomic<int>& x) : b(x) { b.fetch_add(1, std::memory_order_seq_cst); }
        ~Busy() { b.fetch_sub(1, std::memory_order_release); }
    };

    static double graphSec(const pw_time& t, int64_t ticks) {
        return double(ticks) * double(t.rate.num) / double(t.rate.denom);
    }

    static void onInProcess(void* data) noexcept {
        auto* im = static_cast<Impl*>(data);
        Busy guard(im->busy);
        if (!im->active.load(std::memory_order_seq_cst)) return;
        PipeWireBackend& be = im->be;
        pw_time t{};
        const bool haveTime = pw_stream_get_time_n(im->inStream, &t, sizeof t) == 0 && t.rate.denom != 0;
        size_t total = 0;
        pw_buffer* b;
        while ((b = pw_stream_dequeue_buffer(im->inStream)) != nullptr) {
            spa_data& d = b->buffer->datas[0];
            if (d.data && d.chunk) {
                const uint32_t off = std::min(d.chunk->offset, d.maxsize);
                const uint32_t size = std::min(d.chunk->size, d.maxsize - off);
                const auto* x = reinterpret_cast<const float*>(static_cast<const uint8_t*>(d.data) + off);
                const size_t n = size / sizeof(float);
                if (n > 0) {
                    total += n;
                    const size_t written = be.input_.write(x, n);
                    if (written < n) {
                        // Overflow: counter first (release), then the TimePoint of the head.
                        be.dropped_.fetch_add(n - written, std::memory_order_release);
                        be.xruns_.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (written > 0 && haveTime) {
                        im->delivered += int64_t(written);
                        // Capture time of the end of this buffer, minus the dropped tail.
                        const int64_t cycleNs = b->time ? int64_t(b->time) : t.now;
                        const double tEnd = double(cycleNs) * 1e-9 - graphSec(t, t.delay) - double(t.buffered) / kSr;
                        const TimePoint tp{im->delivered, tEnd - double(n - written) / kSr};
                        be.inTimes_.write(&tp, 1);
                    } else if (written > 0) {
                        im->delivered += int64_t(written);  // no clock yet: data without a point
                    }
                }
            }
            pw_stream_queue_buffer(im->inStream, b);
        }
        if (haveTime && total > 0 && im->inTicks.discontinuity(t, total))
            be.xruns_.fetch_add(1, std::memory_order_relaxed);
    }

    static void onOutProcess(void* data) noexcept {
        auto* im = static_cast<Impl*>(data);
        Busy guard(im->busy);
        if (!im->active.load(std::memory_order_seq_cst)) return;
        PipeWireBackend& be = im->be;
        pw_buffer* b = pw_stream_dequeue_buffer(im->outStream);
        if (!b) {  // no free buffer: this period is lost
            be.xruns_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        spa_data& d = b->buffer->datas[0];
        constexpr uint32_t kStride = 2 * sizeof(float);
        if (!d.data || !d.chunk) {
            pw_stream_queue_buffer(im->outStream, b);
            return;
        }
        pw_time t{};
        const bool haveTime = pw_stream_get_time_n(im->outStream, &t, sizeof t) == 0 && t.rate.denom != 0;
        size_t n = d.maxsize / kStride;
        if (b->requested > 0)
            n = std::min<size_t>(n, b->requested);
        else if (haveTime && t.size > 0)
            n = std::min<size_t>(n, t.size);
        else
            n = std::min<size_t>(n, be.o_.quantum);
        auto* out = static_cast<float*>(d.data);
        if (be.render_)
            be.render_(out, n, im->outFrame);
        else
            std::fill(out, out + 2 * n, 0.0f);
        d.chunk->offset = 0;
        d.chunk->stride = kStride;
        d.chunk->size = uint32_t(n * kStride);
        b->size = n;  // pw_time.queued is then in frames
        if (haveTime) {
            // Audible time of the first frame of this (not yet queued) buffer.
            const double audible =
                double(t.now) * 1e-9 + graphSec(t, t.delay) + double(t.queued + t.buffered) / kSr;
            const TimePoint tp{im->outFrame, audible};
            be.outTimes_.write(&tp, 1);
            if (im->outTicks.discontinuity(t, n)) be.xruns_.fetch_add(1, std::memory_order_relaxed);
        }
        im->outFrame += int64_t(n);
        pw_stream_queue_buffer(im->outStream, b);
    }

    static void onState(Impl* im, pw_stream_state& slot, pw_stream_state state) {
        slot = state;
        if (im->active.load(std::memory_order_acquire) &&
            (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED))
            Conn::setFailure(&im->conn, kStreamError);
        pw_thread_loop_signal(im->conn.loop, false);
    }
    static void onInState(void* data, pw_stream_state, pw_stream_state state, const char*) {
        auto* im = static_cast<Impl*>(data);
        onState(im, im->inState, state);
    }
    static void onOutState(void* data, pw_stream_state, pw_stream_state state, const char*) {
        auto* im = static_cast<Impl*>(data);
        onState(im, im->outState, state);
    }

    // Loop locked. Creates and connects one stream.
    pw_stream* makeStream(bool capture, const std::string& target, bool captureSink, spa_hook* listener,
                          unsigned quantum) {
        static const pw_stream_events inEvents = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = onInState;
            e.process = onInProcess;
            return e;
        }();
        static const pw_stream_events outEvents = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = onOutState;
            e.process = onOutProcess;
            return e;
        }();
        const std::string latency = std::to_string(quantum) + "/48000";
        pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, capture ? "Capture" : "Playback", PW_KEY_APP_NAME,
            "Lotus Jam", PW_KEY_NODE_NAME, capture ? "treino-entrada" : "treino-saida", PW_KEY_NODE_DESCRIPTION,
            capture ? "Lotus Jam (entrada)" : "Lotus Jam (saída)", PW_KEY_TARGET_OBJECT, target.c_str(),
            PW_KEY_NODE_LATENCY, latency.c_str(), PW_KEY_NODE_DONT_RECONNECT, "true", "state.restore-props", "false",
            "state.restore-target", "false", nullptr);
        if (!props) return nullptr;
        if (captureSink) pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
        pw_stream* s = pw_stream_new(conn.core, capture ? "treino-entrada" : "treino-saida", props);  // takes props
        if (!s) return nullptr;
        pw_stream_add_listener(s, listener, capture ? &inEvents : &outEvents, this);

        uint8_t buf[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = uint32_t(kSr);
        if (capture) {
            info.channels = 1;
            info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        } else {
            info.channels = 2;
            info.position[0] = SPA_AUDIO_CHANNEL_FL;
            info.position[1] = SPA_AUDIO_CHANNEL_FR;
        }
        const spa_pod* params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
        const auto flags = pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                           PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_DONT_RECONNECT);
        if (pw_stream_connect(s, capture ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1) <
            0) {
            spa_hook_remove(listener);
            pw_stream_destroy(s);
            return nullptr;
        }
        return s;
    }

    // Unlocked on entry. Quiesces the callbacks, then destroys streams and connection.
    void teardown() {
        active.store(false, std::memory_order_seq_cst);
        while (busy.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();
        if (conn.loop && conn.started) {
            pw_thread_loop_lock(conn.loop);
            conn.failure = nullptr;
            for (auto [s, l] : {std::pair{&inStream, &inListener}, std::pair{&outStream, &outListener}}) {
                if (!*s) continue;
                spa_hook_remove(l);
                pw_stream_destroy(*s);  // synchronous with the data thread: no callback after it
                *s = nullptr;
            }
            pw_thread_loop_unlock(conn.loop);
        }
        conn.close();
    }
};

PipeWireBackend::PipeWireBackend(std::string inputNode, std::string outputNode)
    : PipeWireBackend(std::move(inputNode), std::move(outputNode), Options{}) {}

PipeWireBackend::PipeWireBackend(std::string inputNode, std::string outputNode, const Options& o)
    : inName_(std::move(inputNode)), outName_(std::move(outputNode)), o_(o), input_(o.inputCapacity) {}

PipeWireBackend::~PipeWireBackend() { stop(); }

void PipeWireBackend::setRenderer(RenderFn fn) { render_ = std::move(fn); }

double PipeWireBackend::nowSec() const { return monoNow(); }

bool PipeWireBackend::failed(std::string& err) const {
    const int f = failure_.load(std::memory_order_acquire);
    if (f == kNone) return false;
    err = failureText(f);
    return true;
}

bool PipeWireBackend::start(std::string& err) {
    if (impl_) {
        err = "O dispositivo de áudio já está em uso.";
        return false;
    }
    xruns_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
    failure_.store(kNone, std::memory_order_relaxed);
    auto im = std::make_unique<Impl>(*this);
    Conn& c = im->conn;
    auto fail = [&](std::string msg) {
        err = std::move(msg);
        im->teardown();
        failure_.store(kNone, std::memory_order_relaxed);  // failed() is for mid-stream failures
        return false;
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(o_.connectTimeoutSec);
    if (!c.open(o_.connectTimeoutSec))
        return fail("Não foi possível conectar ao serviço de áudio (PipeWire). Verifique se ele está em execução.");

    // Loop locked from here. Resolve both nodes in the registry before connecting.
    const NodeInfo* outNode = c.find(outName_, isOutputClass);
    const NodeInfo* inNode = c.find(inName_, isInputClass);
    std::string inTarget = inName_;
    bool captureSink = false;
    if (!inNode && inName_.ends_with(".monitor")) {
        inTarget = inName_.substr(0, inName_.size() - std::string_view(".monitor").size());
        inNode = c.find(inTarget, isOutputClass);
        captureSink = inNode != nullptr;
    }
    if (!inNode || !outNode) {
        pw_thread_loop_unlock(c.loop);
        return fail(!inNode ? "O dispositivo de entrada \"" + inName_ + "\" não foi encontrado."
                            : "O dispositivo de saída \"" + outName_ + "\" não foi encontrado.");
    }
    c.watch[0] = inNode->id;
    c.watch[1] = outNode->id;
    c.failure = &failure_;

    im->delivered = 0;
    im->outFrame = 0;
    im->active.store(true, std::memory_order_seq_cst);  // publishes the state above to the callbacks
    im->inStream = im->makeStream(true, inTarget, captureSink, &im->inListener, o_.quantum);
    im->outStream = im->inStream ? im->makeStream(false, outName_, false, &im->outListener, o_.quantum) : nullptr;
    if (!im->inStream || !im->outStream) {
        pw_thread_loop_unlock(c.loop);
        return fail("Não foi possível abrir os fluxos de áudio no PipeWire.");
    }
    const double left = std::max(
        0.05, std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count());
    const bool streaming = c.waitUntil(left, [&] {
        return failure_.load(std::memory_order_acquire) != kNone || im->inState == PW_STREAM_STATE_ERROR ||
               im->outState == PW_STREAM_STATE_ERROR ||
               (im->inState == PW_STREAM_STATE_STREAMING && im->outState == PW_STREAM_STATE_STREAMING);
    });
    const bool ok = streaming && failure_.load(std::memory_order_acquire) == kNone &&
                    im->inState == PW_STREAM_STATE_STREAMING && im->outState == PW_STREAM_STATE_STREAMING;
    pw_thread_loop_unlock(c.loop);
    if (!ok)
        return fail(streaming ? "O dispositivo de áudio recusou a conexão."
                              : "Tempo esgotado ao conectar ao dispositivo de áudio.");
    impl_ = std::move(im);
    return true;
}

void PipeWireBackend::stop() {
    if (!impl_) return;
    impl_->teardown();
    impl_.reset();
}
