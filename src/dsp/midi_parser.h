#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Song model from design spec §5: Song { tempo map, compassos, trilhas[] }
struct MidiNote {
    double start;     // seconds from song start
    double duration;  // seconds
    int midi;         // MIDI note number
    int stringId;     // string index (or -1 if not assigned)
};

struct MidiTrack {
    std::string name;
    std::string instrument;  // instrument family name
    std::vector<MidiNote> notes;
};

struct MidiSong {
    double tempoBpm;          // tempo in beats per minute
    int division;             // SMF division (ticks per quarter note)
    std::vector<MidiTrack> tracks;
};

// Parse an SMF 0/1 file. Returns a MidiSong with tempo map and tracks.
MidiSong parseMidiFile(const std::string& filePath);

// Extract a single track by index from a parsed MidiSong.
// Returns empty optional if track index is out of range.
std::optional<MidiTrack> getTrack(const MidiSong& song, size_t trackIndex);
