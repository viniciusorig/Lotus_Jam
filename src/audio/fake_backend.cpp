#include "audio/fake_backend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace {
constexpr double kAttackSec = 0.004;
constexpr double kTauSec = 0.25;
constexpr double kNoteLenSec = 3.0;  // exp(-12): inaudible afterwards
}  // namespace

FakeBackend::FakeBackend(const Options& o)
    : o_(o),
      input_(o.inputCapacity),
      now_(o.startSec),
      rng_(o.seed ? o.seed : 1),
      out_(kBlock * 2),
      in_(kBlock) {
    if (!(o_.speed > 0.0)) o_.speed = 1.0;
    captured_.reserve(o_.captureOutputFrames * 2);
}

FakeBackend::~FakeBackend() { stop(); }

bool FakeBackend::start(std::string& err) {
    if (running_) {
        err = "O dispositivo de áudio já está em uso.";
        return false;
    }
    t0_ = now_.load(std::memory_order_relaxed);
    failed_.store(false, std::memory_order_relaxed);
    xruns_.store(0, std::memory_order_relaxed);
    stopReq_.store(false, std::memory_order_relaxed);
    captured_.clear();  // keeps the reserved capacity
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void FakeBackend::stop() {
    if (!running_) return;
    stopReq_.store(true, std::memory_order_release);
    thread_.join();
    running_ = false;
}

void FakeBackend::setRenderer(RenderFn fn) { render_ = std::move(fn); }

bool FakeBackend::failed(std::string& err) const {
    if (!failed_.load(std::memory_order_acquire)) return false;
    err = "O dispositivo de áudio foi desconectado.";
    return true;
}

void FakeBackend::scheduleNote(double tSec, double hz, double amp) {
    const Note n{tSec, hz, amp};
    pending_.write(&n, 1);
}

double FakeBackend::gaussian() {
    // xorshift64* + Box-Muller: portable and deterministic for a given seed.
    auto uniform = [this] {
        rng_ ^= rng_ >> 12;
        rng_ ^= rng_ << 25;
        rng_ ^= rng_ >> 27;
        return (double((rng_ * 0x2545F4914F6CDD1DULL) >> 11) + 0.5) / 9007199254740992.0;
    };
    const double u1 = uniform(), u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);
}

void FakeBackend::renderInput(float* x, size_t n, int64_t firstFrame) {
    // Capture time of input frame i: t0 + i/sr - inputLatency.
    const double tFirst = t0_ + double(firstFrame) / kSampleRate - o_.inputLatencySec;
    const double tEnd = tFirst + double(n) / kSampleRate;
    for (size_t i = 0; i < n; ++i) x[i] = o_.noiseSigma > 0 ? float(o_.noiseSigma * gaussian()) : 0.0f;

    Note p;
    while (pending_.read(&p, 1) == 1) {
        size_t v = 0;
        while (v < kVoices && used_[v]) ++v;
        if (v == kVoices) break;  // full: ignored
        voices_[v] = p;
        used_[v] = true;
    }
    for (size_t v = 0; v < kVoices; ++v) {
        if (!used_[v]) continue;
        const Note& note = voices_[v];
        if (note.t + kNoteLenSec < tFirst) {
            used_[v] = false;
            continue;
        }
        if (note.t >= tEnd) continue;
        const double w = 2.0 * std::numbers::pi * note.hz;
        for (size_t i = 0; i < n; ++i) {
            const double dt = tFirst + double(i) / kSampleRate - note.t;
            if (dt < 0.0 || dt >= kNoteLenSec) continue;
            const double env = std::min(1.0, dt / kAttackSec) * std::exp(-dt / kTauSec);
            x[i] += float(note.amp * env * std::sin(w * dt));
        }
    }
}

void FakeBackend::run() {
    using clock = std::chrono::steady_clock;
    const auto blockWall = std::chrono::duration<double>(double(kBlock) / kSampleRate / o_.speed);
    auto base = clock::now();
    int64_t k = 0;      // blocks produced
    int64_t paced = 0;  // blocks since `base`
    while (!stopReq_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_until(base + std::chrono::duration_cast<clock::duration>(blockWall * double(paced + 1)));
        ++paced;
        if (failed_.load(std::memory_order_acquire)) {
            // The device is gone: no more data, but the clock keeps running.
            ++k;
            now_.store(t0_ + double(k * int64_t(kBlock)) / kSampleRate, std::memory_order_release);
            continue;
        }
        if (o_.backpressure && input_.writable() < kBlock) {
            while (input_.writable() < kBlock && !stopReq_.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            base = clock::now();  // resume pacing from here (no catch-up burst)
            paced = 0;
            if (stopReq_.load(std::memory_order_acquire)) break;
        }
        const int64_t first = k * int64_t(kBlock);

        // Output: audible at t0 + f/sr + outputLatency.
        if (render_) {
            render_(out_.data(), kBlock, first);
        } else {
            std::fill(out_.begin(), out_.end(), 0.0f);
        }
        const size_t room = o_.captureOutputFrames * 2 - captured_.size();
        captured_.insert(captured_.end(), out_.begin(), out_.begin() + long(std::min(room, out_.size())));
        const TimePoint op{first, t0_ + double(first) / kSampleRate + o_.outputLatencySec};
        outTimes_.write(&op, 1);

        // Input.
        renderInput(in_.data(), kBlock, first);
        if (input_.write(in_.data(), kBlock) < kBlock) xruns_.fetch_add(1, std::memory_order_relaxed);
        const int64_t end = first + int64_t(kBlock);
        const TimePoint ip{end, t0_ + double(end) / kSampleRate - o_.inputLatencySec};
        inTimes_.write(&ip, 1);

        ++k;
        now_.store(t0_ + double(end) / kSampleRate, std::memory_order_release);
    }
}
