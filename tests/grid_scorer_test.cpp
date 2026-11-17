#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "score/grid_scorer.h"

namespace {

// Grid starting at 1.0 s with the given step.
GridScorer make(double step, double comp = 0.0) {
  return GridScorer(step, comp, [step](int64_t k) { return 1.0 + step * double(k); });
}

TEST(GridScorer, PerfectHitScoresHundred) {
  auto g = make(0.5);
  auto e = g.onNote(1.0);
  ASSERT_TRUE(e);
  EXPECT_EQ(e->kind, ScoreEvent::Hit);
  EXPECT_EQ(e->slot, 0);
  EXPECT_NEAR(e->errorMs, 0.0, 1e-9);
  EXPECT_NEAR(e->score, 100.0, 1e-9);
  EXPECT_NEAR(g.onNote(1.5 + 0.010)->score, 100.0, 1e-6);
  auto e50 = g.onNote(2.0 + 0.050);
  EXPECT_NEAR(e50->score, 100.0 * 50.0 / 85.0, 1e-6);
  EXPECT_NEAR(e50->errorMs, 50.0, 1e-6);
  auto e100 = g.onNote(2.5 - 0.120);
  EXPECT_EQ(e100->kind, ScoreEvent::Hit);
  EXPECT_NEAR(e100->errorMs, -120.0, 1e-6);
  EXPECT_NEAR(e100->score, 0.0, 1e-9);
}

TEST(GridScorer, MissAfterWindow) {
  auto g = make(0.5);
  std::vector<ScoreEvent> m;
  g.advance(1.0 + 0.2 + 0.1 - 0.001, m);
  EXPECT_TRUE(m.empty());
  g.advance(1.0 + 0.2 + 0.1 + 0.001, m);
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].kind, ScoreEvent::Miss);
  EXPECT_EQ(m[0].slot, 0);
  EXPECT_EQ(m[0].score, 0.0);
  g.advance(1.4, m);  // finalised exactly once
  EXPECT_EQ(m.size(), 1u);
  auto late = g.onNote(1.01);  // slot 0 already finalised
  EXPECT_EQ(late->kind, ScoreEvent::Extra);
  EXPECT_EQ(g.summary().extras, 1);
}

TEST(GridScorer, ExtraWhenSlotTaken) {
  auto g = make(0.5);
  EXPECT_EQ(g.onNote(1.0)->kind, ScoreEvent::Hit);
  auto e = g.onNote(1.03);
  EXPECT_EQ(e->kind, ScoreEvent::Extra);
  EXPECT_EQ(e->slot, 0);
  EXPECT_EQ(e->score, 0.0);
}

TEST(GridScorer, ExtraWhenFarFromGrid) {
  auto g = make(0.75);
  auto e = g.onNote(1.0 + 0.3);  // 0.3 s from slot 0, 0.45 from slot 1
  ASSERT_TRUE(e);
  EXPECT_EQ(e->kind, ScoreEvent::Extra);
}

TEST(GridScorer, NegativeSlotIgnored) {
  auto g = make(0.5);
  EXPECT_FALSE(g.onNote(0.5));
  EXPECT_FALSE(g.onNote(0.0));
  EXPECT_EQ(g.summary().extras, 0);
}

TEST(GridScorer, CompensationShiftsError) {
  auto g = make(0.5, 0.030);
  auto e = g.onNote(1.0 + 0.030 + 0.020);
  EXPECT_NEAR(e->errorMs, 20.0, 1e-6);
  auto n = g.onNote(1.5 + 0.030 - 0.010);
  EXPECT_NEAR(n->errorMs, -10.0, 1e-6);
  std::vector<ScoreEvent> m;
  g.advance(1.0 + 0.2 + 0.1 + 0.03 - 0.001, m);  // now-comp still too early
  EXPECT_TRUE(m.empty());
}

// Expected values produced by running the prototype's pontuar()/relatorio() logic
// (treino.py, numpy) with python3 -I on grid 1.0+0.5k (k=0..7), attacks at errors
// {0,10,-20,35,none,-60,90,120} ms plus a second attack at slot0+30 ms (extra).
TEST(GridScorer, SummaryMatchesPrototype) {
  auto g = make(0.5);
  const double errs[8] = {0, 10, -20, 35, 1e9, -60, 90, 120};
  g.onNote(1.0 + errs[0] / 1000);
  EXPECT_EQ(g.onNote(1.03)->kind, ScoreEvent::Extra);
  for (int k = 1; k < 8; ++k)
    if (errs[k] < 1e8) g.onNote(1.0 + 0.5 * k + errs[k] / 1000);
  std::vector<ScoreEvent> m;
  g.advance(1.0 + 3.5 + 0.2 + 0.1 + 0.001, m);  // finalises slots 0..7 only
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].slot, 4);
  auto s = g.summary(4);
  EXPECT_EQ(s.slots, 8);
  EXPECT_EQ(s.hits, 7);
  EXPECT_EQ(s.extras, 1);
  EXPECT_NEAR(s.total, 53.67647058823528, 1e-6);
  EXPECT_NEAR(s.lastN, 14.705882352941202, 1e-6);
  EXPECT_NEAR(s.meanErrMs, 25.0, 1e-6);
  EXPECT_NEAR(s.stdErrMs, 57.87918451395115, 1e-6);
}

TEST(GridScorer, EmptySummaryIsZero) {
  auto g = make(0.5);
  auto s = g.summary();
  EXPECT_EQ(s.total, 0.0);
  EXPECT_EQ(s.lastN, 0.0);
  EXPECT_EQ(s.stdErrMs, 0.0);
  EXPECT_EQ(s.slots, 0);
}

TEST(GridScorer, ExtremeTempos) {
  for (double bpm : {30.0, 240.0}) {
    const double step = 60.0 / bpm / 4;
    auto g = make(step);
    EXPECT_LE(g.window(), 0.2);
    EXPECT_GT(g.window(), 0.0);
    auto e = g.onNote(1.0 + step);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ScoreEvent::Hit);
    EXPECT_EQ(e->slot, 1);
    EXPECT_TRUE(std::isfinite(e->errorMs));
    std::vector<ScoreEvent> m;
    g.advance(5.0, m);
    auto s = g.summary();
    EXPECT_TRUE(std::isfinite(s.total));
    EXPECT_TRUE(std::isfinite(s.stdErrMs));
  }
}

#ifdef NDEBUG
TEST(GridScorer, NonPositiveStepIsClamped) {
  auto g = make(0.0);
  EXPECT_GT(g.window(), 0.0);
  auto e = g.onNote(1.0);
  ASSERT_TRUE(e);
  EXPECT_TRUE(std::isfinite(e->errorMs));
}
#else
TEST(GridScorer, NonPositiveStepAsserts) {
  EXPECT_DEATH(make(0.0), "");
}
#endif

}  // namespace
