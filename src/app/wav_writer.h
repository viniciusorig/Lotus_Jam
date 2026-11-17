#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Writes x[0..n) as a 16-bit PCM mono WAV (samples clipped to [-1, 1], scaled by 32767 and
// rounded). The data goes to "<path>.part" first and is renamed to `path` only when complete,
// so a reader never sees a half-written file. Returns false on any I/O error. Blocking I/O:
// never call from a real-time thread.
inline bool writeWav16Mono(const std::string& path, const float* x, size_t n, int sampleRate) {
    if (n > (0xFFFFFFFFu - 36u) / 2u) return false;
    const uint32_t dataBytes = uint32_t(n * 2);
    std::vector<unsigned char> b(44 + size_t(dataBytes));
    auto put32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[at + size_t(i)] = (unsigned char)(v >> (8 * i));
    };
    auto put16 = [&](size_t at, uint16_t v) {
        b[at] = (unsigned char)(v & 0xFF);
        b[at + 1] = (unsigned char)(v >> 8);
    };
    auto tag = [&](size_t at, const char* s) { std::copy(s, s + 4, b.begin() + long(at)); };
    tag(0, "RIFF");
    put32(4, 36 + dataBytes);
    tag(8, "WAVE");
    tag(12, "fmt ");
    put32(16, 16);
    put16(20, 1);  // PCM
    put16(22, 1);  // mono
    put32(24, uint32_t(sampleRate));
    put32(28, uint32_t(sampleRate) * 2);
    put16(32, 2);
    put16(34, 16);
    tag(36, "data");
    put32(40, dataBytes);
    for (size_t i = 0; i < n; ++i) {
        const float v = std::isfinite(x[i]) ? std::clamp(x[i], -1.0f, 1.0f) : 0.0f;
        put16(44 + 2 * i, uint16_t(int16_t(std::lround(double(v) * 32767.0))));
    }
    const std::string tmp = path + ".part";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    if (std::fclose(f) != 0 || !ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}
