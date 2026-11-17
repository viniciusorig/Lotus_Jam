#include "dsp/notes.h"

#include <algorithm>
#include <cmath>

double midiToHz(double midi, double a4) { return a4 * std::pow(2.0, (midi - 69.0) / 12.0); }
double hzToMidi(double hz, double a4) { return 69.0 + 12.0 * std::log2(hz / a4); }

std::string noteName(int midi) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int pc = ((midi % 12) + 12) % 12;
    int octave = (midi - pc) / 12 - 1;
    return std::string(names[pc]) + std::to_string(octave);
}

double centsBetween(double hz, double refHz) { return 1200.0 * std::log2(hz / refHz); }

namespace {

Instrument make(std::string id, std::string label, std::vector<int> standard, bool dadgad) {
    Instrument in{std::move(id), std::move(label), 0, 0, {}};
    in.tunings.push_back({"Padrão", standard});

    // Drop D: lowers the lowest string whose standard pitch is E by a whole tone.
    Tuning drop{"Drop D", standard};
    for (int& s : drop.strings)
        if (s % 12 == 4) {
            s -= 2;
            break;
        }
    in.tunings.push_back(drop);

    Tuning half{"Meio-tom abaixo", standard};
    for (int& s : half.strings) s -= 1;
    in.tunings.push_back(half);

    if (dadgad) in.tunings.push_back({"DADGAD", {38, 45, 50, 55, 57, 62}});

    int lo = 1000, hi = -1000;
    for (const auto& t : in.tunings) {
        lo = std::min(lo, *std::min_element(t.strings.begin(), t.strings.end()));
        hi = std::max(hi, *std::max_element(t.strings.begin(), t.strings.end()));
    }
    in.minHz = 0.9 * midiToHz(lo);
    in.maxHz = 1.2 * midiToHz(hi);
    return in;
}

}  // namespace

const std::vector<Instrument>& instruments() {
    static const std::vector<Instrument> all = {
        make("violao", "Violão", {40, 45, 50, 55, 59, 64}, true),
        make("guitarra", "Guitarra", {40, 45, 50, 55, 59, 64}, true),
        make("baixo4", "Baixo 4 cordas", {28, 33, 38, 43}, false),
        make("baixo5", "Baixo 5 cordas", {23, 28, 33, 38, 43}, false),
        make("baixo6", "Baixo 6 cordas", {23, 28, 33, 38, 43, 48}, false),
    };
    return all;
}

StringMatch nearestString(const Tuning& tuning, double hz, double a4) {
    StringMatch best{0, 0.0, 0.0};
    double bestAbs = 1e30;
    for (size_t i = 0; i < tuning.strings.size(); ++i) {
        double target = midiToHz(tuning.strings[i], a4);
        double c = centsBetween(hz, target);
        if (std::fabs(c) < bestAbs) {
            bestAbs = std::fabs(c);
            best = {static_cast<int>(i), target, c};
        }
    }
    return best;
}
