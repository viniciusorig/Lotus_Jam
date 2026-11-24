#include <gtest/gtest.h>

#include <cstdlib>

#include "dsp/bass_mapper.h"

namespace {
const Tuning kBass{"Padrão", {28, 33, 38, 43}};  // E1 A1 D2 G2

std::vector<MidiNote> notes(std::initializer_list<int> ms) {
    std::vector<MidiNote> v;
    double t = 0;
    for (int m : ms) v.push_back({t += 0.5, 0.4, m, -1});
    return v;
}
}  // namespace

TEST(BassMapper, PositionsReproduceTheNote) {
    const auto n = notes({40, 43, 45, 47, 40});
    const auto f = mapToInstrument(n, kBass);
    ASSERT_EQ(f.size(), n.size());
    for (size_t i = 0; i < f.size(); ++i) {
        EXPECT_EQ(f[i].midi, n[i].midi);  // already in range: no shift
        EXPECT_EQ(kBass.strings[size_t(f[i].string)] + f[i].fret, f[i].midi);
        EXPECT_GE(f[i].fret, 0);
        EXPECT_LE(f[i].fret, 24);
    }
    // A short phrase stays within a few frets of each other.
    for (size_t i = 1; i < f.size(); ++i) EXPECT_LE(std::abs(f[i].fret - f[i - 1].fret), 5);
}

TEST(BassMapper, HighTrackIsShiftedDownByOctaves) {
    const auto f = mapToInstrument(notes({72, 74, 76, 77}), kBass);  // C5 D5 E5 F5
    ASSERT_EQ(f.size(), 4u);
    for (const auto& x : f) {
        EXPECT_GE(x.midi, 28);
        EXPECT_LE(x.midi, 43 + 24);
        EXPECT_EQ(kBass.strings[size_t(x.string)] + x.fret, x.midi);
    }
    EXPECT_EQ(f[0].midi % 12, 0);  // the pitch class is kept
    EXPECT_EQ(f[1].midi - f[0].midi, 2);  // so is the melody's shape
}

TEST(BassMapper, NoStringsGivesNothing) {
    EXPECT_TRUE(mapToInstrument(notes({40}), Tuning{"Cromático", {}}).empty());
}
