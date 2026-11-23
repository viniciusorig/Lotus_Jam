#pragma once
#include <QWidget>
#include <memory>

#include "dsp/midi_parser.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class PianoRollWidget;

// "MÚSICAS & MIDI" tab - import MIDI, select track, practice with note scoring.
class MusicTrainingView : public QWidget {
    Q_OBJECT
public:
    explicit MusicTrainingView(QWidget* parent = nullptr);
    ~MusicTrainingView() override;

    // Current configuration
    QString filePath() const;
    int selectedTrack() const;
    double speedFactor() const;  // 0.5 = 50%, 1.0 = 100%, etc.

    // Session control
    void setRunning(bool running);
    bool isRunning() const { return running_; }
    void setStartAllowed(bool allowed);

    // Update display during playback
    void setTime(double nowSec);
    void setNoteResult(int noteIndex, int kind);  // NoteResult::Kind

    // Access to the song for Session
    const MidiSong& song() const { return song_; }
    bool hasSong() const { return !song_.tracks.empty(); }

    // Test accessors
    PianoRollWidget* pianoRoll() const { return roll_; }
    QPushButton* startButton() const { return start_; }
    QComboBox* trackCombo() const { return trackCombo_; }

Q_SIGNALS:
    void startRequested();
    void stopRequested();
    void configChanged();

private Q_SLOTS:
    void onImport();
    void onTrackChanged(int index);
    void onSpeedChanged(int value);

private:
    void updateTrackCombo();
    void updateSpeedLabel();

    bool running_ = false;
    bool startAllowed_ = false;
    MidiSong song_;
    QString filePath_;

    // Controls
    QPushButton* importBtn_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QComboBox* trackCombo_ = nullptr;
    QSlider* speedSlider_ = nullptr;
    QLabel* speedLabel_ = nullptr;
    QPushButton* start_ = nullptr;

    // Display
    PianoRollWidget* roll_ = nullptr;
    QLabel* statsLabel_ = nullptr;
};
