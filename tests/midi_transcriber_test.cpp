#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "dsp/midi_parser.h"
#include "dsp/midi_writer.h"
#include "dsp/transcriber.h"

namespace {

std::string tmpPath(const char* name) { return std::string(::testing::TempDir()) + name; }

void writeBytes(const std::string& path, const std::vector<uint8_t>& b) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

}  // namespace

// 480 ticks/quarter. 120 BPM for 1 beat, then 60 BPM. Running status on the note-on that
// follows a note-on; the zero-length note (D4) must be dropped.
TEST(MidiParser, TempoMapRunningStatusAndZeroLength) {
    std::vector<uint8_t> trk = {
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,  // tempo 500000 us (120 BPM) at tick 0
        0x00, 0x90, 60,   100,                     // note on C4
        0x83, 0x60, 0x80, 60,   0,                 // +480: off -> 0.5 s
        0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,  // tempo 1000000 us (60 BPM) at tick 480
        0x00, 0x90, 62,   100,                     // note on D4 at tick 480
        0x00, 62,   0,                             // running status: vel 0 = off, zero length
        0x00, 0x90, 64,   100,                     // E4
        0x83, 0x60, 0x80, 64,   0,                 // +480: off at tick 960 -> 0.5 + 1.0 = 1.5 s
        0x00, 0xFF, 0x2F, 0x00};
    std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k',
                              0,   0,   0,   uint8_t(trk.size())};
    f.insert(f.end(), trk.begin(), trk.end());
    const std::string path = tmpPath("parser.mid");
    writeBytes(path, f);

    const MidiSong s = parseMidiFile(path);
    ASSERT_EQ(s.tracks.size(), 1u);
    const auto& n = s.tracks[0].notes;
    ASSERT_EQ(n.size(), 2u);
    EXPECT_EQ(n[0].midi, 60);
    EXPECT_NEAR(n[0].duration, 0.5, 1e-9);
    EXPECT_EQ(n[1].midi, 64);
    EXPECT_NEAR(n[1].start, 0.5, 1e-9);
    EXPECT_NEAR(n[1].duration, 1.0, 1e-9);
    std::remove(path.c_str());
}

TEST(MidiWriter, RoundTrip) {
    const std::vector<MidiNote> in = {{0.0, 0.5, 57, -1}, {0.5, 0.5, 60, -1}, {1.0, 1.0, 64, -1}};
    const std::string path = tmpPath("roundtrip.mid");
    ASSERT_TRUE(MidiWriter::write(path, in, 120.0, 16));
    const MidiSong s = parseMidiFile(path);
    ASSERT_EQ(s.tracks.size(), 1u);
    const auto& out = s.tracks[0].notes;
    ASSERT_EQ(out.size(), in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        EXPECT_EQ(out[i].midi, in[i].midi);
        EXPECT_NEAR(out[i].start, in[i].start, 0.01);
    }
    std::remove(path.c_str());
}

// A 1 s A3 (220 Hz) tone between silences must come out as one A3 note.
TEST(Transcriber, SingleTone) {
    constexpr double kSr = 48000.0;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<float> x(size_t(3 * kSr), 0.0f);
    for (size_t i = size_t(kSr); i < size_t(2 * kSr); ++i)
        x[i] = float(0.3 * std::sin(2 * kPi * 220.0 * double(i) / kSr));

    Transcriber t(kSr, 0.0125);
    for (size_t i = 0; i < x.size(); i += 4096) t.feed(x.data() + i, std::min<size_t>(4096, x.size() - i));
    t.finish();

    ASSERT_EQ(t.notes().size(), 1u);
    EXPECT_EQ(t.notes()[0].midi, 57);
    EXPECT_NEAR(t.notes()[0].start, 1.0, 0.15);
    EXPECT_NEAR(t.notes()[0].duration, 1.0, 0.25);
}
