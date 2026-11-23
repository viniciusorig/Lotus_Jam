#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "dsp/midi_parser.h"

// Result of matching a played note against the song's expected notes.
struct NoteResult {
    enum Kind {
        Hit,       // pitch and timing correct
        WrongNote, // timing ok but wrong pitch
        Early,     // right pitch but too early (60-150ms)
        Late,      // right pitch but too late (60-150ms)
        Miss,      // expected note not played
        Extra      // played note not matching any expected note
    } kind;
    int noteIndex;    // index in the song's note list (-1 for Extra)
    int expectedMidi; // expected MIDI note (-1 for Extra)
    int playedMidi;   // detected MIDI note (-1 for Miss)
    double errorMs;   // timing error (positive = late)
    double score;     // 0..100
};

// A detected note with pitch and timing.
struct DetectedNote {
    double tSec;  // time in seconds
    int midi;     // MIDI note number (from pitch detection)
    double cents; // cents offset from the detected note
};

// Scores played notes against a song's expected notes (pitch + timing).
// Tolerances from spec §9: pitch ±50 cents, timing ≤60ms = hit, 60-150ms = early/late.
class NoteScorer {
public:
    // speedFactor: 1.0 = original tempo, 0.5 = half speed, etc.
    // compensationSec: latency compensation (subtracted from played time)
    NoteScorer(const std::vector<MidiNote>& notes, double speedFactor,
               double compensationSec, double songStartSec);

    // Process a detected note. Returns the result (Hit/WrongNote/Early/Late/Extra).
    std::optional<NoteResult> onNote(const DetectedNote& played);

    // Advance time and finalize missed notes. Appends Miss events to missesOut.
    void advance(double nowSec, std::vector<NoteResult>& missesOut);

    // Get the next N expected notes starting from time nowSec (for display).
    std::vector<std::pair<size_t, const MidiNote*>> upcoming(double nowSec, size_t maxCount) const;

    struct Summary {
        double total, lastN;  // mean score
        int hits, wrongNotes, early, late, misses, extras;
        int totalNotes;
        double meanErrMs, stdErrMs;
    };
    Summary summary(size_t lastN = 16) const;

    // Config
    static constexpr double kPitchToleranceCents = 50.0;
    static constexpr double kTimingPerfectMs = 60.0;
    static constexpr double kTimingOkMs = 150.0;

private:
    struct NoteState {
        bool matched = false;
        NoteResult result{};
    };

    std::vector<MidiNote> notes_;
    std::vector<NoteState> state_;
    double speed_, comp_, start_;
    size_t nextFinal_ = 0;
    int extras_ = 0;
    std::deque<NoteResult> done_;

    double noteTime(size_t i) const;  // adjusted for speed and start
    bool pitchMatches(int expected, int played, double cents) const;
};
