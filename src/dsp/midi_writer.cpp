#include "dsp/midi_writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

void MidiWriter::writeVarLen(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t buf[4];
    int n = 0;
    buf[n++] = value & 0x7F;
    while ((value >>= 7) > 0) {
        buf[n++] = (value & 0x7F) | 0x80;
    }
    while (n > 0) out.push_back(buf[--n]);
}

void MidiWriter::writeU16BE(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void MidiWriter::writeU32BE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

bool MidiWriter::write(const std::string& path, const std::vector<MidiNote>& notes,
                       double bpm, int quantizeGrid) {
    if (notes.empty()) return false;

    const int division = 480;  // ticks per quarter note
    const double ticksPerSec = division * bpm / 60.0;
    const double quantizeTicks = quantizeGrid > 0 ? division / static_cast<double>(quantizeGrid) : 0;

    // Sort notes by start time
    std::vector<MidiNote> sorted = notes;
    std::sort(sorted.begin(), sorted.end(),
              [](const MidiNote& a, const MidiNote& b) { return a.start < b.start; });

    // Build track data
    std::vector<uint8_t> track;

    // Tempo meta event: FF 51 03 tt tt tt (microseconds per quarter)
    uint32_t usPerQuarter = static_cast<uint32_t>(60000000.0 / bpm);
    writeVarLen(track, 0);  // delta time 0
    track.push_back(0xFF);
    track.push_back(0x51);
    track.push_back(0x03);
    track.push_back(static_cast<uint8_t>((usPerQuarter >> 16) & 0xFF));
    track.push_back(static_cast<uint8_t>((usPerQuarter >> 8) & 0xFF));
    track.push_back(static_cast<uint8_t>(usPerQuarter & 0xFF));

    // Track name meta event
    const char* trackName = "Transcription";
    writeVarLen(track, 0);
    track.push_back(0xFF);
    track.push_back(0x03);
    writeVarLen(track, static_cast<uint32_t>(strlen(trackName)));
    for (const char* p = trackName; *p; ++p) track.push_back(static_cast<uint8_t>(*p));

    // Convert notes to MIDI events
    struct Event {
        uint32_t tick;
        uint8_t status;
        uint8_t note;
        uint8_t velocity;
    };
    std::vector<Event> events;

    for (const auto& n : sorted) {
        double startTicks = n.start * ticksPerSec;
        double endTicks = (n.start + n.duration) * ticksPerSec;

        if (quantizeTicks > 0) {
            startTicks = std::round(startTicks / quantizeTicks) * quantizeTicks;
            // Ensure minimum duration of one grid unit
            endTicks = std::max(endTicks, startTicks + quantizeTicks);
        }

        uint32_t startTick = static_cast<uint32_t>(std::max(0.0, startTicks));
        uint32_t endTick = static_cast<uint32_t>(endTicks);
        uint8_t midi = static_cast<uint8_t>(std::clamp(n.midi, 0, 127));

        events.push_back({startTick, 0x90, midi, 100});  // note on, velocity 100
        events.push_back({endTick, 0x80, midi, 0});      // note off
    }

    // Sort events by tick, note-off before note-on at same tick
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return (a.status & 0xF0) < (b.status & 0xF0);  // 0x80 < 0x90
    });

    // Write events with delta times
    uint32_t lastTick = 0;
    for (const auto& e : events) {
        uint32_t delta = e.tick - lastTick;
        lastTick = e.tick;
        writeVarLen(track, delta);
        track.push_back(e.status);  // channel 0
        track.push_back(e.note);
        track.push_back(e.velocity);
    }

    // End of track meta event
    writeVarLen(track, 0);
    track.push_back(0xFF);
    track.push_back(0x2F);
    track.push_back(0x00);

    // Build complete file
    std::vector<uint8_t> file;

    // Header chunk: MThd
    file.push_back('M'); file.push_back('T'); file.push_back('h'); file.push_back('d');
    writeU32BE(file, 6);           // header length
    writeU16BE(file, 0);           // format 0 (single track)
    writeU16BE(file, 1);           // 1 track
    writeU16BE(file, static_cast<uint16_t>(division));

    // Track chunk: MTrk
    file.push_back('M'); file.push_back('T'); file.push_back('r'); file.push_back('k');
    writeU32BE(file, static_cast<uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());

    // Write to file
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return out.good();
}
