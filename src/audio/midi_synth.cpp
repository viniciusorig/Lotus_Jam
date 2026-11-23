#include "audio/midi_synth.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586;

double midiToFreq(int midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}
}  // namespace

MidiSynth::MidiSynth(double sampleRate, const std::vector<MidiNote>& notes, double speedFactor,
                     int64_t startOffsetFrames)
    : sr_(sampleRate), speed_(speedFactor > 0 ? speedFactor : 1.0) {
    double maxEnd = 0.0;
    for (const auto& n : notes) {
        Voice v;
        v.startFrame = (n.start / speed_) * sr_;
        v.endFrame = ((n.start + n.duration) / speed_) * sr_;
        v.freq = midiToFreq(n.midi);
        maxEnd = std::max(maxEnd, v.endFrame);
        v.startFrame += double(startOffsetFrames);
        v.endFrame += double(startOffsetFrames);
        voices_.push_back(v);
    }
    durationFrames_ = static_cast<int64_t>(maxEnd + sr_);  // 1 second grace
}

void MidiSynth::render(float* stereoInterleaved, size_t frames, int64_t firstFrame) noexcept {
    const double attackFrames = kAttackSec * sr_;
    const double releaseFrames = kReleaseSec * sr_;

    for (auto& v : voices_) {
        const int64_t noteStart = static_cast<int64_t>(v.startFrame);
        const int64_t noteEnd = static_cast<int64_t>(v.endFrame);
        const int64_t blockEnd = firstFrame + static_cast<int64_t>(frames);

        // Skip if note is entirely before or after this block
        if (noteEnd + static_cast<int64_t>(releaseFrames) < firstFrame) continue;
        if (noteStart > blockEnd) continue;

        const double freqRad = kTwoPi * v.freq / sr_;

        for (size_t i = 0; i < frames; ++i) {
            const int64_t frame = firstFrame + static_cast<int64_t>(i);
            if (frame < noteStart) continue;
            if (frame > noteEnd + static_cast<int64_t>(releaseFrames)) break;

            // ADSR envelope (simple attack + sustain + release)
            double env = 1.0;
            const double fromStart = static_cast<double>(frame - noteStart);
            const double fromEnd = static_cast<double>(frame - noteEnd);

            if (fromStart < attackFrames) {
                env = fromStart / attackFrames;
            } else if (fromEnd > 0) {
                env = std::max(0.0, 1.0 - fromEnd / releaseFrames);
            }

            // Update phase continuously from note start
            const double t = fromStart;
            const double sample = std::sin(freqRad * t) * env * kVolume;

            stereoInterleaved[i * 2] += static_cast<float>(sample);
            stereoInterleaved[i * 2 + 1] += static_cast<float>(sample);
        }
    }
}
