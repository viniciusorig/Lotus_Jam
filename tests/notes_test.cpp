#include <gtest/gtest.h>

#include <cmath>

#include "dsp/notes.h"


TEST(Notes, A4Is440) { EXPECT_DOUBLE_EQ(midiToHz(69), 440.0); }

TEST(Notes, HzMidiRoundTrip) {
  for (int m = 20; m < 100; ++m) EXPECT_NEAR(hzToMidi(midiToHz(m)), m, 1e-9);
  EXPECT_NEAR(hzToMidi(midiToHz(60.37, 432.0), 432.0), 60.37, 1e-9);
}

TEST(Notes, NoteNames) {
  EXPECT_EQ(noteName(40), "E2");
  EXPECT_EQ(noteName(23), "B0");
  EXPECT_EQ(noteName(61), "C#4");
}

TEST(Notes, CentsOfSemitone) {
  EXPECT_NEAR(centsBetween(midiToHz(61), midiToHz(60)), 100.0, 1e-6);
}

static const Instrument& find(const std::string& id) {
  for (const auto& i : instruments())
    if (i.id == id) return i;
  ADD_FAILURE() << "missing " << id;
  return instruments().front();
}
static const Tuning* tuning(const Instrument& i, const std::string& n) {
  for (const auto& t : i.tunings)
    if (t.name == n) return &t;
  return nullptr;
}

TEST(Notes, InstrumentsCatalog) {
  ASSERT_EQ(instruments().size(), 5u);
  using V = std::vector<int>;
  EXPECT_EQ(tuning(find("violao"), "Padrão")->strings, (V{40, 45, 50, 55, 59, 64}));
  EXPECT_EQ(tuning(find("guitarra"), "Padrão")->strings, (V{40, 45, 50, 55, 59, 64}));
  EXPECT_EQ(tuning(find("baixo4"), "Padrão")->strings, (V{28, 33, 38, 43}));
  EXPECT_EQ(tuning(find("baixo5"), "Padrão")->strings, (V{23, 28, 33, 38, 43}));
  EXPECT_EQ(tuning(find("baixo6"), "Padrão")->strings, (V{23, 28, 33, 38, 43, 48}));
  EXPECT_EQ(tuning(find("violao"), "Drop D")->strings, (V{38, 45, 50, 55, 59, 64}));
  EXPECT_EQ(tuning(find("baixo4"), "Drop D")->strings, (V{26, 33, 38, 43}));
  EXPECT_EQ(tuning(find("baixo5"), "Drop D")->strings, (V{23, 26, 33, 38, 43}));
  EXPECT_EQ(tuning(find("baixo4"), "Meio-tom abaixo")->strings, (V{27, 32, 37, 42}));
  EXPECT_EQ(tuning(find("guitarra"), "DADGAD")->strings, (V{38, 45, 50, 55, 57, 62}));
  EXPECT_NE(tuning(find("violao"), "DADGAD"), nullptr);
  for (const char* id : {"baixo4", "baixo5", "baixo6"})
    EXPECT_EQ(tuning(find(id), "DADGAD"), nullptr) << id;
  EXPECT_EQ(find("violao").label, "Violão");
  EXPECT_EQ(find("baixo5").label, "Baixo 5 cordas");
}

TEST(Notes, EveryStringInsideRange) {
  for (const auto& i : instruments())
    for (const auto& t : i.tunings)
      for (int s : t.strings) {
        EXPECT_GE(midiToHz(s), i.minHz) << i.id << " " << t.name;
        EXPECT_LE(midiToHz(s), i.maxHz) << i.id << " " << t.name;
      }
}

TEST(Notes, NearestStringPicksClosest) {
  const Tuning& t = *tuning(find("violao"), "Padrão");
  auto a = nearestString(t, 82.0);
  EXPECT_EQ(a.stringIndex, 0);
  EXPECT_LT(std::fabs(a.cents), 20.0);
  EXPECT_NEAR(a.targetHz, midiToHz(40), 1e-9);
  EXPECT_EQ(nearestString(t, 110.0).stringIndex, 1);
}
