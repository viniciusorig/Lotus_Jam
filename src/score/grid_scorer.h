#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

struct ScoreEvent {
    enum Kind { Hit, Miss, Extra } kind;
    int64_t slot;
    double errorMs;  // positive = late; 0 for Miss
    double score;    // 0..100
};

// Audible time (s) of grid slot k >= 0.
using SlotTimeFn = std::function<double(int64_t slot)>;

// Scores played notes against a metronome grid. C++ port of the prototype's
// _onset/_finaliza/_janela. Pure and single-threaded (owned by the scoring thread).
//
// stepSec must be > 0 (asserted; in release builds it is clamped to 1 ms so that no
// division by zero or NaN can occur). Window = min(0.5*step, 0.2) s. The played time is
// shifted by -compensationSec before matching. The slot is estimated as
// round((t-comp-slotTime(0))/step) and the error is measured against the real slotTime(k).
class GridScorer {
public:
    GridScorer(double stepSec, double compensationSec, SlotTimeFn slotTime);

    // Hit, or Extra (outside window / slot already taken or finalised); nullopt for k < 0
    // (count-in notes are ignored).
    std::optional<ScoreEvent> onNote(double tSec);

    // Finalises, in order, every slot with slotTime+window+0.1 < now-comp; un-hit slots
    // become Miss events appended to missesOut. Each slot is finalised exactly once.
    void advance(double nowSec, std::vector<ScoreEvent>& missesOut);

    struct Summary {
        double total, lastN;  // mean score over all / last N finalised slots (0 if none)
        int hits, slots, extras;
        double meanErrMs, stdErrMs;  // over hits, population std (0 if no hits)
    };
    Summary summary(size_t lastN = 16) const;

    double window() const { return window_; }

private:
    struct Slot {
        bool hit;
        double errMs, score;
    };
    double step_, comp_, window_;
    SlotTimeFn slotTime_;
    int64_t nextFinal_ = 0;
    int extras_ = 0;
    std::deque<Slot> done_;          // finalised slots, in order
    std::vector<int64_t> pendingSlot_;  // slots hit but not yet finalised (small)
    std::vector<Slot> pending_;
};
