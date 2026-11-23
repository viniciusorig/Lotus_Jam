#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "dsp/midi_parser.h"

// Writes a Standard MIDI File (SMF format 0) from a list of notes.
// Single track, monophonic (notes sorted by start time).
class MidiWriter {
public:
    // Quantize notes to a grid (optional) and write to file.
    // bpm: tempo for the MIDI file
    // quantizeGrid: if > 0, quantize note starts to this subdivision (e.g., 16 = 16th notes)
    // Returns true on success.
    static bool write(const std::string& path, const std::vector<MidiNote>& notes,
                      double bpm = 120.0, int quantizeGrid = 0);

private:
    static void writeVarLen(std::vector<uint8_t>& out, uint32_t value);
    static void writeU16BE(std::vector<uint8_t>& out, uint16_t value);
    static void writeU32BE(std::vector<uint8_t>& out, uint32_t value);
};
