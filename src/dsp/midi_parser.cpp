#include "midi_parser.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>

namespace {

uint32_t be(const std::vector<uint8_t>& d, size_t p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | d[p + i];
    return v;
}

// Reads a variable-length quantity; returns false if the data runs out.
bool vlq(const std::vector<uint8_t>& d, size_t& p, size_t end, uint32_t& v) {
    v = 0;
    for (int i = 0; i < 4 && p < end; ++i) {
        uint8_t b = d[p++];
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) return true;
    }
    return false;
}

struct RawNote { uint64_t on, off; int midi; };

}  // namespace

MidiSong parseMidiFile(const std::string& filePath) {
    MidiSong song;
    song.tempoBpm = 120.0;
    song.division = 480;

    std::ifstream f(filePath, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 14 || !std::equal(d.begin(), d.begin() + 4, "MThd") || be(d, 4, 4) < 6) return song;

    const uint16_t numTracks = be(d, 10, 2);
    const uint16_t div = be(d, 12, 2);
    if (div & 0x8000) return song;  // shortcut: SMPTE time division unsupported, add if needed
    song.division = div ? div : 480;

    std::map<uint64_t, uint32_t> tempos;  // tick -> µs per quarter (all tracks share one tempo map)
    std::vector<std::pair<std::string, std::vector<RawNote>>> raw;

    size_t pos = 8 + be(d, 4, 4);
    while (raw.size() < numTracks && pos + 8 <= d.size()) {
        const size_t len = be(d, pos + 4, 4);
        const bool isTrk = std::equal(d.begin() + pos, d.begin() + pos + 4, "MTrk");
        const size_t start = pos + 8;
        const size_t end = std::min(d.size(), start + len);
        pos = start + len;
        if (!isTrk) continue;

        std::string name;
        std::vector<RawNote> notes;
        std::map<int, uint64_t> pending;  // key = channel<<8 | note
        uint64_t tick = 0;
        uint8_t running = 0;
        size_t p = start;
        uint32_t delta;

        while (vlq(d, p, end, delta) && p < end) {
            tick += delta;
            uint8_t st = d[p];
            if (st >= 0x80) { ++p; if (st < 0xF0) running = st; } else st = running;
            if (st < 0x80) break;  // data byte with no running status: corrupt

            if (st == 0xFF) {
                if (p >= end) break;
                uint8_t type = d[p++];
                uint32_t n;
                if (!vlq(d, p, end, n) || p + n > end) break;
                if (type == 0x03 && name.empty()) name.assign(d.begin() + p, d.begin() + p + n);
                if (type == 0x51 && n == 3) tempos[tick] = be(d, p, 3);
                p += n;
                if (type == 0x2F) break;
            } else if (st == 0xF0 || st == 0xF7) {
                uint32_t n;
                if (!vlq(d, p, end, n) || p + n > end) break;
                p += n;
            } else {
                const uint8_t kind = st & 0xF0;
                const size_t need = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                if (p + need > end) break;
                const int note = d[p], vel = need == 2 ? d[p + 1] : 0;
                p += need;
                const int key = ((st & 0x0F) << 8) | note;
                if (kind == 0x90 && vel > 0) {
                    pending.emplace(key, tick);  // retrigger of a sounding note is ignored
                } else if (kind == 0x80 || kind == 0x90) {
                    auto it = pending.find(key);
                    if (it != pending.end()) {
                        notes.push_back({it->second, tick, note});
                        pending.erase(it);
                    }
                }
            }
        }
        for (auto& [key, on] : pending) notes.push_back({on, tick, key & 0xFF});
        raw.emplace_back(name, std::move(notes));
    }

    // Tick -> seconds through the tempo map.
    if (!tempos.count(0)) tempos[0] = 500000;
    std::vector<std::tuple<uint64_t, double, uint32_t>> seg;  // tick, seconds at tick, tempo
    double secs = 0;
    uint64_t lastTick = 0;
    uint32_t lastTempo = tempos.begin()->second;
    for (auto& [t, us] : tempos) {
        secs += double(t - lastTick) * lastTempo / 1e6 / song.division;
        seg.emplace_back(t, secs, us);
        lastTick = t;
        lastTempo = us;
    }
    auto toSec = [&](uint64_t t) {
        auto it = std::upper_bound(seg.begin(), seg.end(), t,
                                   [](uint64_t v, const auto& s) { return v < std::get<0>(s); });
        const auto& [st, ss, us] = *--it;
        return ss + double(t - st) * us / 1e6 / song.division;
    };
    song.tempoBpm = 60e6 / std::get<2>(seg.front());

    for (size_t i = 0; i < raw.size(); ++i) {
        MidiTrack tr{raw[i].first.empty() ? "Track " + std::to_string(i) : raw[i].first, "unknown", {}};
        std::sort(raw[i].second.begin(), raw[i].second.end(),
                  [](const RawNote& a, const RawNote& b) { return a.on < b.on; });
        for (auto& n : raw[i].second) {
            double s = toSec(n.on);
            tr.notes.push_back({s, toSec(n.off) - s, n.midi, -1});
        }
        song.tracks.push_back(std::move(tr));
    }
    return song;
}

std::optional<MidiTrack> getTrack(const MidiSong& song, size_t trackIndex) {
    if (trackIndex < song.tracks.size()) return song.tracks[trackIndex];
    return std::nullopt;
}
