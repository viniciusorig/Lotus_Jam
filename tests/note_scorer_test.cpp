#include <gtest/gtest.h>

#include "score/note_scorer.h"

namespace {

std::vector<MidiNote> makeNotes(std::initializer_list<std::pair<double, int>> notes) {
    std::vector<MidiNote> result;
    for (const auto& [start, midi] : notes) {
        result.push_back({start, 0.5, midi, -1});
    }
    return result;
}

TEST(NoteScorer, PerfectHitScoresHigh) {
    // Note at t=1.0, play exactly at t=1.0
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{1.0, 60, 0.0};
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Hit);
    EXPECT_GT(result->score, 90.0);
    EXPECT_NEAR(result->errorMs, 0.0, 1.0);
}

TEST(NoteScorer, LateNoteWithinWindow) {
    // Note at t=1.0, play at t=1.08 (80ms late, within 60-150ms window)
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{1.08, 60, 0.0};
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Late);
    EXPECT_NEAR(result->errorMs, 80.0, 1.0);
}

TEST(NoteScorer, EarlyNoteWithinWindow) {
    // Note at t=1.0, play at t=0.92 (80ms early)
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{0.92, 60, 0.0};
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Early);
    EXPECT_NEAR(result->errorMs, -80.0, 1.0);
}

TEST(NoteScorer, WrongPitchScoresZero) {
    // Note at t=1.0 expects C4 (60), play D4 (62)
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{1.0, 62, 0.0};  // wrong note
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::WrongNote);
    EXPECT_EQ(result->score, 0.0);
}

TEST(NoteScorer, OctaveEquivalentAccepted) {
    // Note expects C4 (60), play C3 (48) - octave lower (bass typical)
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{1.0, 48, 0.0};  // octave lower
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Hit);
}

TEST(NoteScorer, ExtraNoteFarFromAny) {
    // Notes at t=1.0 and t=2.0, play at t=1.5 (far from both)
    auto notes = makeNotes({{1.0, 60}, {2.0, 62}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{1.5, 60, 0.0};  // 500ms from nearest
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Extra);
}

TEST(NoteScorer, MissAfterWindow) {
    // Note at t=1.0, never played
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    std::vector<NoteResult> misses;
    scorer.advance(1.5, misses);  // well past the window

    ASSERT_EQ(misses.size(), 1);
    EXPECT_EQ(misses[0].kind, NoteResult::Miss);
    EXPECT_EQ(misses[0].noteIndex, 0);
}

TEST(NoteScorer, SpeedFactorAffectsTiming) {
    // Note at t=1.0 in song, speed 0.5 (half speed) -> actual time is 2.0
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 0.5, 0.0, 0.0);

    // Play at actual time 2.0 (which is song time 1.0 at half speed)
    DetectedNote played{2.0, 60, 0.0};
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Hit);
}

TEST(NoteScorer, CompensationApplied) {
    // Note at t=1.0, compensation 50ms, play at t=1.05
    // After compensation: 1.05 - 0.05 = 1.0, perfect
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.05, 0.0);

    DetectedNote played{1.05, 60, 0.0};
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Hit);
    EXPECT_NEAR(result->errorMs, 0.0, 5.0);
}

TEST(NoteScorer, SummaryCountsCorrectly) {
    auto notes = makeNotes({{1.0, 60}, {2.0, 62}, {3.0, 64}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    // Hit first note
    scorer.onNote({1.0, 60, 0.0});
    // Wrong note for second
    scorer.onNote({2.0, 65, 0.0});
    // Miss third (advance past it)
    std::vector<NoteResult> misses;
    scorer.advance(3.5, misses);

    auto summary = scorer.summary();
    EXPECT_EQ(summary.hits, 1);
    EXPECT_EQ(summary.wrongNotes, 1);
    EXPECT_EQ(summary.misses, 1);
    EXPECT_EQ(summary.totalNotes, 3);
}

TEST(NoteScorer, PitchToleranceWithCents) {
    // Note at t=1.0 expects C4 (60), play C4 with +40 cents (within ±50)
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{1.0, 60, 40.0};  // 40 cents sharp
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, NoteResult::Hit);
}

TEST(NoteScorer, PitchToleranceExceeded) {
    // Note expects C4 (60), play C4 with +60 cents (outside ±50)
    auto notes = makeNotes({{1.0, 60}});
    NoteScorer scorer(notes, 1.0, 0.0, 0.0);

    DetectedNote played{1.0, 60, 60.0};  // 60 cents sharp - too much
    auto result = scorer.onNote(played);

    ASSERT_TRUE(result.has_value());
    // Still matched but wrong pitch tolerance
    EXPECT_EQ(result->kind, NoteResult::WrongNote);
}

}  // namespace
