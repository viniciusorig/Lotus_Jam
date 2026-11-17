#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/session.h"
#include "app/wav_writer.h"
#include "audio/click_scheduler.h"
#include "audio/fake_backend.h"
#include "dsp/notes.h"

// All timing decisions in these tests use the FakeBackend's virtual clock. Wall-clock time is
// only used for generous timeouts (a failing test ends instead of hanging), never to decide a
// result, so the tests do not depend on the speed of the machine (also under TSAN).

namespace {

constexpr double kSr = 48000.0;
// Generous wall-clock timeout; TREINO_TEST_TIMEOUT_SEC overrides it (e.g. for quick RED runs).
double timeoutSec() {
    const char* e = std::getenv("TREINO_TEST_TIMEOUT_SEC");
    return e ? std::atof(e) : 120.0;
}

const Instrument& instrument(const std::string& id) {
    for (const auto& in : instruments())
        if (in.id == id) return in;
    ADD_FAILURE() << "no instrument " << id;
    return instruments().front();
}

SessionConfig training(double bpm = 120, double compensationMs = 0) {
    SessionConfig c;
    c.mode = Mode::Training;
    c.bpm = bpm;
    c.countIn = 4;
    c.subdivision = 1;
    c.compensationMs = compensationMs;
    c.gainDb = 0;
    c.instrument = &instrument("baixo4");
    c.tuning = c.instrument->tunings.front();
    c.a4 = 440;
    return c;
}

// Polls `cond` every millisecond of wall time; false on timeout.
bool waitUntil(const std::function<bool()>& cond) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSec());
    while (!cond()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

struct Collector {
    Session& s;
    std::vector<UiEvent> all;
    void poll() { s.pollEvents(all); }
    size_t count(UiEvent::Type t) const {
        size_t n = 0;
        for (const auto& e : all) n += e.type == t;
        return n;
    }
    std::vector<UiEvent> of(UiEvent::Type t) const {
        std::vector<UiEvent> v;
        for (const auto& e : all)
            if (e.type == t) v.push_back(e);
        return v;
    }
    // True once slot `slots - 1` was finalised as a Miss (slots are finalised in order, so
    // every slot before it is final too). Callers make sure that slot is not played.
    bool slotsDone(int64_t slots) const {
        for (const auto& e : all)
            if (e.type == UiEvent::Miss && e.n >= slots - 1) return true;
        return false;
    }
    bool waitFor(const std::function<bool()>& cond) {
        return waitUntil([&] {
            poll();
            return cond();
        });
    }
};

// Audible time of beat n (count-in included) for a stream that started at t0.
double beatTime(const ClickScheduler& cs, double t0, int64_t n, double outLatency) {
    const double frame = double(std::llround(double(cs.firstBeatFrame()) + double(n) * cs.framesPerBeat()));
    return t0 + frame / kSr + outLatency;
}

// Deterministic jitter in [-3, 3] ms.
double jitterMs(int k) {
    static const double j[] = {1.5, -2.0, 0.5, 3.0, -1.0, -3.0, 2.5, 0.0, -0.5, 2.0, -2.5, 1.0, -1.5, 3.0, 0.5, -3.0};
    return j[k % 16];
}

struct PlayerRun {
    std::vector<UiEvent> events;
    std::vector<double> expectedErrMs;  // per slot
    GridScorer::Summary summary;
};

// BPM 120, count-in 4, a synthetic player hits the first 16 scored beats at +offsetMs+jitter
// on the bass open E string (41.2 Hz).
PlayerRun runSyntheticPlayer(double offsetMs, double compensationMs) {
    FakeBackend::Options o;
    o.speed = 20;
    o.backpressure = true;
    o.noiseSigma = 0.002;
    auto fake = std::make_unique<FakeBackend>(o);
    FakeBackend* f = fake.get();
    Session s(std::move(fake));

    const SessionConfig cfg = training(120, compensationMs);
    ClickScheduler cs(kSr, cfg.bpm, cfg.countIn);
    const double t0 = f->nowSec();  // the virtual clock is frozen while stopped
    PlayerRun r;
    for (int k = 0; k < 16; ++k) {
        const double err = offsetMs + jitterMs(k);
        r.expectedErrMs.push_back(err);
        f->scheduleNote(beatTime(cs, t0, cfg.countIn + k, o.outputLatencySec) + err / 1000.0, 41.2, 0.3);
    }

    std::string err;
    EXPECT_TRUE(s.start(cfg, err)) << err;
    Collector c{s, {}};
    EXPECT_TRUE(c.waitFor([&] { return c.slotsDone(18); }));
    s.stop();
    c.poll();
    r.events = c.all;
    r.summary = s.summary();
    return r;
}

}  // namespace

TEST(WavWriter, WritesHeaderAndClippedSamples) {
    const auto path = (std::filesystem::temp_directory_path() / "treino_wav_writer_test.wav").string();
    const float x[] = {0.0f, 0.5f, -0.5f, 2.0f, -2.0f};
    ASSERT_TRUE(writeWav16Mono(path, x, 5, 48000));
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(b.size(), 44u + 10u);
    EXPECT_EQ(std::memcmp(b.data(), "RIFF", 4), 0);
    EXPECT_EQ(std::memcmp(b.data() + 8, "WAVE", 4), 0);
    auto i16 = [&](size_t at) { return int16_t(uint16_t(b[at] | (b[at + 1] << 8))); };
    EXPECT_EQ(i16(44), 0);
    EXPECT_EQ(i16(46), 16384);  // 0.5 * 32767 rounded
    EXPECT_EQ(i16(48), -16384);
    EXPECT_EQ(i16(50), 32767);
    EXPECT_EQ(i16(52), -32767);
    std::filesystem::remove(path);
    EXPECT_FALSE(writeWav16Mono("/nonexistent-dir/x.wav", x, 5, 48000));
}

TEST(Session, TrainingScoresSyntheticPlayer) {
    PlayerRun r = runSyntheticPlayer(12.0, 0.0);
    int notes = 0, misses = 0, extras = 0;
    double sumScore = 0, worst = 0;
    for (const auto& e : r.events) {
        if (e.type == UiEvent::Note && e.n < 16) {
            ++notes;
            sumScore += e.b;
            const double d = e.a - r.expectedErrMs[size_t(e.n)];
            worst = std::max(worst, std::fabs(d));
            EXPECT_NEAR(e.a, r.expectedErrMs[size_t(e.n)], 4.0) << "slot " << e.n;
        }
        if (e.type == UiEvent::Miss && e.n < 16) ++misses;
        if (e.type == UiEvent::Extra) ++extras;
    }
    EXPECT_EQ(notes, 16);
    EXPECT_EQ(misses, 0);
    EXPECT_EQ(extras, 0);
    ASSERT_GT(notes, 0);
    EXPECT_GE(sumScore / notes, 95.0);
    std::printf("[ measured ] notes=%d mean score=%.2f worst |err-known|=%.2f ms meanErr=%.2f ms std=%.2f ms\n",
                notes, sumScore / notes, worst, r.summary.meanErrMs, r.summary.stdErrMs);

    // Beat events: count-in first, audible times on the backend clock, increasing by one beat.
    std::vector<UiEvent> beats;
    for (const auto& e : r.events)
        if (e.type == UiEvent::Beat) beats.push_back(e);
    ASSERT_GE(beats.size(), 20u);
    for (size_t i = 0; i < beats.size(); ++i) {
        EXPECT_EQ(beats[i].n, int64_t(i));
        if (i) {
            EXPECT_NEAR(beats[i].a - beats[i - 1].a, 0.5, 0.001);
        }
    }
    // Exactly one Stopped, and it is the last event.
    EXPECT_EQ(r.events.back().type, UiEvent::Stopped);
    int stopped = 0;
    for (const auto& e : r.events) stopped += e.type == UiEvent::Stopped;
    EXPECT_EQ(stopped, 1);
    // Summary is refreshed at stop and covers the scored slots.
    EXPECT_GE(r.summary.slots, 16);
    EXPECT_EQ(r.summary.hits, 16);
}

TEST(Session, BeatTimesMatchOutputGeometry) {
    FakeBackend::Options o;
    o.speed = 20;
    o.backpressure = true;
    auto fake = std::make_unique<FakeBackend>(o);
    FakeBackend* f = fake.get();
    Session s(std::move(fake));
    const SessionConfig cfg = training(120);
    ClickScheduler cs(kSr, cfg.bpm, cfg.countIn);
    const double t0 = f->nowSec();
    std::string err;
    ASSERT_TRUE(s.start(cfg, err)) << err;
    Collector c{s, {}};
    ASSERT_TRUE(c.waitFor([&] { return c.count(UiEvent::Beat) >= 8; }));
    // Beats are announced ahead of time: at most 0.3 s (plus one scoring tick) before audible.
    s.stop();
    c.poll();
    auto beats = c.of(UiEvent::Beat);
    for (const auto& b : beats) EXPECT_NEAR(b.a, beatTime(cs, t0, b.n, o.outputLatencySec), 1e-4) << b.n;
}

TEST(Session, SilenceAllMisses) {
    FakeBackend::Options o;
    o.speed = 20;
    o.backpressure = true;
    o.noiseSigma = 0.003;  // stationary noise only
    Session s(std::make_unique<FakeBackend>(o));
    std::string err;
    ASSERT_TRUE(s.start(training(120), err)) << err;
    Collector c{s, {}};
    ASSERT_TRUE(c.waitFor([&] { return c.slotsDone(12); }));
    s.stop();
    c.poll();
    EXPECT_EQ(c.count(UiEvent::Note), 0u);
    EXPECT_EQ(c.count(UiEvent::Extra), 0u);
    auto misses = c.of(UiEvent::Miss);
    ASSERT_GE(misses.size(), 12u);
    for (size_t i = 0; i < misses.size(); ++i) EXPECT_EQ(misses[i].n, int64_t(i));
    const auto sum = s.summary();
    EXPECT_EQ(sum.hits, 0);
    EXPECT_GE(sum.slots, 12);
    EXPECT_EQ(sum.total, 0.0);
    EXPECT_EQ(c.count(UiEvent::Stopped), 1u);
}

TEST(Session, CompensationApplied) {
    PlayerRun r = runSyntheticPlayer(12.0, 12.0);
    double sum = 0;
    int notes = 0;
    for (const auto& e : r.events)
        if (e.type == UiEvent::Note && e.n < 16) {
            sum += e.a;
            ++notes;
        }
    ASSERT_EQ(notes, 16);
    EXPECT_NEAR(sum / notes, 0.0, 3.0);
    EXPECT_NEAR(r.summary.meanErrMs, 0.0, 3.0);
    std::printf("[ measured ] compensated mean error=%.2f ms\n", sum / notes);
}

TEST(Session, TunerModeEmitsReadings) {
    FakeBackend::Options o;
    o.speed = 4;
    o.backpressure = true;
    o.noiseSigma = 0.001;
    o.captureOutputFrames = size_t(4 * kSr);
    auto fake = std::make_unique<FakeBackend>(o);
    FakeBackend* f = fake.get();
    Session s(std::move(fake));

    SessionConfig cfg = training();
    cfg.mode = Mode::Tuner;
    cfg.instrument = &instrument("guitarra");
    cfg.tuning = cfg.instrument->tunings.front();  // E2 A2 D3 G3 B3 E4
    const double t0 = f->nowSec();
    for (int k = 0; k < 4; ++k) f->scheduleNote(t0 + 0.2 + 0.5 * k, 110.0, 0.4);  // A2
    std::string err;
    ASSERT_TRUE(s.start(cfg, err)) << err;
    Collector c{s, {}};
    ASSERT_TRUE(c.waitFor([&] { return f->nowSec() > t0 + 2.2; }));
    s.stop();
    c.poll();

    int voiced = 0;
    for (const auto& e : c.of(UiEvent::Tuner))
        if (e.tuner.voiced) {
            ++voiced;
            EXPECT_EQ(e.tuner.stringIndex, 1);
            EXPECT_NEAR(e.tuner.cents, 0.0, 2.0);
            EXPECT_NEAR(e.tuner.hz, 110.0, 0.2);
        }
    EXPECT_GE(voiced, 10);
    EXPECT_EQ(c.count(UiEvent::Beat), 0u);
    EXPECT_EQ(c.count(UiEvent::Note), 0u);
    EXPECT_EQ(c.count(UiEvent::Miss), 0u);
    EXPECT_GT(c.count(UiEvent::Level), 0u);
    const auto& out = f->capturedOutput();
    ASSERT_GT(out.size(), size_t(2 * kSr));
    for (float v : out) ASSERT_EQ(v, 0.0f);  // silence in tuner mode
}

TEST(Session, BackendFailureStops) {
    FakeBackend::Options o;
    o.speed = 20;
    o.backpressure = true;
    auto fake = std::make_unique<FakeBackend>(o);
    FakeBackend* f = fake.get();
    Session s(std::move(fake));
    std::string err;
    ASSERT_TRUE(s.start(training(), err)) << err;
    Collector c{s, {}};
    const double t0 = f->nowSec();
    ASSERT_TRUE(c.waitFor([&] { return f->nowSec() > t0 + 1.5; }));
    f->simulateFailure();
    ASSERT_TRUE(c.waitFor([&] { return c.count(UiEvent::Stopped) == 1; }));
    auto errors = c.of(UiEvent::Error);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].text, "O dispositivo de áudio foi desconectado.");
    s.stop();  // must return (threads already finished)
    s.stop();  // idempotent
    c.poll();
    EXPECT_EQ(c.count(UiEvent::Stopped), 1u);
    EXPECT_EQ(c.all.back().type, UiEvent::Stopped);
    // The session can be restarted on the same (recovered) backend.
    ASSERT_TRUE(s.start(training(), err)) << err;
    ASSERT_TRUE(c.waitFor([&] { return c.count(UiEvent::Beat) >= 2; }));
    s.stop();
    c.poll();
    EXPECT_EQ(c.count(UiEvent::Stopped), 2u);
}

TEST(Session, StartStopStress) {
    FakeBackend::Options o;
    o.speed = 20;
    auto fake = std::make_unique<FakeBackend>(o);
    FakeBackend* f = fake.get();
    Session s(std::move(fake));
    Collector c{s, {}};
    for (int i = 0; i < 20; ++i) {
        SessionConfig cfg = training(60 + 9 * i);
        if (i % 5 == 4) {
            cfg.mode = Mode::Tuner;
            cfg.instrument = &instrument("guitarra");
            cfg.tuning = cfg.instrument->tunings.front();
        }
        cfg.subdivision = (i % 3 == 0) ? 4 : 1;
        std::string err;
        ASSERT_TRUE(s.start(cfg, err)) << err;
        if (i % 2) {  // half of the cycles run for a little virtual time, the rest stop at once
            const double t0 = f->nowSec();
            ASSERT_TRUE(waitUntil([&] { return f->nowSec() > t0 + 0.2 * (i % 7); }));
        }
        if (i % 4 == 0) s.recordSample((std::filesystem::temp_directory_path() / "treino_stress.wav").string(), 0.5);
        s.stop();
        c.poll();
        EXPECT_EQ(c.count(UiEvent::Stopped), size_t(i + 1));
    }
    std::filesystem::remove(std::filesystem::temp_directory_path() / "treino_stress.wav");
    // Destructor with a running session must also stop and join everything.
    std::string err;
    ASSERT_TRUE(s.start(training(), err)) << err;
}

TEST(Session, RecordSampleWritesWav) {
    FakeBackend::Options o;
    o.speed = 20;
    o.backpressure = true;
    o.noiseSigma = 0.01;
    Session s(std::make_unique<FakeBackend>(o));
    const auto path = (std::filesystem::temp_directory_path() / "treino_record_test.wav").string();
    std::filesystem::remove(path);
    std::string err;
    ASSERT_TRUE(s.start(training(), err)) << err;
    s.recordSample(path, 1.5);
    Collector c{s, {}};
    ASSERT_TRUE(c.waitFor([&] { return std::filesystem::exists(path); }));
    s.stop();
    c.poll();
    EXPECT_EQ(c.count(UiEvent::Error), 0u);

    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_GE(b.size(), 44u);
    auto u32 = [&](size_t at) { return uint32_t(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (uint32_t(b[at + 3]) << 24)); };
    auto u16 = [&](size_t at) { return uint16_t(b[at] | (b[at + 1] << 8)); };
    EXPECT_EQ(std::memcmp(b.data(), "RIFF", 4), 0);
    EXPECT_EQ(u32(4), b.size() - 8);
    EXPECT_EQ(std::memcmp(b.data() + 8, "WAVE", 4), 0);
    EXPECT_EQ(std::memcmp(b.data() + 12, "fmt ", 4), 0);
    EXPECT_EQ(u32(16), 16u);
    EXPECT_EQ(u16(20), 1u);       // PCM
    EXPECT_EQ(u16(22), 1u);       // mono
    EXPECT_EQ(u32(24), 48000u);   // sample rate
    EXPECT_EQ(u32(28), 96000u);   // byte rate
    EXPECT_EQ(u16(32), 2u);       // block align
    EXPECT_EQ(u16(34), 16u);      // bits
    EXPECT_EQ(std::memcmp(b.data() + 36, "data", 4), 0);
    const uint32_t dataBytes = u32(40);
    EXPECT_EQ(dataBytes, b.size() - 44);
    const double seconds = double(dataBytes) / 2.0 / 48000.0;
    EXPECT_NEAR(seconds, 1.5, 0.05);
    // Not silence: the noise was recorded.
    int nonZero = 0;
    for (size_t i = 44; i + 1 < b.size(); i += 2) nonZero += u16(i) != 0;
    EXPECT_GT(nonZero, int(dataBytes / 4));
    std::filesystem::remove(path);
}

TEST(Session, RecordSampleFailureReportsError) {
    FakeBackend::Options o;
    o.speed = 20;
    o.backpressure = true;
    Session s(std::make_unique<FakeBackend>(o));
    std::string err;
    ASSERT_TRUE(s.start(training(), err)) << err;
    s.recordSample("/nonexistent-dir/amostra.wav", 0.2);
    Collector c{s, {}};
    ASSERT_TRUE(c.waitFor([&] { return c.count(UiEvent::Error) == 1; }));
    EXPECT_NE(c.of(UiEvent::Error)[0].text.find("amostra"), std::string::npos);
    s.stop();
}

TEST(Session, AudioThreadsNeverBlockOnUi) {
    FakeBackend::Options o;
    o.speed = 10;
    o.backpressure = false;  // a stalled analysis thread would overflow the input ring (xrun)
    o.noiseSigma = 0.002;
    auto fake = std::make_unique<FakeBackend>(o);
    FakeBackend* f = fake.get();
    Session s(std::move(fake));
    const SessionConfig cfg = training(120);
    ClickScheduler cs(kSr, cfg.bpm, cfg.countIn);
    const double t0 = f->nowSec();
    for (int k = 0; k < 6; ++k) f->scheduleNote(beatTime(cs, t0, cfg.countIn + k, o.outputLatencySec), 55.0, 0.3);
    std::string err;
    ASSERT_TRUE(s.start(cfg, err)) << err;
    // The UI does not poll for 7 simulated seconds (the 6th note is at t0 + 5.58 s).
    ASSERT_TRUE(waitUntil([&] { return f->nowSec() > t0 + 7.0; }));
    EXPECT_EQ(f->xruns(), 0u);
    Collector c{s, {}};
    c.poll();
    // Level events were dropped (7 s at ~20 Hz = ~140 > queue capacity of 64) but the scoring
    // events all arrived: the producers kept running while nobody consumed.
    const size_t levels = c.count(UiEvent::Level);
    EXPECT_GT(levels, 0u);
    EXPECT_LT(levels, 100u);  // 64 queued + the few written while this poll drains
    EXPECT_EQ(c.count(UiEvent::Note), 6u);
    EXPECT_GE(c.count(UiEvent::Beat), 12u);  // beats audible up to t0 + 7.3 s: 1.08 + 0.5 n
    s.stop();
    EXPECT_EQ(f->xruns(), 0u);
}

TEST(Session, RejectsInvalidConfig) {
    Session s(std::make_unique<FakeBackend>(FakeBackend::Options{}));
    std::string err;
    SessionConfig c = training();
    c.subdivision = 3;
    EXPECT_FALSE(s.start(c, err));
    EXPECT_FALSE(err.empty());
    c = training();
    c.mode = Mode::Tuner;
    c.instrument = nullptr;
    err.clear();
    EXPECT_FALSE(s.start(c, err));
    EXPECT_FALSE(err.empty());
    std::vector<UiEvent> ev;
    s.pollEvents(ev);
    EXPECT_TRUE(ev.empty());  // no session, no Stopped
    s.stop();
}
