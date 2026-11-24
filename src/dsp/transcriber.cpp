#include "dsp/transcriber.h"

#include <algorithm>
#include <cmath>
#include <numeric>

Transcriber::Transcriber(double sampleRate, double minRms)
    : sr_(sampleRate), minRms_(minRms), onset_(sampleRate),
      yin_(sampleRate, 50.0, 2000.0) {  // Bass/guitar range: 50 Hz to 2000 Hz
    buf_.resize(yin_.requiredSamples(), 0.0f);
}

void Transcriber::reset() {
    bufPos_ = 0;
    totalFrames_ = 0;
    noteActive_ = false;
    noteStart_ = 0.0;
    noteMidi_ = -1;
    notePitches_.clear();
    notes_.clear();
    onsetTimes_.clear();
    onsetsTemp_.clear();
    std::fill(buf_.begin(), buf_.end(), 0.0f);
}

void Transcriber::feed(const float* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        buf_[bufPos_++] = samples[i];
        if (bufPos_ == buf_.size()) {  // window full: analyse, then wait for kHopSize new samples
            processFrame();
            const size_t keep = buf_.size() - kHopSize;
            std::copy(buf_.begin() + kHopSize, buf_.end(), buf_.begin());
            bufPos_ = keep;
            totalFrames_ += kHopSize;
        }
    }
}

void Transcriber::processFrame() {
    // Time of the newest sample of the window.
    const double currentTime = static_cast<double>(totalFrames_ + buf_.size()) / sr_;

    // Check for onset: the detector is streaming, so it sees every sample once (the whole
    // window the first time, then only the newest hop).
    onsetsTemp_.clear();
    if (totalFrames_ == 0)
        onset_.feed(buf_.data(), buf_.size(), onsetsTemp_);
    else
        onset_.feed(buf_.data() + buf_.size() - kHopSize, kHopSize, onsetsTemp_);
    bool isOnset = !onsetsTemp_.empty();

    // Calculate RMS
    double sumSq = 0.0;
    for (size_t i = 0; i < buf_.size(); ++i) {
        sumSq += buf_[i] * buf_[i];
    }
    double rms = std::sqrt(sumSq / buf_.size());

    // Detect pitch if loud enough
    int detectedMidi = -1;
    if (rms >= minRms_) {
        PitchResult pr = yin_.process(buf_.data());
        if (pr.probability > 0.8 && pr.hz > 20.0 && pr.hz < 5000.0) {
            // Convert frequency to MIDI note number
            double midiFloat = 69.0 + 12.0 * std::log2(pr.hz / 440.0);
            detectedMidi = static_cast<int>(std::round(midiFloat));
            detectedMidi = std::clamp(detectedMidi, 0, 127);
        }
    }

    // State machine for note detection
    if (isOnset && detectedMidi >= 0) {
        // New onset with valid pitch
        if (noteActive_) {
            endCurrentNote(currentTime);
        }
        noteActive_ = true;
        noteStart_ = currentTime;
        noteMidi_ = detectedMidi;
        notePitches_.clear();
        notePitches_.push_back(detectedMidi);
        if (onsetTimes_.size() < kMaxNotes) onsetTimes_.push_back(currentTime);
    } else if (noteActive_) {
        if (detectedMidi < 0 || rms < minRms_ * 0.5) {
            // Note ended (silence or too quiet)
            endCurrentNote(currentTime);
        } else if (std::abs(detectedMidi - noteMidi_) > 2) {
            // Pitch changed significantly without onset - might be a new note
            endCurrentNote(currentTime);
            noteActive_ = true;
            noteStart_ = currentTime;
            noteMidi_ = detectedMidi;
            notePitches_.clear();
            notePitches_.push_back(detectedMidi);
        } else {
            // Continue current note, track pitch for averaging
            notePitches_.push_back(detectedMidi);
        }
    } else if (detectedMidi >= 0 && rms >= minRms_) {
        // Start note without explicit onset (gradual entry)
        noteActive_ = true;
        noteStart_ = currentTime;
        noteMidi_ = detectedMidi;
        notePitches_.clear();
        notePitches_.push_back(detectedMidi);
    }
}

void Transcriber::endCurrentNote(double endTime) {
    if (!noteActive_) return;

    double duration = endTime - noteStart_;
    if (duration >= 0.05 && !notePitches_.empty()) {  // minimum 50ms note
        // Use median pitch for robustness
        std::vector<int> sorted = notePitches_;
        std::sort(sorted.begin(), sorted.end());
        int medianMidi = sorted[sorted.size() / 2];

        if (notes_.size() < kMaxNotes) notes_.push_back({noteStart_, duration, medianMidi, -1});
    }

    noteActive_ = false;
    noteMidi_ = -1;
    notePitches_.clear();
}

void Transcriber::finish() {
    // End any active note (a partial window is not analysed)
    double endTime = static_cast<double>(totalFrames_ + bufPos_) / sr_;
    endCurrentNote(endTime);
}

double Transcriber::estimatedBpm() const {
    if (onsetTimes_.size() < 4) return 120.0;  // default

    // Calculate inter-onset intervals
    std::vector<double> intervals;
    for (size_t i = 1; i < onsetTimes_.size(); ++i) {
        double interval = onsetTimes_[i] - onsetTimes_[i - 1];
        if (interval > 0.1 && interval < 2.0) {  // reasonable range
            intervals.push_back(interval);
        }
    }

    if (intervals.empty()) return 120.0;

    // Median interval
    std::sort(intervals.begin(), intervals.end());
    double medianInterval = intervals[intervals.size() / 2];

    // Convert to BPM (assume quarter notes)
    double bpm = 60.0 / medianInterval;

    // Clamp to reasonable range
    bpm = std::clamp(bpm, 40.0, 240.0);

    return bpm;
}
