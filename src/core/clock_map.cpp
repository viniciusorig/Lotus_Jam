#include "core/clock_map.h"

ClockMap::ClockMap(size_t maxPoints) : maxPoints_(maxPoints < kMinPoints ? kMinPoints : maxPoints) {}

void ClockMap::add(TimePoint p) {
    if (!haveOrigin_) {
        f0_ = p.frame;
        t0_ = p.tSec;
        haveOrigin_ = true;
    }
    points_.push_back(p);
    if (points_.size() > maxPoints_) points_.pop_front();
    ++sinceFit_;
    if (points_.size() >= kMinPoints && (!fitted_ || sinceFit_ >= kRefitEvery)) refit();
}

void ClockMap::refit() {
    const double n = static_cast<double>(points_.size());
    double sx = 0, sy = 0;
    for (const auto& p : points_) {
        sx += static_cast<double>(p.frame - f0_);
        sy += p.tSec - t0_;
    }
    const double mx = sx / n, my = sy / n;
    double sxx = 0, sxy = 0;
    for (const auto& p : points_) {
        const double dx = static_cast<double>(p.frame - f0_) - mx;
        sxx += dx * dx;
        sxy += dx * (p.tSec - t0_ - my);
    }
    if (sxx <= 0.0) return;  // degenerate (all same frame): keep previous fit
    b_ = sxy / sxx;
    a_ = my - b_ * mx;
    fitted_ = true;
    sinceFit_ = 0;
}

double ClockMap::timeOf(double frame) const {
    return t0_ + a_ + b_ * (frame - static_cast<double>(f0_));
}

double ClockMap::frameAt(double tSec) const {
    return static_cast<double>(f0_) + (tSec - t0_ - a_) / b_;
}
