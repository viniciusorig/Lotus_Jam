#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp/notes.h"
#include "dsp/tuner.h"

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

const Instrument& inst(const char* id) {
  for (const auto& i : instruments())
    if (i.id == id) return i;
  ADD_FAILURE() << "missing instrument " << id;
  return instruments().front();
}

// String-like tone (fundamental + 3 harmonics), or a pure sine when pure == true.
std::vector<float> tone(double hz, double seconds, bool pure = false) {
  size_t n = size_t(seconds * kSr);
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    double t = double(i) / kSr;
    double v = std::sin(2 * kPi * hz * t);
    if (!pure)
      v += 0.5 * std::sin(2 * kPi * 2 * hz * t) + 0.3 * std::sin(2 * kPi * 3 * hz * t) +
           0.2 * std::sin(2 * kPi * 4 * hz * t);
    x[i] = float(0.3 * v);
  }
  return x;
}

// Feeds x in blocks of 480 samples (10 ms) and collects every reading.
std::vector<TunerReading> run(Tuner& tuner, const std::vector<float>& x) {
  std::vector<TunerReading> out;
  for (size_t i = 0; i < x.size(); i += 480) {
    tuner.feed(x.data() + i, std::min<size_t>(480, x.size() - i));
    TunerReading r;
    while (tuner.poll(r)) out.push_back(r);
  }
  return out;
}

}  // namespace

// The violão window is W + 48000/minHz = 2048 + 727 = 2775 samples (~58 ms), so a full
// analysis fits inside 100 ms. (Even baixo5, with the longest window of 3775 samples
// = ~79 ms, would fit; the guitar A string keeps the test simple.)
TEST(Tuner, ReadingWithin100ms) {
  const Instrument& v = inst("violao");
  Tuner tuner(kSr, v, v.tunings[0], 440.0);
  auto x = tone(midiToHz(45), 0.100);  // A2, string index 1
  ASSERT_EQ(x.size(), 4800u);
  bool found = false;
  for (const auto& r : run(tuner, x))
    if (r.voiced && r.stringIndex == 1 && std::fabs(r.cents) < 1.0) found = true;
  EXPECT_TRUE(found);
}

TEST(Tuner, AtLeastTwentyReadingsPerSecond) {
  const Instrument& b = inst("baixo5");
  Tuner tuner(kSr, b, b.tunings[0], 440.0);
  auto x = tone(midiToHz(23), 2.0);  // B0, the lowest string
  auto rs = run(tuner, x);
  EXPECT_GE(rs.size(), 39u);  // >= 20/s once the first window is full
  ASSERT_FALSE(rs.empty());
  EXPECT_TRUE(rs.back().voiced);
  EXPECT_EQ(rs.back().stringIndex, 0);
  EXPECT_NEAR(rs.back().cents, 0.0, 1.0);
}

TEST(Tuner, DetuneShowsCents) {
  const Instrument& v = inst("violao");
  Tuner tuner(kSr, v, v.tunings[0], 440.0);
  auto x = tone(midiToHz(40) * std::pow(2.0, 20.0 / 1200.0), 0.5);
  auto rs = run(tuner, x);
  ASSERT_FALSE(rs.empty());
  const TunerReading& r = rs.back();
  ASSERT_TRUE(r.voiced);
  EXPECT_EQ(r.stringIndex, 0);
  EXPECT_NEAR(r.targetHz, midiToHz(40), 1e-6);
  EXPECT_NEAR(r.cents, 20.0, 1.0);
}

TEST(Tuner, HonoursA4) {
  const Instrument& v = inst("violao");
  Tuner tuner(kSr, v, v.tunings[0], 432.0);
  auto rs = run(tuner, tone(midiToHz(40, 432.0), 0.5));
  ASSERT_FALSE(rs.empty());
  ASSERT_TRUE(rs.back().voiced);
  EXPECT_NEAR(rs.back().cents, 0.0, 1.0);
  EXPECT_NEAR(rs.back().targetHz, midiToHz(40, 432.0), 1e-6);
}

TEST(Tuner, UnvoicedAndOutOfRange) {
  const Instrument& b = inst("baixo4");
  {
    Tuner tuner(kSr, b, b.tunings[0], 440.0);
    auto rs = run(tuner, std::vector<float>(size_t(kSr / 2), 0.0f));
    ASSERT_FALSE(rs.empty());
    for (const auto& r : rs) EXPECT_FALSE(r.voiced) << "silence";
  }
  {
    Tuner tuner(kSr, b, b.tunings[0], 440.0);
    auto rs = run(tuner, tone(440.0, 0.5, true));
    ASSERT_FALSE(rs.empty());
    for (const auto& r : rs) EXPECT_FALSE(r.voiced) << "440 Hz on bass: " << r.hz;
  }
  {
    // Quiet mains hum (60 Hz + 2nd harmonic at about -50 dBFS, like the PCM2902 in silence).
    Tuner tuner(kSr, b, b.tunings[0], 440.0);
    std::vector<float> hum = tone(60.0, 0.5, false);
    for (float& v : hum) v *= 0.012f;
    auto rs = run(tuner, hum);
    ASSERT_FALSE(rs.empty());
    for (const auto& r : rs) EXPECT_FALSE(r.voiced) << "quiet hum: " << r.hz;
  }
}

// A slack string must be reported against the nearest string (the player needs the cents to
// tune it up), not rejected. Recorded case: the D string of a bass sounding at about 66 Hz.
TEST(Tuner, SlackStringIsReportedWithLargeCents) {
  const Instrument& b = inst("baixo4");
  Tuner tuner(kSr, b, b.tunings[0], 440.0);
  auto rs = run(tuner, tone(66.0, 1.0));
  ASSERT_FALSE(rs.empty());
  const TunerReading& r = rs.back();
  ASSERT_TRUE(r.voiced);
  EXPECT_EQ(r.stringIndex, 2);  // D2
  EXPECT_NEAR(r.cents, 1200.0 * std::log2(66.0 / midiToHz(38)), 3.0);
  EXPECT_LT(r.cents, -150.0);
}

// A pluck that starts inside the analysis window must not produce a wild first reading.
TEST(Tuner, PluckOnsetNeverGivesWildReadings) {
  const Instrument& b = inst("baixo4");
  Tuner tuner(kSr, b, b.tunings[0], 440.0);
  std::vector<float> x(size_t(0.25 * kSr), 0.0f);
  std::vector<float> note = tone(midiToHz(33), 1.0);  // A1
  x.insert(x.end(), note.begin(), note.end());
  auto rs = run(tuner, x);
  int voiced = 0;
  for (const auto& r : rs)
    if (r.voiced) {
      ++voiced;
      EXPECT_EQ(r.stringIndex, 1);
      EXPECT_LT(std::fabs(r.cents), 20.0) << r.hz;
    }
  EXPECT_GT(voiced, 5);
}
