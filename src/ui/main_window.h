#pragma once
#include <QMainWindow>
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
class QTabWidget;
class LevelMeter;
class TempoTrainingView;

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
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(BackendFactory factory, QWidget* parent = nullptr);  // PipeWire listing
    MainWindow(BackendFactory factory, DeviceLister lister, QWidget* parent = nullptr);
    ~MainWindow() override;

    TempoTrainingView* tempoView() const { return tempo_; }
    QTabWidget* tabs() const { return tabs_; }
    QLabel* errorBanner() const { return errorBanner_; }
    QLabel* warningNotice() const { return warning_; }
    LevelMeter* levelMeter() const { return level_; }
    QComboBox* inputCombo() const { return inCombo_; }
    QComboBox* outputCombo() const { return outCombo_; }
    QDoubleSpinBox* gainSpin() const { return gain_; }
    QDoubleSpinBox* compensationSpin() const { return comp_; }
    QLabel* engineBadge() const { return badge_; }
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
    void startSession();
    void stopSession();
    void recordSample();
    void poll();
    void setEngine(Engine e);
    void showError(const QString& text);
    void showWarning(const QString& text);
    void lockInputs(bool locked);
    void shutdown();

    BackendFactory factory_;
    DeviceLister lister_;

    std::unique_ptr<Session> session_;
    IAudioBackend* backend_ = nullptr;  // owned by session_
    std::string sessIn_, sessOut_;
    bool starting_ = false, running_ = false, devicesReady_ = false, listing_ = false;
    int countIn_ = 0;  // count-in (beats) of the current session
    std::vector<UiEvent> events_;

    std::thread listThread_, startThread_;
    std::mutex handoff_;  // guards the two results below (helper thread -> UI thread)
    DeviceLists listed_;
    bool startOk_ = false;
    std::string startErr_;

    QTimer pollTimer_, summaryTimer_, warningTimer_;

    QTabWidget* tabs_ = nullptr;
    TempoTrainingView* tempo_ = nullptr;
    QLabel* errorBanner_ = nullptr;
    QLabel* warning_ = nullptr;
    QLabel* badge_ = nullptr;
    LevelMeter* level_ = nullptr;
    QComboBox* inCombo_ = nullptr;
    QComboBox* outCombo_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QDoubleSpinBox* gain_ = nullptr;
    QDoubleSpinBox* comp_ = nullptr;
};
