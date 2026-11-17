#pragma once
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
//
// A jump is only treated as a gap while the backend reports dropped frames not yet accounted
// for (`droppedSoFar`, read AFTER the point was taken from the ring) and when it exceeds
// kMinGapFrames (0.5 ms): wake-up jitter of the timestamps never splits the timeline, and a
// drop shorter than 0.5 ms is absorbed by the fit (error below 0.5 ms). A new segment starts
// at the previous point's frame (the dropped frames lie between the two points). Until a
// segment's ClockMap is ready its times use the nominal rate from the segment's first point.
// At most kMaxSegments are kept (older frames use the oldest kept segment).
class InputTimeline {
public:
    explicit InputTimeline(double sampleRate) : sr_(sampleRate) {}

    // Adds the next input TimePoint; true if it starts a new segment (a gap was detected).
    bool add(TimePoint p, uint64_t droppedSoFar) {
        if (segs_.empty()) {
            segs_.push_back(Segment{std::numeric_limits<int64_t>::min(), p, ClockMap()});
            segs_.back().map.add(p);
            last_ = p;
            return false;
        }
        const uint64_t pending = droppedSoFar > accounted_ ? droppedSoFar - accounted_ : 0;
        const double gapFrames = (p.tSec - last_.tSec) * sr_ - double(p.frame - last_.frame);
        bool split = false;
        if (pending > 0 && gapFrames > kMinGapFrames) {
            const uint64_t gap = uint64_t(std::llround(gapFrames));
            accounted_ += gap < pending ? gap : pending;
            segs_.push_back(Segment{last_.frame, p, ClockMap()});
            if (segs_.size() > kMaxSegments) segs_.pop_front();
            split = true;
        }
        segs_.back().map.add(p);
        last_ = p;
        return split;
    }

    bool empty() const { return segs_.empty(); }
    size_t segments() const { return segs_.size(); }

    // Capture time of input frame `frame`. Requires !empty().
    double timeOf(double frame) const {
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
    static constexpr size_t kMaxSegments = 4;
    struct Segment {
        int64_t start;  // first frame belonging to this segment
        TimePoint first;
        ClockMap map;
    };
    double sr_;
    std::deque<Segment> segs_;
    TimePoint last_{0, 0.0};
    uint64_t accounted_ = 0;
};
