#include "score/grid_scorer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

GridScorer::GridScorer(double stepSec, double compensationSec, SlotTimeFn slotTime)
    : step_(stepSec), comp_(compensationSec), slotTime_(std::move(slotTime)) {
    assert(stepSec > 0.0);
    if (!(step_ > 0.0)) step_ = 0.001;  // release-build clamp (also catches NaN)
    window_ = std::min(0.5 * step_, 0.2);
}

std::optional<ScoreEvent> GridScorer::onNote(double tSec) {
    const double t = tSec - comp_;
    const int64_t k = int64_t(std::llround((t - slotTime_(0)) / step_));
    if (k < 0) return std::nullopt;
    const double errMs = (t - slotTime_(k)) * 1000.0;
    const bool taken = std::find(pendingSlot_.begin(), pendingSlot_.end(), k) != pendingSlot_.end();
    if (std::fabs(errMs) / 1000.0 > window_ || taken || k < nextFinal_) {
        ++extras_;
        return ScoreEvent{ScoreEvent::Extra, k, errMs, 0.0};
    }
    const double score = 100.0 * std::clamp((100.0 - std::fabs(errMs)) / 85.0, 0.0, 1.0);
    pendingSlot_.push_back(k);
    pending_.push_back({true, errMs, score});
    return ScoreEvent{ScoreEvent::Hit, k, errMs, score};
}

void GridScorer::advance(double nowSec, std::vector<ScoreEvent>& missesOut) {
    const double now = nowSec - comp_;
    while (slotTime_(nextFinal_) + window_ + 0.1 < now) {
        auto it = std::find(pendingSlot_.begin(), pendingSlot_.end(), nextFinal_);
        if (it != pendingSlot_.end()) {
            done_.push_back(pending_[it - pendingSlot_.begin()]);
            pending_.erase(pending_.begin() + (it - pendingSlot_.begin()));
            pendingSlot_.erase(it);
        } else {
            done_.push_back({false, 0.0, 0.0});
            missesOut.push_back({ScoreEvent::Miss, nextFinal_, 0.0, 0.0});
        }
        ++nextFinal_;
    }
}

GridScorer::Summary GridScorer::summary(size_t lastN) const {
    Summary s{0, 0, 0, int(done_.size()), extras_, 0, 0};
    double sum = 0, sumLast = 0, sumErr = 0;
    const size_t n = done_.size(), from = n > lastN ? n - lastN : 0;
    for (size_t i = 0; i < n; ++i) {
        sum += done_[i].score;
        if (i >= from) sumLast += done_[i].score;
        if (done_[i].hit) {
            ++s.hits;
            sumErr += done_[i].errMs;
        }
    }
    if (n) s.total = sum / double(n);
    if (n > from) s.lastN = sumLast / double(n - from);
    if (s.hits) {
        s.meanErrMs = sumErr / s.hits;
        double v = 0;
        for (const auto& d : done_)
            if (d.hit) v += (d.errMs - s.meanErrMs) * (d.errMs - s.meanErrMs);
        s.stdErrMs = std::sqrt(v / s.hits);
    }
    return s;
}
