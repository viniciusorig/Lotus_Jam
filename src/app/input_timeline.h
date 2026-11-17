#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>

#include "core/clock_map.h"

// Input frame -> capture time mapping that survives dropped input (scoring thread only).
//
// The backend's input TimePoints count frames actually written to input(), so frames and
// onset indices share one domain; when the backend drops frames, the next TimePoint's time
// jumps forward by the dropped duration. A single least-squares fit across that jump would be
// poisoned, so the timeline is split into continuous segments, each with its own ClockMap.
// A new segment starts at the previous point's frame (the dropped frames lie between the two
// points). Until a segment's ClockMap is ready its times use the nominal rate from the
// segment's first point.
//
// Gap detection. `droppedSoFar` is the backend's cumulative dropped-frame counter, read AFTER
// the batch of points was taken from the ring; `pending` = observed dropped frames not yet
// matched to a jump. A jump g (frames) between consecutive points is a gap when
//   min(g, pending) >= kMinGapFrames  and  g <= pending + kJitterFrames,
// and it consumes min(g, pending). The measured jump of a drop is drop + (jitter after -
// jitter before), so kJitterFrames (5 ms) allows for several ms of timestamp jitter; without
// pending drops jitter never splits. Drops too small to locate (< kMinGapFrames, 0.5 ms) never
// split: they are absorbed into the fit (error <= their size). Pending frames still unmatched
// once a point captured more than kMatchSec after the observation (backend time, `nowSec`
// passed with the batch) has been processed are absorbed too (a small drop, or the
// jitter-hidden remainder of a matched one), so `pending` always drains and cannot arm later
// jitter for long. This is safe whatever the batching or thread preemption: the increment
// happened before the observation and the post-drop TimePoint is pushed at most one period
// after the increment, so its capture time is before observation + one period.
//
// Memory and pruning. The scoring thread reads TimePoints up to the backend's head while the
// analysis (and so the onsets) can lag by the whole input ring, so many gaps may lie between
// the analysed position and the head. Segments are therefore never dropped by count while
// the analysis may still need them: prune(analysed) drops only segments that end more than
// kKeepFrames before the analysed position. As a hard memory bound at most kMaxSegments are
// kept; beyond that the oldest is evicted (evictions() counts them). timeOf() returns NaN for
// a frame before the oldest kept segment (pruned or evicted): such a frame is refused, never
// extrapolated through missing gaps.
class InputTimeline {
public:
    static constexpr int64_t kKeepFrames = 24000;  // 0.5 s behind the analysed position
    static constexpr size_t kMaxSegments = 64;     // each holds a <= 300-point ClockMap

    explicit InputTimeline(double sampleRate) : sr_(sampleRate) {}

    // Adds a batch of consecutive input TimePoints read from the ring, with the dropped-frame
    // counter and the backend clock read after that read. Returns how many new segments were
    // started.
    size_t add(const TimePoint* p, size_t n, uint64_t droppedSoFar, double nowSec) {
        observed_ = std::max(observed_, droppedSoFar);
        size_t splits = 0;
        for (size_t i = 0; i < n; ++i) {
            splits += addOne(p[i]) ? 1 : 0;
            // Absorb amounts whose post-drop point must have been seen by now.
            while (!obs_.empty() && p[i].tSec > obs_.front().tSec + kMatchSec) {
                accounted_ = std::max(accounted_, obs_.front().value);
                obs_.pop_front();
            }
        }
        if (observed_ > accounted_ && (obs_.empty() || observed_ > obs_.back().value)) {
            if (obs_.size() == kMaxObs) obs_.pop_front();  // bound; the newest value covers it
            obs_.push_back({observed_, nowSec});
        }
        return splits;
    }
    // One point read on its own, observed at its own capture time; true if it splits.
    bool add(TimePoint p, uint64_t droppedSoFar) { return add(&p, 1, droppedSoFar, p.tSec) > 0; }

    // Drops segments that end more than kKeepFrames before `analysedFrame`.
    void prune(int64_t analysedFrame) {
        while (segs_.size() > 1 && segs_[1].start <= analysedFrame - kKeepFrames) segs_.pop_front();
    }

    bool empty() const { return segs_.empty(); }
    size_t segments() const { return segs_.size(); }
    uint64_t evictions() const { return evictions_; }

    // Capture time of input frame `frame`; NaN when empty or when the frame lies before the
    // oldest kept segment.
    double timeOf(double frame) const {
        if (segs_.empty() || frame < double(segs_.front().start)) return std::numeric_limits<double>::quiet_NaN();
        const Segment* s = &segs_.front();
        for (auto it = segs_.rbegin(); it != segs_.rend(); ++it)
            if (double(it->start) <= frame) {
                s = &*it;
                break;
            }
        if (s->map.ready()) return s->map.timeOf(frame);
        return s->first.tSec + (frame - double(s->first.frame)) / sr_;
    }

private:
    bool addOne(TimePoint p) {
        if (segs_.empty()) {
            segs_.push_back(Segment{std::numeric_limits<int64_t>::min(), p, ClockMap()});
            segs_.back().map.add(p);
            last_ = p;
            return false;
        }
        const double pending = observed_ > accounted_ ? double(observed_ - accounted_) : 0.0;
        const double gap = (p.tSec - last_.tSec) * sr_ - double(p.frame - last_.frame);
        const double match = std::min(gap, pending);
        bool split = false;
        if (match >= kMinGapFrames && gap <= pending + kJitterFrames) {
            accounted_ += uint64_t(std::llround(match));
            segs_.push_back(Segment{last_.frame, p, ClockMap()});
            if (segs_.size() > kMaxSegments) {
                segs_.pop_front();
                ++evictions_;
            }
            split = true;
        }
        segs_.back().map.add(p);
        last_ = p;
        return split;
    }

    static constexpr double kMinGapFrames = 24.0;   // 0.5 ms at 48 kHz
    static constexpr double kJitterFrames = 240.0;  // 5 ms timestamp-jitter allowance
    static constexpr double kMatchSec = 0.05;       // > one period + timestamp jitter
    static constexpr size_t kMaxObs = 64;
    struct Segment {
        int64_t start;  // first frame belonging to this segment
        TimePoint first;
        ClockMap map;
    };
    struct Obs {
        uint64_t value;  // counter value observed with a batch
        double tSec;     // backend time of that observation
    };
    double sr_;
    std::deque<Segment> segs_;
    std::deque<Obs> obs_;  // <= kMaxObs entries
    TimePoint last_{0, 0.0};
    uint64_t observed_ = 0, accounted_ = 0, evictions_ = 0;
};
