#include "dsp/onset.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr size_t kEnvHistory = 10000;  // 10 s of 1 ms steps
// Noise floor = p3 of the last 10 s of the RAW 5 ms RMS (not the held envelope). p20 over 5 s
// tracked note-level energy when notes sound >= ~80% of the time (sustained bass), pushing
// 5x floor above the note RMS and losing onsets; a low percentile over a longer span still
// lands in the gaps/quiet moments -- as long as there are gaps (see kGateRatio below).
constexpr double kFloorPercentile = 0.03;
// Power of two. Indexed back by up to kHold - 1 (peak-hold), kRiseSteps (rise), kHold (baseline
// window) and kMaxWalkBack + 1 (walk-back) steps, so it needs >= kHold + 1 = 37 entries.
constexpr size_t kDbHistory = 64;
// Ripple suppression. A 5 ms RMS window is only ~20% of the period of a 41 Hz note (24 ms;
// 30.9 Hz: 32 ms), so its output oscillates within each cycle and the rise criterion fired on
// the ripple. The decision (threshold, peak reference, rise) therefore uses the peak-hold of
// the 5 ms RMS over 36 steps (> one period of 30 Hz), which is flat on a sustained low sine;
// the short 5 ms envelope is still used for the noise floor and to walk back to the attack
// start, keeping the timing sharp.
constexpr int64_t kHold = 36;
static_assert(kDbHistory > size_t(kHold), "dB ring must hold the kHold + 1 steps read back");
constexpr double kBaseDb = 1.0;
constexpr int64_t kRecompute = 500;
constexpr int64_t kRiseSteps = 15;
constexpr int kMaxWalkBack = 20;
constexpr double kRiseDb = 6.0;
constexpr float kStepDb = 0.2f;
// Noise-floor multiplier (spec value; to be retuned later with the real bass).
constexpr double kFloorMult = 5.0;
constexpr int64_t kResync = 1000;  // steps between exact recomputations of the running sum
constexpr double kPeakFrac = 0.12;
// Floor-term gate (continuous playing). The floor term 5 x p3 assumes the 10 s window holds
// some real silence. When the player never stops (e.g. 120 BPM, tau 0.4 s: the level falls only
// ~11 dB between plucks), p3 converges to the INTER-NOTE level and 5 x p3 (+14 dB) ends above
// the note peaks: detection collapsed ~11 s into the take. Measured p99(held)/p3(raw) over the
// window: stationary noise 2.0-2.3 dB (sigma 6e-4..0.1), 50/60 Hz hum + noise at -39 dBFS
// 3.7-5.3 dB, but >= 10.8 dB for every continuous tau = 0.4 take (41-247 Hz, sine/harmonic,
// replace/overlap). So when p99 > kGateRatio x p3 the window holds signal that stationary noise
// cannot produce, p3 may be a note tail rather than noise, and the floor term is capped at
// kGateCap x p99 (it still applies in full when it is lower, e.g. weak notes over real silence,
// where 5 x p3 is far below the notes). Below the gate (noise, hum) nothing changes: 5 x p3.
// Capping only weakens a second guard: the 6 dB / 15 ms rise on the held envelope alone gave 0
// false onsets in 2 h of noise and hum (sigma 6e-4..0.1, 50/60 Hz) with no floor term at all.
constexpr double kGateRatio = 2.5;  // 8 dB
constexpr double kGateCap = 0.25;   // -12 dB below the peak reference
}  // namespace

OnsetDetector::OnsetDetector(double sampleRate)
    : win_(size_t(0.005 * sampleRate + 0.5)),
      hop_(size_t(sampleRate / 1000.0 + 0.5)),
      dcAlpha_(1.0 / (2.0 * sampleRate)),
      sq_(win_, 0.0),
      untilStep_(win_),
      env_(kEnvHistory, 0.0f),
      rawHist_(kEnvHistory, 0.0f),
      db_(kDbHistory, 0.0f),
      raw_(kDbHistory, 0.0f),
      holdDb_(kDbHistory, 0.0f),
      lin_(kEnvHistory),
      scratch_(kEnvHistory) {
    if (win_ < 1) {
        win_ = 1;
        sq_.assign(1, 0.0);
        untilStep_ = 1;
    }
    if (hop_ < 1) hop_ = 1;
}

double OnsetDetector::percentile(const float* data, size_t n, double p) {
    std::copy(data, data + n, scratch_.begin());
    double pos = p * double(n - 1);
    size_t k = size_t(pos);
    double frac = pos - double(k);
    auto b = scratch_.begin();
    std::nth_element(b, b + k, b + n);
    double lo = *(b + k);
    if (frac == 0.0 || k + 1 >= n) return lo;
    double hi = *std::min_element(b + k + 1, b + n);
    return lo + frac * (hi - lo);
}

void OnsetDetector::updateThreshold() {
    // Linearise the last min(steps, 10000) envelope values (oldest first) from the ring.
    size_t n = size_t(std::min<int64_t>(steps_, int64_t(kEnvHistory)));
    size_t start = size_t(steps_) - n;
    // Floor from the raw 5 ms RMS (a lower, tighter estimate of the quiet level than the
    // peak-held one, which keeps weak notes detectable); peak from the held envelope.
    for (size_t i = 0; i < n; ++i) lin_[i] = rawHist_[(start + i) % kEnvHistory];
    double floorEnv = percentile(lin_.data(), n, kFloorPercentile);
    for (size_t i = 0; i < n; ++i) lin_[i] = env_[(start + i) % kEnvHistory];
    double peakEnv = percentile(lin_.data(), n, 0.99);
    double floorTerm = kFloorMult * floorEnv;
    if (peakEnv > kGateRatio * floorEnv) floorTerm = std::min(floorTerm, kGateCap * peakEnv);
    thr_ = std::max(floorTerm, kPeakFrac * peakEnv);
    haveThr_ = true;
}

void OnsetDetector::step(std::vector<int64_t>& out) {
    const int64_t m = steps_;
    if (m % kResync == 0) {  // bound floating-point drift of the running sum
        double s = 0.0;
        for (double q : sq_) s += q;
        sum_ = s;
    }
    double e = std::sqrt(std::max(sum_, 0.0) / double(win_)) + 1e-9;
    raw_[size_t(m) % kDbHistory] = float(e);
    db_[size_t(m) % kDbHistory] = float(20.0 * std::log10(e));
    // Peak-hold of the short-window RMS over kHold steps: removes the within-cycle ripple.
    float held = float(e);
    for (int64_t k = 1; k < kHold && k <= m; ++k) held = std::max(held, raw_[size_t(m - k) % kDbHistory]);
    env_[size_t(m) % kEnvHistory] = held;
    rawHist_[size_t(m) % kEnvHistory] = float(e);
    holdDb_[size_t(m) % kDbHistory] = float(20.0 * std::log10(double(held)));
    steps_ = m + 1;

    if (m % kRecompute == 0 && m >= int64_t(warmup_)) updateThreshold();
    if (!haveThr_ || m < int64_t(warmup_) || m < kRiseSteps) return;

    auto db = [&](int64_t k) { return double(db_[size_t(k) % kDbHistory]); };
    auto hdb = [&](int64_t k) { return double(holdDb_[size_t(k) % kDbHistory]); };
    if (double(held) > thr_ && hdb(m) - hdb(m - kRiseSteps) > kRiseDb && m - lastOnset_ > refractory_) {
        // Pre-attack baseline: lowest short-window level in the 16..36 steps before m. The
        // walk-back stops once within kBaseDb of it, so noise wiggles before the attack are
        // not mistaken for the start of the rise.
        double base = 1e9;
        for (int64_t k = kRiseSteps + 1; k <= kHold && k <= m; ++k) base = std::min(base, db(m - k));
        int64_t j = m;
        while (j > 0 && db(j - 1) < db(j) - kStepDb && m - j < kMaxWalkBack && db(j) > base + kBaseDb) --j;
        // Deviation from the prototype (start + WIN/2): the first window whose energy rises is
        // the one whose *end* just passes the attack, so the attack is at start + WIN. With
        // WIN/2 onsets were systematically 1.5-3.5 ms early; this gives |error| <= 1 ms.
        out.push_back(j * int64_t(hop_) + int64_t(win_));
        lastOnset_ = m;
    }
}

void OnsetDetector::feed(const float* x, size_t n, std::vector<int64_t>& onsetsOut) {
    for (size_t i = 0; i < n; ++i) {
        double v = x[i];
        if (!dcInit_) {
            dc_ = v;
            dcInit_ = true;
        } else {
            dc_ += dcAlpha_ * (v - dc_);
        }
        double d = v - dc_;
        double q = d * d;
        sum_ += q - sq_[sqPos_];
        sq_[sqPos_] = q;
        if (++sqPos_ == win_) sqPos_ = 0;
        if (--untilStep_ == 0) {
            untilStep_ = hop_;
            step(onsetsOut);
        }
    }
}
