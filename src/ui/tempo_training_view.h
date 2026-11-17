#pragma once
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class PulseWidget;
class TimelineWidget;
class ScoreSummaryCard;

// "TREINO DE TEMPO" tab (briefing §5.3). Pure view: it holds the session controls and the
// feedback widgets; MainWindow owns the Session, reacts to startRequested/stopRequested/
// recordRequested and feeds the widgets from UiEvents.
class TempoTrainingView : public QWidget {
    Q_OBJECT
public:
    explicit TempoTrainingView(QWidget* parent = nullptr);

    int bpm() const;
    int countInBeats() const;  // 0, 4 or 8 (0/1/2 bars of 4/4)
    int subdivision() const;   // 1, 2 or 4

    // Running state: button text "Iniciar"/"Parar", property `running`, controls locked.
    void setRunning(bool running);
    bool isRunning() const { return running_; }
    // Start in progress (backend connecting): the button is disabled and shows "Iniciando…".
    void setStarting(bool starting);
    // Shows the recording indicator for `seconds`, then "Amostra salva: <file>".
    void showRecording(const QString& path, double seconds);

    QPushButton* startButton() const { return start_; }
    QPushButton* recordButton() const { return record_; }
    QSpinBox* bpmSpin() const { return bpm_; }
    QComboBox* countInCombo() const { return countIn_; }
    QComboBox* subdivisionCombo() const { return subdivision_; }
    PulseWidget* pulse() const { return pulse_; }
    TimelineWidget* timeline() const { return timeline_; }
    ScoreSummaryCard* summaryCard() const { return card_; }

Q_SIGNALS:
    void startRequested();
    void stopRequested();
    void recordRequested();

private:
    void restyle(QWidget* w);

    bool running_ = false;
    QSpinBox* bpm_ = nullptr;
    QComboBox* countIn_ = nullptr;
    QComboBox* subdivision_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* record_ = nullptr;
    QLabel* recLabel_ = nullptr;
    QTimer recTimer_;
    QString recPath_;
    PulseWidget* pulse_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    ScoreSummaryCard* card_ = nullptr;
};
