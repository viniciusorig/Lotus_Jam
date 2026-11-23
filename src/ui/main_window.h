#pragma once
#include <QMainWindow>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/session.h"
#include "audio/backend.h"
#include "ui/device_select.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSettings;
class QTabWidget;
class LevelMeter;
class MusicTrainingView;
class TempoTrainingView;
class TunerWidget;

using BackendFactory =
    std::function<std::unique_ptr<IAudioBackend>(const std::string& inNode, const std::string& outNode)>;
// Device listing; may block (PipeWire registry, ~2 s timeout): always run off the UI thread.
using DeviceLister = std::function<DeviceLists()>;
DeviceLists listPipeWireDevices();

// Lotus Jam main window (briefing §5.1): header (identity, engine badge, devices, ↻, level
// meter, gain, latency compensation), error banner / warning notice and the tabs
// AFINADOR / TREINO DE TEMPO / MÚSICAS & MIDI.
//
// Session ownership: the window owns one Session (and through it the backend made by the
// factory for the selected device pair; it is reused while the pair does not change). The UI
// thread never does DSP or blocking I/O:
//  - the device listing runs on a helper thread (listThread_);
//  - Session::start (which may wait for PipeWire to connect) runs on a helper thread
//    (startThread_); meanwhile the UI does not touch the Session (no poll, controls locked).
//    Session's calls stay strictly sequential: the helper's result is handed back through a
//    std::mutex and the thread is joined before the UI thread calls the Session again;
//  - a 16 ms QTimer drains Session::pollEvents and dispatches the UiEvents; a 100 ms QTimer
//    refreshes the score card from Session::summary() (cached by the Session).
// Closing the window or destroying it stops the session first (helpers joined, Session
// destroyed before any widget).
//
// One session at a time, owned by the tab that started it (sessMode_):
//  - AFINADOR: the tuner session starts by itself when the tab becomes current and stops when
//    the tab is left; its Parar/Iniciar pill pauses/resumes it (Parar holds until Iniciar or
//    the tab is entered again). Instrument, tuning, A4, gain or device changes restart it
//    (the DSP Tuner takes them at construction), debounced by 250 ms (A4 and gain spin boxes
//    without keyboard tracking) so a burst of changes restarts the streams once. A device scan
//    refills the combos silently and restarts a running tuner only if the selection changed. Device combos and gain stay enabled while
//    the tuner runs (changing them restarts it); a start failure disarms the tuner (banner,
//    pill back to "Iniciar", no retry loop).
//  - TREINO DE TEMPO: started/stopped by its own Iniciar/Parar; leaving the tab stops it.
//  reconcile() applies these rules; it never touches the Session while a start is in flight
//  (starting_): onStartFinished() calls it again, so a tab change during a start stops the
//  late session right away.
//
// Settings (app/settings.h): loaded in the constructor (the controls show them before the first
// scan has finished); the saved device nodes are preferred by every scan when present, else the
// current selection is kept, else device_select's default; a node chosen by the user becomes
// the preferred (saved) one; automatic choices are never saved (empty = automatic, so a
// fallback picked while the PCM2902 is unplugged never takes its place).
// Changing the input or output loads that pair's compensation into COMPENSAÇÃO; editing it stores
// it for the current pair. Everything is saved when a session starts, 1 s after a change
// (debounced) and when the window closes (then also written to disk).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // The application: PipeWire listing and QSettings("lotus-jam", "lotus-jam").
    explicit MainWindow(BackendFactory factory, QWidget* parent = nullptr);
    // Tests: injected listing and settings. settings == nullptr: nothing is loaded nor saved.
    // A given QSettings is not owned and must outlive the window.
    MainWindow(BackendFactory factory, DeviceLister lister, QSettings* settings = nullptr, QWidget* parent = nullptr);
    ~MainWindow() override;

    TempoTrainingView* tempoView() const { return tempo_; }
    TunerWidget* tunerView() const { return tuner_; }
    QTabWidget* tabs() const { return tabs_; }
    QLabel* errorBanner() const { return errorBanner_; }
    QLabel* warningNotice() const { return warning_; }
    LevelMeter* levelMeter() const { return level_; }
    QComboBox* inputCombo() const { return inCombo_; }
    QComboBox* outputCombo() const { return outCombo_; }
    QDoubleSpinBox* gainSpin() const { return gain_; }
    QDoubleSpinBox* compensationSpin() const { return comp_; }
    QLabel* engineBadge() const { return badge_; }
    QPushButton* refreshButton() const { return refresh_; }
    static QStringList engineBadgeTexts();
    // Where "Gravar amostra" writes and how long it records (tests use a temp dir / short take).
    void setSampleDir(const QString& dir) { sampleDir_ = dir; }
    void setSampleSeconds(double s) { sampleSeconds_ = s; }
    bool devicesReady() const { return devicesReady_; }

    // Applies a batch of UiEvents to the widgets (called by the poll timer; public for tests).
    void dispatchEvents(const std::vector<UiEvent>& events);
    // Re-lists the PipeWire devices in the background (the ↻ button).
    void refreshDevices();

protected:
    void closeEvent(QCloseEvent* e) override;

private Q_SLOTS:
    void onDevicesListed();
    void onStartFinished();

private:
    enum class Engine { Idle, Starting, Running, Failed };
    void buildUi();
    void startSession(Mode mode);
    void stopSession();
    void reconcile();
    void onTabChanged();
    void onTunerConfigChanged();
    void syncStartAllowed();
    void recordSample();
    void poll();
    void setEngine(Engine e);
    void showError(const QString& text);
    void showWarning(const QString& text);
    void lockInputs(bool locked);
    void shutdown();
    void applySettings();       // loads settings_ into the controls
    void persistSettings();     // controls -> settings_ (in memory)
    void flushSettings();       // persistSettings() + write to disk
    void scheduleSave();
    void loadPairCompensation();  // the current pair's compensation -> COMPENSAÇÃO

    BackendFactory factory_;
    DeviceLister lister_;
    std::unique_ptr<QSettings> ownSettings_;  // the application's (production constructor)
    QSettings* settings_ = nullptr;           // null: no persistence
    std::string prefIn_, prefOut_;  // saved or user-chosen nodes (kept while absent)
    bool applying_ = false;         // applySettings() / loadPairCompensation() running
    bool settingsWarned_ = false;
    QTimer saveTimer_;
    static constexpr int kSaveDebounceMs = 1000;

    std::unique_ptr<Session> session_;
    IAudioBackend* backend_ = nullptr;  // owned by session_
    std::string sessIn_, sessOut_;
    bool starting_ = false, running_ = false, devicesReady_ = false, listing_ = false;
    int countIn_ = 0;  // count-in (beats) of the current session
    Mode sessMode_ = Mode::Training;  // mode of the current (or starting) session
    bool tunerArmed_ = false;    // the tuner should run while its tab is current
    bool tunerRestart_ = false;  // tuner config changed: restart the running tuner session
    bool reconciling_ = false;
    unsigned tunerGen_ = 0, sessTunerGen_ = 0;  // config generation (stale readings dropped)
    std::vector<UiEvent> events_;

    std::thread listThread_, startThread_;
    std::mutex handoff_;  // guards the two results below (helper thread -> UI thread)
    DeviceLists listed_;
    bool startOk_ = false;
    std::string startErr_;

    QTimer pollTimer_, summaryTimer_, warningTimer_;
    QTimer restartTimer_;  // debounces tuner restarts after config changes
    static constexpr int kRestartDebounceMs = 250;

    QTabWidget* tabs_ = nullptr;
    TempoTrainingView* tempo_ = nullptr;
    TunerWidget* tuner_ = nullptr;
    MusicTrainingView* music_ = nullptr;
    QLabel* errorBanner_ = nullptr;
    QLabel* warning_ = nullptr;
    QLabel* badge_ = nullptr;
    LevelMeter* level_ = nullptr;
    QComboBox* inCombo_ = nullptr;
    QComboBox* outCombo_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QDoubleSpinBox* gain_ = nullptr;
    QDoubleSpinBox* comp_ = nullptr;
    QString sampleDir_;
    double sampleSeconds_ = 10.0;
    static constexpr double kRecGraceSec = 1.5;
    bool userStop_ = false;  // the current session's end was asked for by Parar
    bool recPending_ = false, recError_ = false;
    // Calibration: errors (ms, relative to the nearest beat slot) of every note heard in the
    // last training session, hits and extras alike, and the compensation/step they were taken with.
    std::vector<double> offsets_;
    double sessComp_ = 0.0, stepMs_ = 0.0;
    void calibrate();
    void addOffset(double ms);
    double recDeadline_ = 0;  // backend clock
    QString recPath_;
};
