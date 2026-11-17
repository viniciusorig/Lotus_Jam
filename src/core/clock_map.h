#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

struct TimePoint {
    int64_t frame;
    double tSec;
};

// Maps audio sample frames <-> wall-clock seconds using a least-squares
// linear fit over the most recent points. Single-threaded (scoring thread).
class ClockMap {
public:
    explicit ClockMap(size_t maxPoints = 300);

    void add(TimePoint p);
    bool ready() const { return points_.size() >= kMinPoints; }
    double timeOf(double frame) const;
    double frameAt(double tSec) const;

private:
    static constexpr size_t kMinPoints = 20;
    static constexpr size_t kRefitEvery = 25;

    void refit();

    size_t maxPoints_;
    std::deque<TimePoint> points_;
    size_t sinceFit_ = 0;
    bool fitted_ = false;
    // Fit relative to the first point ever added: t - t0 = a + b * (frame - f0).
    bool haveOrigin_ = false;
    int64_t f0_ = 0;
    double t0_ = 0.0;
    double a_ = 0.0;
    double b_ = 0.0;
};
