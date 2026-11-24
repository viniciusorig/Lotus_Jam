#include "score/note_scorer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

NoteScorer::NoteScorer(const std::vector<MidiNote>& notes, double speedFactor,
                       double compensationSec, double songStartSec)
    : notes_(notes), speed_(speedFactor > 0 ? speedFactor : 1.0),
      comp_(compensationSec), start_(songStartSec) {
    // Sort notes by start time
    // Stable: the piano roll indexes notes in the parser's order, equal starts (chords) included.
    std::stable_sort(notes_.begin(), notes_.end(),
              [](const MidiNote& a, const MidiNote& b) { return a.start < b.start; });
    state_.resize(notes_.size());
}

double NoteScorer::noteTime(size_t i) const {
    return start_ + notes_[i].start / speed_;
}

bool NoteScorer::pitchMatches(int expected, int played, double cents) const {
    // Same note = match; also accept octave equivalents if within cents tolerance
    if (expected == played) return std::fabs(cents) <= kPitchToleranceCents;
    // Check octave equivalents (e.g., bass sounds octave lower)
    if ((expected % 12) == (played % 12)) return std::fabs(cents) <= kPitchToleranceCents;
    return false;
}

std::optional<NoteResult> NoteScorer::onNote(const DetectedNote& played) {
    const double t = played.tSec - comp_;

    // Find the closest unmatched note within the timing window
    size_t bestIdx = SIZE_MAX;
    double bestErr = 1e30;

    for (size_t i = 0; i < notes_.size(); ++i) {
        if (state_[i].matched) continue;

        const double expected = noteTime(i);
        const double errMs = (t - expected) * 1000.0;

        // Within the matching window?
        if (std::fabs(errMs) <= kTimingOkMs && std::fabs(errMs) < bestErr) {
            bestErr = std::fabs(errMs);
            bestIdx = i;
        }
    }

    // No matching note found = Extra
    if (bestIdx == SIZE_MAX) {
        ++extras_;
        return NoteResult{NoteResult::Extra, -1, -1, played.midi, 0.0, 0.0};
    }

    // Found a note to match against
    const MidiNote& expected = notes_[bestIdx];
    const double errMs = (t - noteTime(bestIdx)) * 1000.0;
    const bool pitchOk = pitchMatches(expected.midi, played.midi, played.cents);

    NoteResult r;
    r.noteIndex = static_cast<int>(bestIdx);
    r.expectedMidi = expected.midi;
    r.playedMidi = played.midi;
    r.errorMs = errMs;

    if (!pitchOk) {
        r.kind = NoteResult::WrongNote;
        r.score = 0.0;
    } else if (std::fabs(errMs) <= kTimingPerfectMs) {
        r.kind = NoteResult::Hit;
        // Score: 100 at 0ms, linearly down to ~30 at 60ms
        r.score = std::max(0.0, 100.0 * (1.0 - std::fabs(errMs) / (kTimingPerfectMs * 1.5)));
    } else if (errMs < 0) {
        r.kind = NoteResult::Early;
        r.score = std::max(0.0, 50.0 * (1.0 - (std::fabs(errMs) - kTimingPerfectMs) / kTimingPerfectMs));
    } else {
        r.kind = NoteResult::Late;
        r.score = std::max(0.0, 50.0 * (1.0 - (errMs - kTimingPerfectMs) / kTimingPerfectMs));
    }

    state_[bestIdx].matched = true;
    state_[bestIdx].result = r;
    done_.push_back(r);

    return r;
}

void NoteScorer::advance(double nowSec, std::vector<NoteResult>& missesOut) {
    const double t = nowSec - comp_;

    while (nextFinal_ < notes_.size()) {
        const double expected = noteTime(nextFinal_);
        // Finalize if we're past the late window
        if (t < expected + kTimingOkMs / 1000.0 + 0.1) break;

        if (!state_[nextFinal_].matched) {
            NoteResult r;
            r.kind = NoteResult::Miss;
            r.noteIndex = static_cast<int>(nextFinal_);
            r.expectedMidi = notes_[nextFinal_].midi;
            r.playedMidi = -1;
            r.errorMs = 0.0;
            r.score = 0.0;

            state_[nextFinal_].matched = true;
            state_[nextFinal_].result = r;
            done_.push_back(r);
            missesOut.push_back(r);
        }
        ++nextFinal_;
    }
}

std::vector<std::pair<size_t, const MidiNote*>> NoteScorer::upcoming(double nowSec, size_t maxCount) const {
    std::vector<std::pair<size_t, const MidiNote*>> result;
    const double t = nowSec - comp_;

    for (size_t i = 0; i < notes_.size() && result.size() < maxCount; ++i) {
        const double expected = noteTime(i);
        // Show notes from 1 second before to 5 seconds ahead
        if (expected >= t - 1.0 && expected <= t + 5.0) {
            result.emplace_back(i, &notes_[i]);
        }
    }
    return result;
}

NoteScorer::Summary NoteScorer::summary(size_t lastN) const {
    Summary s{};
    s.totalNotes = static_cast<int>(notes_.size());
    s.extras = extras_;

    if (done_.empty()) return s;

    double sumScore = 0, sumErr = 0, sumErr2 = 0;
    int errCount = 0;

    for (const auto& r : done_) {
        sumScore += r.score;
        switch (r.kind) {
            case NoteResult::Hit: ++s.hits; break;
            case NoteResult::WrongNote: ++s.wrongNotes; break;
            case NoteResult::Early: ++s.early; break;
            case NoteResult::Late: ++s.late; break;
            case NoteResult::Miss: ++s.misses; break;
            case NoteResult::Extra: break;
        }
        if (r.kind == NoteResult::Hit || r.kind == NoteResult::Early || r.kind == NoteResult::Late) {
            sumErr += r.errorMs;
            sumErr2 += r.errorMs * r.errorMs;
            ++errCount;
        }
    }

    s.total = sumScore / static_cast<double>(done_.size());

    // Last N
    size_t start = done_.size() > lastN ? done_.size() - lastN : 0;
    double lastSum = 0;
    for (size_t i = start; i < done_.size(); ++i) lastSum += done_[i].score;
    s.lastN = lastSum / static_cast<double>(done_.size() - start);

    if (errCount > 0) {
        s.meanErrMs = sumErr / errCount;
        double var = sumErr2 / errCount - s.meanErrMs * s.meanErrMs;
        s.stdErrMs = var > 0 ? std::sqrt(var) : 0;
    }

    return s;
}
