#pragma once
#include <QWidget>
#include <vector>

#include "dsp/midi_parser.h"
#include "score/note_scorer.h"

// Displays MIDI notes as a scrolling piano roll.
// Notes scroll from right to left; a vertical line marks "now".
// Colors: green = hit, yellow = early/late, red = wrong/miss, white = upcoming.
class PianoRollWidget : public QWidget {
    Q_OBJECT
public:
    explicit PianoRollWidget(QWidget* parent = nullptr);

    // Set the song to display (call before starting).
    void setSong(const MidiSong& song, int trackIndex);

    // Clear the display.
    void clear();

    // Update the current playback time (call at ~60 Hz).
    void setTime(double nowSec);

    // Mark a note with its result (call when NoteScorer produces a result).
    void setNoteResult(int noteIndex, NoteResult::Kind kind);

    // Accessors for testing
    int noteCount() const { return static_cast<int>(notes_.size()); }
    double currentTime() const { return now_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct DisplayNote {
        double start, duration;
        int midi;
        NoteResult::Kind result = NoteResult::Miss;  // default = not yet played
        bool hasResult = false;
    };

    std::vector<DisplayNote> notes_;
    double now_ = 0.0;
    double windowBefore_ = 1.0;  // seconds to show before "now"
    double windowAfter_ = 4.0;   // seconds to show after "now"

    // MIDI range for display
    int midiLow_ = 28;   // E1 (5-string bass low)
    int midiHigh_ = 72;  // C5

    // Cached geometry
    double pxPerSec_ = 100.0;
    double pxPerNote_ = 10.0;
    int nowLineX_ = 0;

    void recalcGeometry();
    QColor colorFor(const DisplayNote& n) const;
};
