#include "midi_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stack>
#include <unordered_map>

// --- SMF (Standard MIDI File) chunk helpers ---

// Read exactly `n` bytes from stream, returning false on error.
static bool readExact(std::istream& stream, std::vector<uint8_t>& buf, size_t n) {
    buf.resize(n);
    stream.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    return stream.gcount() == static_cast<std::streamsize>(n);
}

// Read a big-endian 16-bit unsigned integer.
static uint16_t readU16(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);
}

// Read a big-endian 32-bit unsigned integer.
static uint32_t readU32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8)  |
           static_cast<uint32_t>(data[3]);
}

// Decode a variable-length quantity (SMF delta time encoding).
// Returns the decoded value and advances `pos` past the VLQ bytes.
static size_t decodeVLQ(const uint8_t* data, size_t pos, size_t end, int64_t& value) {
    value = 0;
    size_t shift = 0;
    while (pos < end) {
        uint8_t b = data[pos++];
        value |= (static_cast<int64_t>(b & 0x7f) << shift);
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return pos;
}

// --- SMF 0/1 parser ---

MidiSong parseMidiFile(const std::string& filePath) {
    MidiSong song;
    song.tempoBpm = 120.0;
    song.division = 480;  // default: ticks per quarter note

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return song;  // empty
    }

    // --- Read header chunk ---

    // Must start with "MThd" (4 bytes)
    char magic[4];
    std::vector<uint8_t> magicVec(magic, magic + 4);
    if (!readExact(file, magicVec, 4)) return song;
    if (std::string(reinterpret_cast<const char*>(magicVec.data()), 4) != "MThd") return song;

    // Header size (always 6 for standard format)
    uint32_t headerSize;
    std::vector<uint8_t> headerSizeVec(reinterpret_cast<uint8_t*>(&headerSize), reinterpret_cast<uint8_t*>(&headerSize) + 4);
    if (!readExact(file, headerSizeVec, 4)) return song;
    headerSize = readU32(reinterpret_cast<const uint8_t*>(&headerSize));  // big-endian to host

    if (headerSize != 6) return song;  // only support standard format

    // Format type: 0 or 1
    uint16_t format;
    std::vector<uint8_t> formatVec(reinterpret_cast<uint8_t*>(&format), reinterpret_cast<uint8_t*>(&format) + 2);
    if (!readExact(file, formatVec, 2)) return song;
    format = readU16(reinterpret_cast<const uint8_t*>(&format));

    // Number of tracks
    uint16_t numTracks;
    std::vector<uint8_t> numTracksVec(reinterpret_cast<uint8_t*>(&numTracks), reinterpret_cast<uint8_t*>(&numTracks) + 2);
    if (!readExact(file, numTracksVec, 2)) return song;
    numTracks = readU16(reinterpret_cast<const uint8_t*>(&numTracks));

    // Division (ticks per quarter note for SMF 0; for SMF 1 it's also ticks per quarter)
    int16_t divisionRaw;
    std::vector<uint8_t> divisionRawVec(reinterpret_cast<uint8_t*>(&divisionRaw), reinterpret_cast<uint8_t*>(&divisionRaw) + 2);
    if (!readExact(file, divisionRawVec, 2)) return song;
    divisionRaw = readU16(reinterpret_cast<const uint8_t*>(&divisionRaw));
    if (divisionRaw > 0) {
        song.division = divisionRaw;  // positive = pulses per quarter note
    } else {
        song.division = -divisionRaw;  // negative = frames per second (MIDI Film)
    }

    // --- Read track chunks ---

    song.tracks.resize(numTracks);

    for (uint16_t t = 0; t < numTracks; ++t) {
        // Read "MTrk" magic
        char trkMagic[4];
        std::vector<uint8_t> trkMagicVec(trkMagic, trkMagic + 4);
        if (!readExact(file, trkMagicVec, 4)) break;
        if (std::string(reinterpret_cast<const char*>(trkMagicVec.data()), 4) != "MTrk") continue;

        // Track size
        uint32_t trackSize;
        std::vector<uint8_t> trackSizeVec(reinterpret_cast<uint8_t*>(&trackSize), reinterpret_cast<uint8_t*>(&trackSize) + 4);
        if (!readExact(file, trackSizeVec, 4)) break;
        trackSize = readU32(reinterpret_cast<const uint8_t*>(&trackSize));

        // Read track events with note-on/off tracking
        std::vector<MidiNote> notes;
        int64_t deltaTime = 0;
        int64_t absoluteTimeTicks = 0;  // absolute ticks from song start
        double ticksPerQuarter = song.division;

        // For tempo tracking
        double currentTempoBpm = 120.0;

        // Pending note-ons: note -> (start_ticks, channel)
        // Using unordered_map for note -> start_ticks mapping
        std::unordered_map<int, int64_t> pendingNotes;  // note -> start absolute tick

        const uint8_t* trackData = nullptr;
        {
            std::vector<uint8_t> buf(trackSize);
            if (!readExact(file, buf, trackSize)) break;
            trackData = buf.data();
        }

        size_t pos = 0;
        size_t end = trackSize;

        while (pos < end) {
            // Decode delta time
            int64_t dv = 0;
            pos = decodeVLQ(trackData, pos, end, dv);
            deltaTime = dv;

            // Absolute time in ticks from song start
            absoluteTimeTicks += deltaTime;

            // Event type: first byte (must have high bit set for a valid MIDI event)
            uint8_t status = trackData[pos];
            pos++;

            if (status < 0x80) {
                // Invalid or running-status byte without preceding status – skip it
                continue;
            }

            // Process note-on and note-off events
            if ((status & 0xF0) == 0x90) {  // Note On
                // velocity byte
                if (pos >= end) break;
                uint8_t velocity = trackData[pos];
                pos++;

                // Note number byte
                if (pos >= end) break;
                uint8_t note = trackData[pos];
                pos++;

                if (velocity > 0) {
                    // Note on with velocity > 0 - note starts now
                    // Store start absolute tick for this note
                    pendingNotes[note] = absoluteTimeTicks;
                } else {
                    // Note on with velocity 0 = note off
                    // Finalize the note
                    auto it = pendingNotes.find(note);
                    if (it != pendingNotes.end()) {
                        int64_t startTick = it->second;
                        int64_t durationTicks = absoluteTimeTicks - startTick;
                        // Convert ticks to seconds: (ticks * 60) / (tempo * division)
                        double durationSec = (durationTicks * 60.0) / (currentTempoBpm * ticksPerQuarter);
                        // Also compute start time from start tick
                        double startSec = (startTick * 60.0) / (currentTempoBpm * ticksPerQuarter);
                        notes.push_back({startSec, durationSec, (int)note, -1});
                        pendingNotes.erase(it);
                    }
                }
            } else if ((status & 0xF0) == 0x80) {  // Note Off
                if (pos >= end) break;
                pos++;  // velocity byte (unused)

                if (pos >= end) break;
                uint8_t note = trackData[pos];
                pos++;

                // Note off - find matching note-on and finalize
                auto it = pendingNotes.find(note);
                if (it != pendingNotes.end()) {
                    int64_t startTick = it->second;
                    int64_t durationTicks = absoluteTimeTicks - startTick;
                    // Convert ticks to seconds: (ticks * 60) / (tempo * division)
                    double durationSec = (durationTicks * 60.0) / (currentTempoBpm * ticksPerQuarter);
                    // Also compute start time from start tick
                    double startSec = (startTick * 60.0) / (currentTempoBpm * ticksPerQuarter);
                    notes.push_back({startSec, durationSec, (int)note, -1});
                    pendingNotes.erase(it);
                }
            }
            // Pitch wheel, control change, etc - skip
            else if ((status & 0xF0) == 0xE0) {  // Pitch Wheel Change
                if (pos >= end) break;
                pos++;  // LSB
                if (pos >= end) break;
                pos++;  // MSB
            } else if ((status & 0xF0) == 0xB0) {  // Control Change
                if (pos >= end) break;
                pos++;  // controller number
                if (pos >= end) break;
                pos++;  // value
            } else if ((status & 0xF0) == 0xC0) {  // Program Change
                if (pos >= end) break;
                pos++;  // program number
            } else if ((status & 0xF0) == 0xD0) {  // Channel Aftertouch
                if (pos >= end) break;
                pos++;  // value
            } else if ((status & 0xF0) == 0xF0) {  // SysEx / Meta
                // Handle meta events
                uint8_t metaType = trackData[pos];
                pos++;
                // Parameter length
                if (pos >= end) break;
                uint8_t length = trackData[pos];
                pos++;
                // Skip length bytes
                pos += length;
                // Check for tempo change meta event (0x51)
                if (metaType == 0x51 && pos <= end) {
                    // Tempo events have 3 bytes: microseconds per quarter note
                    if (pos + 3 <= end) {
                        uint8_t* tdata = const_cast<uint8_t*>(trackData);
                        uint32_t usPerQuarter = (static_cast<uint32_t>(tdata[pos]) << 16) |
                                               (static_cast<uint32_t>(tdata[pos+1]) << 8) |
                                               static_cast<uint32_t>(tdata[pos+2]);
                        currentTempoBpm = 60.0 / (usPerQuarter / 1e6);
                        pos += 3;
                    }
                }
            }
        }

        // Finalize any remaining pending notes at end of track
        // (notes that were turned on but never turned off)
        double endSec = (absoluteTimeTicks * 60.0) / (currentTempoBpm * ticksPerQuarter);
        for (auto& [note, startTick] : pendingNotes) {
            int64_t durationTicks = absoluteTimeTicks - startTick;
            double durationSec = (durationTicks * 60.0) / (currentTempoBpm * ticksPerQuarter);
            notes.push_back({endSec, durationSec, (int)note, -1});
        }

        // Build the track with name and instrument
        song.tracks[t] = { .name = "Track " + std::to_string(t), .instrument = "unknown", .notes = std::move(notes) };
    }

    return song;
}

std::optional<MidiTrack> getTrack(const MidiSong& song, size_t trackIndex) {
    if (trackIndex < song.tracks.size()) {
        return song.tracks[trackIndex];
    }
    return std::nullopt;
}