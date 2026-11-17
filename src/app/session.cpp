#include "app/session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

#include "app/wav_writer.h"

namespace {
constexpr double kSampleRate = 48000.0;
constexpr int64_t kLevelEvery = 2400;  // input frames per Level event (~20 Hz)
constexpr double kBeatLeadSec = 0.3;   // Beat events are emitted this far ahead
constexpr double kSummaryEverySec = 0.25;
constexpr double kWarnEverySec = 5.0;
const char* const kOverloadText = "Entrada de áudio sobrecarregada: algumas notas podem ter sido ignoradas.";
constexpr size_t kMaxRecordFrames = size_t(600 * kSampleRate);  // 10 min cap
constexpr auto kAnalysisIdle = std::chrono::milliseconds(1);
constexpr auto kScoringTick = std::chrono::milliseconds(2);

template <class T>
void drain(SpscRing<T>& r) {
    std::array<T, 256> tmp;
    while (r.read(tmp.data(), tmp.size()) > 0) {
    }
}

bool validate(const SessionConfig& c, std::string& err) {
    if (!std::isfinite(c.bpm) || c.bpm < 30.0 || c.bpm > 240.0) {
        err = "O andamento deve estar entre 30 e 240 BPM.";
        return false;
    }
    if (c.countIn < 0 || c.countIn > 64) {
        err = "Contagem inválida.";
        return false;
    }
    if (c.subdivision != 1 && c.subdivision != 2 && c.subdivision != 4) {
        err = "A subdivisão deve ser 1, 2 ou 4 notas por batida.";
        return false;
    }
    if (!std::isfinite(c.compensationMs) || std::fabs(c.compensationMs) > 1000.0) {
        err = "Compensação de latência inválida.";
        return false;
    }
    if (!std::isfinite(c.gainDb) || std::fabs(c.gainDb) > 60.0) {
        err = "Ganho de entrada inválido.";
        return false;
    }
    if (c.mode == Mode::Tuner) {
        if (!c.instrument || c.tuning.strings.empty()) {
            err = "Escolha um instrumento e uma afinação.";
            return false;
        }
        if (!std::isfinite(c.a4) || c.a4 < 400.0 || c.a4 > 480.0) {
            err = "A referência do Lá (A4) deve estar entre 400 e 480 Hz.";
            return false;
        }
    }
    return true;
}
}  // namespace

Session::Session(std::unique_ptr<IAudioBackend> backend) : backend_(std::move(backend)) {}

Session::~Session() {
    stop();
    reapWriters(true);
    delete recReq_.exchange(nullptr);
}

bool Session::start(const SessionConfig& cfg, std::string& err) {
    stop();
    if (!validate(cfg, err)) return false;
    reapWriters(false);
    cfg_ = cfg;

    // The backend is stopped and no worker runs: this thread may consume the rings to throw
    // away whatever the previous session left behind.
    drain(backend_->input());
    drain(backend_->inputTimes());
    drain(backend_->outputTimes());
    drain(onsets_);
    // Previous session's events: Level/Tuner leftovers are stale and dropped; scoring leftovers
    // (ending with its Stopped) are kept, ahead of anything of the new session.
    drain(anaEvents_);
    {
        UiEvent e;
        while (scoEvents_.read(&e, 1) == 1) carry_.push_back(std::move(e));
    }

    gain_ = std::pow(10.0, cfg.gainDb / 20.0);
    inTimeline_ = std::make_unique<InputTimeline>(kSampleRate);
    outMap_ = std::make_unique<ClockMap>();
    clicks_.reset();
    onset_.reset();
    tuner_.reset();
    scorer_.reset();
    if (cfg.mode == Mode::Training) {
        clicks_ = std::make_unique<ClickScheduler>(kSampleRate, cfg.bpm, cfg.countIn);
        onset_ = std::make_unique<OnsetDetector>(kSampleRate);
        const double step = 60.0 / cfg.bpm / cfg.subdivision;
        scorer_ = std::make_unique<GridScorer>(step, cfg.compensationMs / 1000.0,
                                               [this](int64_t k) { return slotTime(k); });
        ClickScheduler* cs = clicks_.get();
        backend_->setRenderer([cs](float* out, size_t frames, int64_t first) { cs->render(out, frames, first); });
    } else {
        // A4/instrument/tuning are fixed at Tuner construction: changing them needs a restart.
        tuner_ = std::make_unique<::Tuner>(kSampleRate, *cfg.instrument, cfg.tuning, cfg.a4);
        backend_->setRenderer([](float* out, size_t frames, int64_t) { std::fill(out, out + 2 * frames, 0.0f); });
    }
    {
        std::lock_guard<std::mutex> lk(summaryMutex_);
        summary_ = GridScorer::Summary{0, 0, 0, 0, 0, 0, 0};
    }
    stopReq_.store(false);
    analysisDone_.store(false);
    stoppedEmitted_.store(false);
    analysedFrames_.store(0);

    if (!backend_->start(err)) {
        backend_->setRenderer(nullptr);
        return false;
    }
    active_ = true;
    analysis_ = std::thread([this] { analysisLoop(); });
    scoring_ = std::thread([this] { scoringLoop(); });
    return true;
}

void Session::stop() {
    if (!active_) return;
    // Backend first: the callbacks stop producing before their consumers go away.
    backend_->stop();
    stopReq_.store(true, std::memory_order_release);
    analysis_.join();
    scoring_.join();
    backend_->setRenderer(nullptr);
    delete recReq_.exchange(nullptr, std::memory_order_acq_rel);  // never started: cancelled
    // The scoring thread is gone, so this thread is now the only producer of scoEvents_.
    if (!stoppedEmitted_.load(std::memory_order_acquire)) {
        UiEvent e;
        e.type = UiEvent::Stopped;
        pushScoring(e, true);
    }
    active_ = false;
}

bool Session::pollEvents(std::vector<UiEvent>& out) {
    const size_t before = out.size();
    for (auto& c : carry_) out.push_back(std::move(c));
    carry_.clear();
    UiEvent e;
    while (anaEvents_.read(&e, 1) == 1) out.push_back(std::move(e));
    while (scoEvents_.read(&e, 1) == 1) {
        if (e.type == UiEvent::Stopped) {
            // Every analysis event of this session was written before Stopped (the analysis
            // thread had finished), and no next session can be running here (start() moves a
            // pending Stopped to carry_): drain them now so that Stopped is last.
            UiEvent a;
            while (anaEvents_.read(&a, 1) == 1) out.push_back(std::move(a));
        }
        out.push_back(std::move(e));
    }
    std::vector<std::string> failed;
    {
        std::lock_guard<std::mutex> lk(failMutex_);
        failed.swap(failedWrites_);
    }
    for (auto& path : failed) {
        UiEvent f;
        f.type = UiEvent::Error;
        f.text = "Não foi possível gravar a amostra em " + path + ".";
        out.push_back(std::move(f));
    }
    return out.size() > before;
}

GridScorer::Summary Session::summary() const {
    std::lock_guard<std::mutex> lk(summaryMutex_);
    return summary_;
}

void Session::recordSample(const std::string& wavPath, double seconds) {
    if (!active_ || !(seconds > 0.0)) return;
    const size_t n = std::min(kMaxRecordFrames, size_t(std::llround(seconds * kSampleRate)));
    if (n == 0) return;
    auto* r = new Recording{wavPath, std::vector<float>(n), 0};
    delete recReq_.exchange(r, std::memory_order_acq_rel);  // replaces a request not yet taken
}

bool Session::pushScoring(const UiEvent& e, bool reserved) {
    if (!reserved && scoEvents_.writable() <= kReserve) return false;  // dropped, never blocks
    return scoEvents_.write(&e, 1) == 1;
}

bool Session::pushAnalysis(const UiEvent& e) { return anaEvents_.write(&e, 1) == 1; }

double Session::beatFrame(int64_t beat) const {
    // Same rounding as ClickScheduler: beat n starts at round(firstBeat + n * framesPerBeat).
    return double(std::llround(double(clicks_->firstBeatFrame()) + double(beat) * clicks_->framesPerBeat()));
}

double Session::slotTime(int64_t slot) const {
    const double beats = double(cfg_.countIn) + double(slot) / double(cfg_.subdivision);
    const double frame = double(clicks_->firstBeatFrame()) + beats * clicks_->framesPerBeat();
    return outMap_->timeOf(frame);
}

void Session::refreshSummary() {
    if (!scorer_) return;
    const GridScorer::Summary s = scorer_->summary();
    std::lock_guard<std::mutex> lk(summaryMutex_);
    summary_ = s;
}

// ---------------------------------------------------------------- analysis thread

void Session::analysisLoop() {
    constexpr size_t kChunk = 4096;
    std::vector<float> raw(kChunk), x(kChunk);
    std::vector<int64_t> found;
    found.reserve(64);
    SpscRing<float>& in = backend_->input();
    const bool training = cfg_.mode == Mode::Training;
    int64_t frames = 0, sinceLevel = 0;
    float peak = 0.0f;

    while (!stopReq_.load(std::memory_order_acquire)) {
        const size_t n = in.read(raw.data(), kChunk);
        if (n == 0) {
            std::this_thread::sleep_for(kAnalysisIdle);
            continue;
        }
        // Raw input for recordSample().
        if (!rec_) rec_.reset(recReq_.exchange(nullptr, std::memory_order_acq_rel));
        if (rec_) {
            const size_t m = std::min(n, rec_->buf.size() - rec_->filled);
            std::copy(raw.begin(), raw.begin() + long(m), rec_->buf.begin() + long(rec_->filled));
            rec_->filled += m;
            if (rec_->filled == rec_->buf.size()) finishRecording();
        }
        for (size_t i = 0; i < n; ++i) {
            x[i] = float(double(raw[i]) * gain_);
            peak = std::max(peak, std::fabs(x[i]));
            if (++sinceLevel == kLevelEvery) {
                UiEvent e;
                e.type = UiEvent::Level;
                e.a = std::min(1.0f, peak);
                pushAnalysis(e);
                peak = 0.0f;
                sinceLevel = 0;
            }
        }
        if (training) {
            found.clear();
            onset_->feed(x.data(), n, found);
            onsets_.write(found.data(), found.size());  // a full ring drops onsets (never waits)
        } else {
            tuner_->feed(x.data(), n);
            TunerReading r;
            while (tuner_->poll(r)) {
                UiEvent e;
                e.type = UiEvent::Tuner;
                e.tuner = r;
                pushAnalysis(e);
            }
        }
        frames += int64_t(n);
        // Release: every onset up to `frames` is in onsets_ before this store is seen.
        analysedFrames_.store(frames, std::memory_order_release);
    }
    if (rec_ && rec_->filled > 0) finishRecording();  // stopped early: keep what was recorded
    rec_.reset();
    analysisDone_.store(true, std::memory_order_release);
}

void Session::finishRecording() {
    reapWriters(false);
    rec_->buf.resize(rec_->filled);
    auto w = std::make_unique<Writer>();
    Writer* wp = w.get();
    wp->thread = std::thread([this, wp, r = std::move(rec_)] {
        if (!writeWav16Mono(r->path, r->buf.data(), r->buf.size(), int(kSampleRate))) {
            std::lock_guard<std::mutex> lk(failMutex_);
            failedWrites_.push_back(r->path);
        }
        wp->done.store(true, std::memory_order_release);
    });
    writers_.push_back(std::move(w));
}

// Called by the analysis thread while it runs, otherwise by the controlling thread.
void Session::reapWriters(bool all) {
    for (auto it = writers_.begin(); it != writers_.end();) {
        if (all || (*it)->done.load(std::memory_order_acquire)) {
            (*it)->thread.join();
            it = writers_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- scoring thread

void Session::scoringLoop() {
    std::array<TimePoint, 64> tp;
    std::array<int64_t, 64> ons;
    std::vector<ScoreEvent> misses;
    misses.reserve(64);
    std::string err;
    const bool training = cfg_.mode == Mode::Training;
    int64_t nextBeat = 0;
    double lastRefresh = -std::numeric_limits<double>::infinity();
    double lastWarn = -std::numeric_limits<double>::infinity();
    uint64_t warnedDropped = 0;
    bool dirty = true;
    auto emit = [this](UiEvent::Type t, int64_t n, double a, double b) {
        UiEvent e;
        e.type = t;
        e.n = n;
        e.a = a;
        e.b = b;
        pushScoring(e, false);
    };

    while (!stopReq_.load(std::memory_order_acquire)) {
        if (backend_->failed(err)) {
            stopReq_.store(true, std::memory_order_release);
            // Let the analysis thread finish so that nothing is emitted after Stopped.
            while (!analysisDone_.load(std::memory_order_acquire)) std::this_thread::sleep_for(kAnalysisIdle);
            UiEvent e;
            e.type = UiEvent::Error;
            e.text = err;
            pushScoring(e, true);
            e.type = UiEvent::Stopped;
            e.text.clear();
            pushScoring(e, true);
            stoppedEmitted_.store(true, std::memory_order_release);
            break;
        }
        size_t n;
        while ((n = backend_->inputTimes().read(tp.data(), tp.size())) > 0) {
            // Read the drop counter AFTER the points: it then covers every drop before them.
            const uint64_t dropped = backend_->inputDroppedFrames();
            for (size_t i = 0; i < n; ++i) inTimeline_->add(tp[i], dropped);
        }
        const uint64_t dropped = backend_->inputDroppedFrames();
        if (dropped > warnedDropped) {
            // One Warning per overload episode: drops within 5 s of the last Warning are
            // covered by it.
            const double now = backend_->nowSec();
            if (now - lastWarn >= kWarnEverySec) {
                UiEvent w;
                w.type = UiEvent::Warning;
                w.text = kOverloadText;
                pushScoring(w, false);
            }
            lastWarn = now;
            warnedDropped = dropped;
        }
        while ((n = backend_->outputTimes().read(tp.data(), tp.size())) > 0)
            for (size_t i = 0; i < n; ++i) outMap_->add(tp[i]);

        if (training) {
            const bool ready = !inTimeline_->empty() && outMap_->ready();
            // Load the progress BEFORE draining: every onset up to `analysed` is then drained.
            const int64_t analysed = analysedFrames_.load(std::memory_order_acquire);
            while ((n = onsets_.read(ons.data(), ons.size())) > 0) {
                for (size_t i = 0; i < n; ++i) {
                    if (!ready) continue;  // only before the output ClockMap is ready (< 0.5 s)
                    const double t = inTimeline_->timeOf(double(ons[i]));
                    if (!std::isfinite(t)) continue;
                    if (auto ev = scorer_->onNote(t)) {
                        emit(ev->kind == ScoreEvent::Hit ? UiEvent::Note : UiEvent::Extra, ev->slot, ev->errorMs, ev->score);
                        dirty = true;
                    }
                }
            }
            if (ready) {
                // "Now" for finalisation = capture time of the last analysed frame, so a slot
                // is never finalised before its onset could have been analysed.
                misses.clear();
                scorer_->advance(inTimeline_->timeOf(double(analysed)), misses);
                for (const auto& m : misses) emit(UiEvent::Miss, m.slot, 0.0, 0.0);
                dirty = dirty || !misses.empty();
            }
            if (outMap_->ready()) {
                const double horizon = backend_->nowSec() + kBeatLeadSec;
                for (int guard = 0; guard < 64; ++guard) {
                    const double a = outMap_->timeOf(beatFrame(nextBeat));
                    if (!(a <= horizon)) break;
                    emit(UiEvent::Beat, nextBeat, a, 0.0);
                    ++nextBeat;
                }
            }
            const double now = backend_->nowSec();
            if (dirty && now - lastRefresh >= kSummaryEverySec) {
                refreshSummary();
                lastRefresh = now;
                dirty = false;
            }
        }
        std::this_thread::sleep_for(kScoringTick);
    }
    refreshSummary();
}
