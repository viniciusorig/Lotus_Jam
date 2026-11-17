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
// Gap detection. `droppedSoFar` is the backend's cumulative dropped-frame counter read AFTER
// the point was taken from the ring; `pending` = dropped frames not yet matched to a jump. A
// jump g (frames) between consecutive points is a gap only while pending > 0 and
//   min(kMinGapFrames, pending / 2) <= g <= pending + kMinGapFrames,
// and it consumes min(g, pending). Without pending drops, timestamp jitter never splits. A
// drop too small for its own jump to be told from jitter is matched by the next compatible
// jump (at worst a spurious split that consumes it), so `pending` always drains and cannot
// arm later jitter for the rest of the session.
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

    // Adds the next input TimePoint; true if it starts a new segment (a gap was detected).
    bool add(TimePoint p, uint64_t droppedSoFar) {
        if (segs_.empty()) {
            segs_.push_back(Segment{std::numeric_limits<int64_t>::min(), p, ClockMap()});
            segs_.back().map.add(p);
            last_ = p;
            return false;
        }
        const double pending = droppedSoFar > accounted_ ? double(droppedSoFar - accounted_) : 0.0;
        const double gap = (p.tSec - last_.tSec) * sr_ - double(p.frame - last_.frame);
        const double minGap = std::max(0.5, std::min(kMinGapFrames, 0.5 * pending));
        bool split = false;
        if (pending > 0.0 && gap >= minGap && gap <= pending + kMinGapFrames) {
            accounted_ += uint64_t(std::llround(std::min(gap, pending)));
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
    static constexpr double kMinGapFrames = 24.0;  // 0.5 ms at 48 kHz
    struct Segment {
        int64_t start;  // first frame belonging to this segment
        TimePoint first;
        ClockMap map;
    };
    double sr_;
    std::deque<Segment> segs_;
    TimePoint last_{0, 0.0};
    uint64_t accounted_ = 0;
    uint64_t evictions_ = 0;
};
