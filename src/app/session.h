#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/input_timeline.h"
#include "audio/backend.h"
#include "audio/click_scheduler.h"
#include "core/clock_map.h"
#include "core/ring_buffer.h"
#include "dsp/midi_parser.h"
#include "dsp/notes.h"
#include "dsp/onset.h"
#include "dsp/tuner.h"
#include "score/grid_scorer.h"
#include "score/note_scorer.h"

enum class Mode { Training, Tuner, Music };

struct SessionConfig {
    Mode mode = Mode::Training;
    double bpm = 120;           // 30..240
    int countIn = 4;            // beats, >= 0
    int subdivision = 1;        // 1, 2 or 4 scored slots per beat
    double compensationMs = 0;  // latency compensation subtracted from played times
    double gainDb = 0;          // digital input gain (analysis only; recordings are raw)
    const Instrument* instrument = nullptr;  // required in Tuner and Music modes
    Tuning tuning;                           // required in Tuner mode
    double a4 = 440;  // fixed for the session: changing A4/instrument/tuning needs a restart
    // Music mode
    std::vector<MidiNote> songNotes;  // notes to practice (from selected MIDI track)
    double speedFactor = 1.0;         // 0.5 = 50% speed, 1.0 = normal
};

// Event for the UI thread.
//   Beat:   n = beat index from 0 (count-in beats first), a = audible time on the backend
//           clock (the UI computes a - backend.nowSec()). Emitted up to 0.3 s ahead.
//   Note:   n = slot, a = error ms (positive = late), b = score 0..100.
//   Miss:   n = slot.
//   Extra:  n = nearest slot, a = error ms.
//   Tuner:  tuner = reading.
//   Level:  a = input peak 0..1 (after gain), ~20 Hz. Dropped when the queue is full.
//   Error:  text = pt-BR message.
//   Stopped: emitted exactly once per session end (stop() or device failure), and it is the
//           last event of that session: pollEvents never delivers an event of that session
//           after it, nor an event of the next session before it. Exception: the Error of a
//           failed recordSample() WAV write comes from a writer thread that outlives the
//           session, so it can arrive after that session's Stopped.
//   Warning: text = pt-BR message for a non-fatal problem (input overload); the session goes on.
//   MusicNote: Music mode - n = note index, a = error ms, b = score, noteKind = NoteResult::Kind.
//   MusicTime: Music mode - a = current playback time in seconds (for UI sync).
struct UiEvent {
    enum Type { Beat, Note, Miss, Extra, Tuner, Level, Error, Stopped, Warning, MusicNote, MusicTime } type = Beat;
    int64_t n = 0;
    double a = 0, b = 0;
    TunerReading tuner{};
    std::string text;
    int noteKind = 0;  // NoteResult::Kind for MusicNote events
};

// One training/tuner session on top of an IAudioBackend (reused across start/stop cycles).
//
// Threads:
//  - backend callbacks (real time): only copy/generate audio and push TimePoints;
//  - analysis: reads backend.input(), applies the gain, feeds the OnsetDetector (Training)
//    or the Tuner (Tuner), emits Level (~20 Hz) and Tuner events, forwards onsets to the
//    scoring thread through an SPSC ring, copies raw input for recordSample();
//  - scoring: owns the two ClockMaps (input/output), the GridScorer and the beat timeline;
//    maps onsets to backend time, emits Beat/Note/Miss/Extra, watches backend.failed().
// Each worker thread has its own SPSC event queue (never blocks: Level/Tuner/other events
// are dropped when full, a reserve keeps room for Error and Stopped); pollEvents drains both.
// Dropped input (backend.inputDroppedFrames()) does not break the timing: the input timeline
// is split at the gap (see InputTimeline; segments are pruned behind the analysed position,
// and if its 64-segment cap is ever hit, onsets in evicted frames are dropped, never
// mistimed) and one Warning is emitted per overload episode (an episode ends after 5 s of
// backend time without drops or evictions). The OnsetDetector is not reset: its
// sample indices stay in the written-frames domain (a note cut by the gap may be missed).
//
// start/stop/pollEvents/recordSample must be called from one controlling (UI) thread;
// summary() is thread-safe. The destructor stops and joins everything.
class Session {
public:
    explicit Session(std::unique_ptr<IAudioBackend> backend);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Stops a running session first. Returns false with a pt-BR message on invalid config
    // or when the backend cannot start (no Stopped event is emitted then).
    bool start(const SessionConfig& cfg, std::string& err);
    // Idempotent. Stops the backend, joins the worker threads and emits Stopped (unless a
    // device failure already did). Does not wait for a pending WAV write.
    void stop();
    // Appends the pending events to `out`; true if any was appended.
    bool pollEvents(std::vector<UiEvent>& out);
    // Snapshot refreshed by the scoring thread at most every 250 ms of wall time (also while
    // the backend clock is paused) and at stop.
    GridScorer::Summary summary() const;
    // Records `seconds` of raw input (16-bit mono 48 kHz WAV at wavPath), starting with the
    // next input block of the running session; ignored when no session runs. A session that
    // stops early writes what was recorded so far; a request not yet started when the session
    // stops is discarded. A write failure is reported as an Error event.
    void recordSample(const std::string& wavPath, double seconds);

private:
    struct Recording {
        std::string path;
        std::vector<float> buf;
        size_t filled = 0;
    };
    struct Writer {
        std::thread thread;
        std::atomic<bool> done{false};
    };

    void analysisLoop();
    void scoringLoop();
    void finishRecording();  // analysis thread
    void reapWriters(bool all);
    bool pushScoring(const UiEvent& e, bool reserved);
    bool pushAnalysis(const UiEvent& e);
    void refreshSummary();
    double slotTime(int64_t slot) const;
    double beatFrame(int64_t beat) const;

    std::unique_ptr<IAudioBackend> backend_;
    SessionConfig cfg_;
    bool active_ = false;  // controlling thread only

    // Per-session DSP/scoring state (built in start(), then owned by one worker thread).
    std::unique_ptr<ClickScheduler> clicks_;
    std::unique_ptr<OnsetDetector> onset_;
    std::unique_ptr<::Tuner> tuner_;
    std::unique_ptr<GridScorer> scorer_;
    std::unique_ptr<NoteScorer> noteScorer_;  // Music mode
    std::unique_ptr<InputTimeline> inTimeline_;  // input frame -> capture time, gap aware
    std::unique_ptr<ClockMap> outMap_;
    double gain_ = 1.0;
    double songStartSec_ = 0.0;  // Music mode: when the song started (backend time)

    std::thread analysis_, scoring_;
    std::atomic<bool> stopReq_{false};
    std::atomic<bool> analysisDone_{false};
    std::atomic<bool> stoppedEmitted_{false};
    std::atomic<int64_t> analysedFrames_{0};

    SpscRing<int64_t> onsets_{1024};
    SpscRing<DetectedNote> detectedNotes_{1024};  // Music mode: analysis -> scoring
    SpscRing<UiEvent> anaEvents_{64};
    SpscRing<UiEvent> scoEvents_{4096};
    static constexpr size_t kReserve = 8;
    std::vector<UiEvent> carry_;  // controller: previous session's undelivered scoring events

    // Recording hand-off: controlling thread -> analysis thread.
    std::atomic<Recording*> recReq_{nullptr};
    std::unique_ptr<Recording> rec_;  // analysis thread
    std::vector<std::unique_ptr<Writer>> writers_;  // analysis thread while running, else controller
    std::mutex failMutex_;
    std::vector<std::string> failedWrites_;

    mutable std::mutex summaryMutex_;
    GridScorer::Summary summary_{};
};
