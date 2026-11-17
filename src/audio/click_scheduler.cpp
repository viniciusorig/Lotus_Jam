#include "audio/click_scheduler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double kLimit = 1e15;  // keeps double->int64 conversions defined for absurd inputs

void addTone(std::vector<float>& v, size_t at, double sr, double hz, double dur,
             double attack, double decay, double amp) {
    const size_t n = static_cast<size_t>(std::lround(dur * sr));
    for (size_t i = 0; i < n && at + i < v.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const double env = std::min(1.0, t / attack) * std::exp(-decay * t);
        v[at + i] = static_cast<float>(amp * env * std::sin(2.0 * std::numbers::pi * hz * t));
    }
}
}  // namespace

ClickScheduler::ClickScheduler(double sampleRate, double bpm, int countIn)
    : fpb_(sampleRate * 60.0 / std::clamp(bpm, 30.0, 240.0)), countIn_(std::max(0, countIn)) {
    const size_t n1 = static_cast<size_t>(std::lround(0.14 * sampleRate));
    const size_t n2 = static_cast<size_t>(std::lround(0.24 * sampleRate));
    cue_.assign(n1 + n2, 0.0f);
    addTone(cue_, 0, sampleRate, 587.3, 0.14, 0.004, 9.0, 0.6);
    addTone(cue_, n1, sampleRate, 880.0, 0.24, 0.004, 9.0, 0.6);
    click_.assign(static_cast<size_t>(std::lround(0.030 * sampleRate)), 0.0f);
    for (size_t i = 0; i < click_.size(); ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        click_[i] = static_cast<float>(0.8 * std::exp(-120.0 * t) *
                                       std::sin(2.0 * std::numbers::pi * 1200.0 * t));
    }
    firstBeat_ = static_cast<int64_t>(cue_.size()) + std::llround(0.7 * sampleRate);
}

int64_t ClickScheduler::beatStart(int64_t n) const noexcept {
    return std::llround(static_cast<double>(firstBeat_) + static_cast<double>(n) * fpb_);
}

int64_t ClickScheduler::beatBelow(int64_t frame) const noexcept {
    const double q = std::floor((static_cast<double>(frame) - static_cast<double>(firstBeat_)) / fpb_);
    int64_t n = static_cast<int64_t>(std::clamp(q, -kLimit, kLimit));
    while (beatStart(n) > frame) --n;       // fix rounding at the edges
    while (beatStart(n + 1) <= frame) ++n;
    return n;
}

size_t ClickScheduler::beatsIn(int64_t firstFrame, size_t frames, BeatMark* out,
                               size_t maxOut) const noexcept {
    if (frames == 0 || maxOut == 0) return 0;
    const int64_t end = firstFrame + static_cast<int64_t>(frames);
    int64_t n = std::max<int64_t>(0, beatBelow(firstFrame));
    size_t k = 0;
    for (; k < maxOut; ++n) {
        const int64_t s = beatStart(n);
        if (s >= end) break;
        if (s >= firstFrame) out[k++] = {n, s};
    }
    return k;
}

void ClickScheduler::render(float* out, size_t frames, int64_t firstFrame) noexcept {
    if (frames == 0) return;
    const int64_t end = firstFrame + static_cast<int64_t>(frames);
    for (size_t i = 0; i < frames; ++i) {
        const int64_t f = firstFrame + static_cast<int64_t>(i);
        float v = 0.0f;
        if (f >= 0 && f < static_cast<int64_t>(cue_.size())) v = cue_[static_cast<size_t>(f)];
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    // Clicks that can touch [firstFrame, end): starts in (firstFrame - clickLen, end).
    const int64_t len = static_cast<int64_t>(click_.size());
    for (int64_t n = std::max<int64_t>(0, beatBelow(firstFrame - len + 1)); beatStart(n) < end; ++n) {
        const int64_t s = beatStart(n);
        const int64_t a = std::max(s, firstFrame), b = std::min(s + len, end);
        for (int64_t f = a; f < b; ++f) {
            const size_t i = static_cast<size_t>(f - firstFrame);
            out[2 * i] += click_[static_cast<size_t>(f - s)];
            out[2 * i + 1] = out[2 * i];
        }
    }
    for (size_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}
