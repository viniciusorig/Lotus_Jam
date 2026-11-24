#pragma once
#include <vector>

#include "dsp/midi_parser.h"
#include "dsp/notes.h"

// Where to play a note on a fretted instrument.
struct Fingering {
    int midi;    // the note as played: the original shifted by whole octaves to fit the instrument
    int string;  // index into Tuning::strings (0 = lowest)
    int fret;    // 0 = open string
};

// Translates a track to positions on the instrument: the whole track is shifted by the number of
// octaves that fits most notes in the instrument's range, the remaining outliers are folded one
// by one, then the positions are chosen to keep the hand still (fewest fret and string jumps).
// The result is parallel to `notes` (same order). Empty when the tuning has no strings.
// Display only: scoring already ignores the octave (NoteScorer::pitchMatches).
std::vector<Fingering> mapToInstrument(const std::vector<MidiNote>& notes, const Tuning& tuning, int maxFret = 24);
