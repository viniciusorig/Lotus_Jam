#pragma once
#include <QWidget>
#include <memory>

#include "dsp/midi_parser.h"
#include "dsp/notes.h"

class QCheckBox;
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
    bool isPaused() const { return paused_; }
    void setPaused(bool paused);  // stopped but resumable: "Continuar" instead of "Pausar"
    void setStartAllowed(bool allowed);
    void loadFile(const QString& path);  // parse a MIDI file and show it (onImport minus the dialog)

    // Update display during playback
    void setTime(double nowSec);
    void setNoteResult(int noteIndex, int kind);  // NoteResult::Kind
    void showSummary(int hits, int misses, int wrongNotes, int early, int late, int extras, double score);

    // Access to the song for Session
    const MidiSong& song() const { return song_; }
    bool hasSong() const { return !song_.tracks.empty(); }
    void setTuning(const Tuning& t);  // strings used by the "corda:casa" labels
    double songEndSec() const;  // end of the last note of the selected track (0 without a song)

    // Test accessors
    PianoRollWidget* pianoRoll() const { return roll_; }
    QPushButton* startButton() const { return start_; }
    QPushButton* pauseButton() const { return pause_; }
    QPushButton* backButton() const { return back_; }
    QPushButton* forwardButton() const { return fwd_; }
    QPushButton* restartButton() const { return restart_; }
    QComboBox* trackCombo() const { return trackCombo_; }

Q_SIGNALS:
    void startRequested();
    void stopRequested();
    void pauseRequested();    // Pausar (running) or Continuar (paused)
    void restartRequested();  // from the beginning
    void seekRequested(double deltaSec);  // jump forward (+) or back (-) in the song
    void configChanged();

private Q_SLOTS:
    void onImport();
    void onTrackChanged(int index);
    void onSpeedChanged(int value);

private:
    void updateTrackCombo();
    void updateSpeedLabel();
    void refreshLabels();

    bool running_ = false;
    bool paused_ = false;
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
    QCheckBox* tabCheck_ = nullptr;
    Tuning tuning_;
    QPushButton* back_ = nullptr;
    QPushButton* fwd_ = nullptr;
    QPushButton* pause_ = nullptr;
    QPushButton* restart_ = nullptr;
    void refreshButtons();

    // Display
    PianoRollWidget* roll_ = nullptr;
    QLabel* statsLabel_ = nullptr;
};
