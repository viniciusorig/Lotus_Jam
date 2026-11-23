#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/midi_parser.h"

// Simple sine-wave MIDI synthesizer for backing track playback.
// Renders all notes from a song as sine waves with ADSR envelope.
// Thread-safe: render() may be called from the audio callback.
class MidiSynth {
public:
    // startOffsetFrames: output frame at which song time 0 is heard (after the count-in).
    MidiSynth(double sampleRate, const std::vector<MidiNote>& notes, double speedFactor,
              int64_t startOffsetFrames = 0);

    // Renders stereo interleaved samples for the given frame range.
    // firstFrame is absolute output frame; output is added to existing buffer content.
    void render(float* stereoInterleaved, size_t frames, int64_t firstFrame) noexcept;

    // Duration of the song in frames (after speed adjustment), counted from song time 0.
    int64_t durationFrames() const noexcept { return durationFrames_; }

private:
    struct Voice {
        double startFrame;
        double endFrame;
        double freq;
        double phase = 0.0;
    };

    double sr_;
    double speed_;
    int64_t durationFrames_;
    std::vector<Voice> voices_;

    static constexpr double kAttackSec = 0.01;
    static constexpr double kReleaseSec = 0.05;
    static constexpr double kVolume = 0.15;  // quieter than click
};
