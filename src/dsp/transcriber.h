#pragma once
#include <vector>

#include "dsp/midi_parser.h"
#include "dsp/onset.h"
#include "dsp/yin.h"

// Monophonic audio-to-MIDI transcription.
// Feeds audio samples, detects notes (pitch + timing), outputs MidiNotes.
// Uses YIN for pitch detection and OnsetDetector for note boundaries.
class Transcriber {
public:
    explicit Transcriber(double sampleRate, double minRms = 0.01);

    // Feed audio samples. Call repeatedly with consecutive blocks.
    void feed(const float* samples, size_t count);

    // Finalize transcription (flush pending note).
    void finish();

    // Get transcribed notes (call after finish()).
    const std::vector<MidiNote>& notes() const { return notes_; }

    // Clear state for a new transcription.
    void reset();

    // Estimated BPM from note onsets (simple heuristic).
    double estimatedBpm() const;

private:
    void processFrame();
    void endCurrentNote(double endTime);

    double sr_;
    double minRms_;
    OnsetDetector onset_;
    Yin yin_;

    std::vector<float> buf_;
    size_t bufPos_ = 0;
    int64_t totalFrames_ = 0;

    static constexpr size_t kHopSize = 512;

    // Current note state
    bool noteActive_ = false;
    double noteStart_ = 0.0;
    int noteMidi_ = -1;
    std::vector<int> notePitches_;  // pitches during current note for averaging

    std::vector<MidiNote> notes_;
    std::vector<double> onsetTimes_;  // for BPM estimation
    std::vector<int64_t> onsetsTemp_;  // temp for OnsetDetector
};
