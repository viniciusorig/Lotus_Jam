#pragma once
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "dsp/notes.h"
#include "dsp/tuner.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class TunerDial;
class StringStrip;

// "AFINADOR" tab (briefing §5.2). Pure view, like TempoTrainingView: MainWindow owns the
// Session, starts it in Mode::Tuner with instrument()/tuning()/a4() and feeds setReading()
// from UiEvent::Tuner.
//
// Layout: instrument bar (Instrumento, Afinação rebuilt from the instrument, Referência A4,
// Iniciar/Parar pill), the dial (QPainter: ticks −50…+50 cents, smoothed pointer, LED crown
// ♭/♯, lotus hub, "Nana Glow" when voiced && |cents| <= 5, giant note, Hz/cents readout or
// "Sem sinal") and the string strip (one indicator per string, the current one in pink).
//
// Pointer: exponential smoothing towards the last reading's cents (0 when unvoiced) with
// time constant kPointerTauSec, advanced by a 16 ms QTimer that runs only while the pointer
// moves or the glow pulses (90 % of a step in tau·ln 10 ≈ 69 ms, < 100 ms).
//
// The Session's Tuner has no A4/instrument/tuning setter: configChanged() makes MainWindow
// restart the tuner session with the new values.
class TunerWidget : public QWidget {
    Q_OBJECT
public:
    explicit TunerWidget(QWidget* parent = nullptr);
    ~TunerWidget() override;

    // Current configuration (the strings of tuning() are what the strip shows).
    const Instrument& instrument() const;
    Tuning tuning() const;
    double a4() const;

    // Applies one reading (UI thread). Readings whose stringIndex does not exist in the
    // current tuning are treated as unvoiced.
    void setReading(const TunerReading& r);
    // Back to "Sem sinal" (session started/stopped); the pointer glides back to 0.
    void clearReading();
    // Advances the pointer smoothing and the glow pulse by dtSec (the animation timer calls
    // it with the measured frame time; tests call it directly).
    void advance(double dtSec);
    // One smoothing step: shown + (target - shown)·(1 - e^(−dt/τ)).
    static double smoothStep(double shown, double target, double dtSec);
    static constexpr double kPointerTauSec = 0.030;
    static constexpr double kGlowCents = 5.0;  // |cents| <= 5: Nana Glow
    static constexpr double kFarCents = 15.0;  // |cents| > 15: LED crown in magenta/amber

    // Session pill, like the training view: "Iniciar" / "Parar" / "Iniciando…".
    void setRunning(bool running);
    bool isRunning() const { return running_; }
    void setStarting(bool starting);
    void setStartAllowed(bool allowed);

    // Test accessors.
    QString noteText() const { return note_; }  // "E2"; empty when unvoiced
    int highlightedString() const;    // 1-based (lowest string = 1); 0 = none (Ruling R5)
    double centsShown() const { return shown_; }  // smoothed pointer position
    bool glowActive() const;
    QString statusText() const { return status_; }  // "82,41 Hz  •  +0,4 cents" or "Sem sinal"
    QStringList stringLabels() const;
    int readingCount() const { return readings_; }
    bool animating() const { return anim_.isActive(); }

    QComboBox* instrumentCombo() const { return instrument_; }
    QComboBox* tuningCombo() const { return tuningCombo_; }
    QDoubleSpinBox* a4Spin() const { return a4_; }
    QPushButton* startButton() const { return start_; }

Q_SIGNALS:
    void startRequested();
    void stopRequested();
    void configChanged();  // instrument, tuning or A4 changed (by the user or programmatically)
    void readingApplied(const TunerReading& r);  // after setReading() (tests observe readings)

private:
    friend class TunerDial;
    friend class StringStrip;
    void rebuildTunings();
    void ensureAnimating();
    void refreshViews();
    void restyle(QWidget* w);
    void updateTexts();

    bool running_ = false, startAllowed_ = false;
    TunerReading reading_{false, -1, 0, 0, 0};
    double shown_ = 0.0;      // pointer (cents)
    double pulsePhase_ = 0.0;  // glow ring pulse, seconds
    int readings_ = 0;
    QString note_, status_{"Sem sinal"};
    QTimer anim_;
    QElapsedTimer frame_;

    QComboBox* instrument_ = nullptr;
    QComboBox* tuningCombo_ = nullptr;
    QDoubleSpinBox* a4_ = nullptr;
    QPushButton* start_ = nullptr;
    TunerDial* dial_ = nullptr;
    StringStrip* strip_ = nullptr;
};
