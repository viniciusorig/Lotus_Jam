#include "dsp/bass_mapper.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

std::vector<Fingering> mapToInstrument(const std::vector<MidiNote>& notes, const Tuning& tuning, int maxFret) {
    std::vector<Fingering> out;
    if (tuning.strings.empty() || notes.empty()) return out;
    const int lo = *std::min_element(tuning.strings.begin(), tuning.strings.end());
    const int hi = *std::max_element(tuning.strings.begin(), tuning.strings.end()) + maxFret;

    // One shift for the whole track keeps the melody's shape; ties go to the smallest shift.
    int bestShift = 0, bestCount = -1;
    for (int k : {0, -1, 1, -2, 2, -3, 3, -4, 4}) {
        int count = 0;
        for (const auto& n : notes) count += n.midi + 12 * k >= lo && n.midi + 12 * k <= hi;
        if (count > bestCount) bestCount = count, bestShift = k;
    }

    // Candidates per note (string, fret), after folding the outliers into range.
    struct Cand { int string, fret; };
    std::vector<std::vector<Cand>> cands(notes.size());
    out.resize(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        int m = notes[i].midi + 12 * bestShift;
        while (m < lo) m += 12;
        while (m > hi) m -= 12;
        out[i].midi = m;
        for (int s = 0; s < int(tuning.strings.size()); ++s) {
            const int fret = m - tuning.strings[size_t(s)];
            if (fret >= 0 && fret <= maxFret) cands[i].push_back({s, fret});
        }
    }

    // Viterbi: cheapest path through the candidates (fret jump + string jump + a little for height).
    auto cost = [](const Cand& a, const Cand& b) {
        return std::abs(a.fret - b.fret) + 0.5 * std::abs(a.string - b.string) + 0.05 * b.fret;
    };
    const double kInf = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> best(notes.size());
    std::vector<std::vector<int>> from(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        best[i].assign(cands[i].size(), kInf);
        from[i].assign(cands[i].size(), -1);
        for (size_t c = 0; c < cands[i].size(); ++c) {
            if (i == 0) {
                best[i][c] = 0.05 * cands[i][c].fret;
                continue;
            }
            for (size_t p = 0; p < cands[i - 1].size(); ++p) {
                const double v = best[i - 1][p] + cost(cands[i - 1][p], cands[i][c]);
                if (v < best[i][c]) best[i][c] = v, from[i][c] = int(p);
            }
        }
    }
    int c = int(std::min_element(best.back().begin(), best.back().end()) - best.back().begin());
    for (size_t i = notes.size(); i-- > 0;) {
        out[i].string = cands[i][size_t(c)].string;
        out[i].fret = cands[i][size_t(c)].fret;
        c = from[i][size_t(c)];
    }
    return out;
}
