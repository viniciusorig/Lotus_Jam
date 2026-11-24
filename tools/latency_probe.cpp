// Round-trip audio latency probe (output -> your loopback -> input), on the real PipeWire backend.
//
// Plays short chirps on the output and measures how long each takes to come back on the input,
// using the backend's own timestamps (the same ones the app scores with). Each chirp is found
// by normalised cross-correlation with the known signal, so noise and hum do not fool it, and
// a match that is not clearly above the noise is reported as "not heard". The result is the
// value for the app's COMPENSAÇÃO: it needs a physical loop (a cable from the output to the
// input, or the speaker next to the microphone).
//
//   latency-probe                      lists devices
//   latency-probe <input> <output>     measures (node names as listed)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "app/input_timeline.h"
#include "audio/pipewire_backend.h"

namespace {
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kClicks = 8;
constexpr int64_t kFirst = int64_t(2.5 * kSr);  // first chirp, output frames (lets a sleeping sink wake up)
constexpr int64_t kPeriod = int64_t(1.2 * kSr);
constexpr float kAmp = 0.5f;
constexpr size_t kChirp = 1440;  // 30 ms, 500 -> 6000 Hz, Hann-windowed
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::puts("Entradas:");
        for (auto& d : listInputDevices()) std::printf("  %s  (%s)\n", d.name.c_str(), d.description.c_str());
        std::puts("Saídas:");
        for (auto& d : listOutputDevices()) std::printf("  %s  (%s)\n", d.name.c_str(), d.description.c_str());
        std::puts("\nUso: latency-probe <entrada> <saída>");
        return 2;
    }
    std::vector<float> chirp(kChirp);
    for (size_t i = 0; i < kChirp; ++i) {
        const double t = double(i) / kSr, T = double(kChirp) / kSr;
        const double phase = 2 * kPi * (500.0 * t + 0.5 * (5500.0 / T) * t * t);
        chirp[i] = kAmp * float(0.5 * (1 - std::cos(2 * kPi * double(i) / double(kChirp - 1))) * std::sin(phase));
    }

    PipeWireBackend be(argv[1], argv[2]);
    const float* c = chirp.data();
    be.setRenderer([c](float* out, size_t frames, int64_t first) {
        for (size_t i = 0; i < frames; ++i) {
            const int64_t f = first + int64_t(i) - kFirst;
            float v = 0.0f;
            if (f >= 0 && f / kPeriod < kClicks && size_t(f % kPeriod) < kChirp) v = c[f % kPeriod];
            out[2 * i] = out[2 * i + 1] = v;
        }
    });
    std::string err;
    if (!be.start(err)) {
        std::fprintf(stderr, "Erro: %s\n", err.c_str());
        return 1;
    }

    std::vector<float> in;
    InputTimeline timeline(kSr);
    ClockMap outMap;
    std::vector<float> buf(4096);
    std::vector<TimePoint> tp(64);
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(int(1000 * (2.5 + kClicks * 1.2 + 1.0)));
    while (std::chrono::steady_clock::now() < end) {
        size_t n;
        while ((n = be.input().read(buf.data(), buf.size())) > 0) in.insert(in.end(), buf.begin(), buf.begin() + long(n));
        while ((n = be.inputTimes().read(tp.data(), tp.size())) > 0)
            timeline.add(tp.data(), n, be.inputDroppedFrames(), be.nowSec());
        while ((n = be.outputTimes().read(tp.data(), tp.size())) > 0)
            for (size_t i = 0; i < n; ++i) outMap.add(tp[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const uint32_t xruns = be.xruns();
    const uint64_t dropped = be.inputDroppedFrames();
    be.stop();

    double peak = 0, sq = 0;
    const size_t quiet = std::min(in.size(), size_t(2.0 * kSr));  // before the first chirp
    for (size_t i = 0; i < in.size(); ++i) peak = std::max(peak, double(std::fabs(in[i])));
    for (size_t i = 0; i < quiet; ++i) sq += double(in[i]) * in[i];
    std::printf("entrada: pico %.2f, ruído de fundo (rms) %.4f\n", peak, std::sqrt(sq / double(std::max<size_t>(1, quiet))));
    if (peak >= 0.98) std::puts("AVISO: a entrada está saturando; baixe o volume ou afaste o microfone.");

    double echirp = 0;
    for (float v : chirp) echirp += double(v) * v;

    std::vector<double> lat;
    for (int k = 0; k < kClicks; ++k) {
        const int64_t e = kFirst + k * kPeriod;  // input and output frames are not aligned: search around it
        const int64_t lo = std::max<int64_t>(0, e - int64_t(0.4 * kSr));
        const int64_t hi = std::min<int64_t>(int64_t(in.size()) - int64_t(kChirp), e + int64_t(0.8 * kSr));
        if (hi <= lo) {
            std::printf("clique %d: sem áudio\n", k + 1);
            continue;
        }
        // Normalised cross-correlation at every lag; the best one must stand out of the rest.
        std::vector<double> ncc(size_t(hi - lo));
        double ein = 0;
        for (size_t i = 0; i < kChirp; ++i) ein += double(in[size_t(lo) + i]) * in[size_t(lo) + i];
        for (int64_t j = lo; j < hi; ++j) {
            double dot = 0;
            for (size_t i = 0; i < kChirp; ++i) dot += double(in[size_t(j) + i]) * chirp[i];
            ncc[size_t(j - lo)] = std::fabs(dot) / std::sqrt(std::max(ein, 1e-12) * echirp);
            if (j + int64_t(kChirp) < int64_t(in.size())) {
                ein += double(in[size_t(j) + kChirp]) * in[size_t(j) + kChirp] - double(in[size_t(j)]) * in[size_t(j)];
            }
        }
        const size_t best = size_t(std::max_element(ncc.begin(), ncc.end()) - ncc.begin());
        std::vector<double> sorted = ncc;
        std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
        const double median = sorted[sorted.size() / 2];
        if (ncc[best] < 0.25 || ncc[best] < 5.0 * median) {
            std::printf("clique %d: não ouvido (correlação %.2f)\n", k + 1, ncc[best]);
            continue;
        }
        const double audible = outMap.timeOf(double(e));
        const double ms = (timeline.timeOf(double(lo) + double(best)) - audible) * 1000.0;
        std::printf("clique %d: %.1f ms (correlação %.2f)\n", k + 1, ms, ncc[best]);
        lat.push_back(ms);
    }
    std::printf("xruns: %u, amostras perdidas: %llu\n", xruns, (unsigned long long)dropped);
    if (lat.size() < 3) {
        std::puts("Poucos cliques ouvidos: aproxime o microfone do alto-falante ou aumente o volume.");
        return 1;
    }
    std::sort(lat.begin(), lat.end());
    std::printf("\nLatência de ida e volta: mediana %.1f ms (mín %.1f, máx %.1f, %zu cliques)\n", lat[lat.size() / 2],
                lat.front(), lat.back(), lat.size());
    if (lat.back() - lat.front() > 10.0) std::puts("AVISO: os valores variam mais de 10 ms; a medição não é confiável.");
    return 0;
}
