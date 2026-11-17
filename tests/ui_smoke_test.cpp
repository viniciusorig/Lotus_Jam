#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/session.h"
#include "audio/click_scheduler.h"
#include "audio/fake_backend.h"
#include "ui/device_select.h"
#include "ui/main_window.h"
#include "ui/pulse_widget.h"
#include "ui/score_summary_card.h"
#include "ui/tempo_training_view.h"
#include "ui/theme.h"
#include "ui/timeline_widget.h"

// UI smoke tests. Always offscreen (main() forces QT_QPA_PLATFORM=offscreen, CTest sets it too),
// always on a FakeBackend and a fake device list: no real PipeWire stream, no visible window.
// Timing decisions use the FakeBackend's virtual clock or UI state; wall-clock time is only a
// generous timeout (TREINO_TEST_TIMEOUT_SEC overrides it).

namespace {

constexpr double kSr = 48000.0;

double timeoutSec() {
    const char* e = std::getenv("TREINO_TEST_TIMEOUT_SEC");
    return e ? std::atof(e) : 120.0;
}

// Runs the Qt event loop until `cond` holds; false on timeout.
bool pumpUntil(const std::function<bool()>& cond, double limitSec = timeoutSec()) {
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (double(t.elapsed()) > limitSec * 1000.0) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return true;
}

DeviceLists fakeDevices() {
    DeviceLists d;
    d.inputs = {{"alsa_input.pci-0000_00_1f.3.analog-stereo", "Áudio interno Microfone"},
                {"alsa_input.usb-BurrBrown_from_Texas_Instruments_USB_AUDIO_CODEC-00.analog-stereo",
                 "PCM2902 Audio Codec Analog Stereo"}};
    d.outputs = {{"alsa_output.pci-0000_00_1f.3.analog-stereo", "Áudio interno Estéreo analógico"},
                 {"alsa_output.pci-0000_01_00.1.hdmi-stereo", "TU102 HDMI Audio Controller Digital Stereo (HDMI)"}};
    return d;
}

double beatTime(const ClickScheduler& cs, double t0, int64_t n, double outLatency) {
    const double frame = double(std::llround(double(cs.firstBeatFrame()) + double(n) * cs.framesPerBeat()));
    return t0 + frame / kSr + outLatency;
}

double jitterMs(int k) {
    static const double j[] = {1.5, -2.0, 0.5, 3.0, -1.0, -3.0, 2.5, 0.0, -0.5, 2.0, -2.5, 1.0, -1.5, 3.0, 0.5, -3.0};
    return j[k % 16];
}

// Factory handing out FakeBackends; remembers the last one and the requested nodes.
struct FakeFactory {
    FakeBackend* last = nullptr;
    int calls = 0;
    std::string in, out;
    double speed = 4;
    int notes = 0;            // synthetic player: notes on the first scored beats
    int countInBeats = 4;     // must match the UI's count-in for the notes to land on the grid
    double bpm = 120;
    double offsetMs = 8;

    BackendFactory make() {
        return [this](const std::string& inNode, const std::string& outNode) -> std::unique_ptr<IAudioBackend> {
            ++calls;
            in = inNode;
            out = outNode;
            FakeBackend::Options o;
            o.speed = speed;
            o.backpressure = true;
            o.noiseSigma = 0.002;
            auto f = std::make_unique<FakeBackend>(o);
            last = f.get();
            ClickScheduler cs(kSr, bpm, countInBeats);
            const double t0 = f->nowSec();  // frozen until start()
            for (int k = 0; k < notes; ++k)
                f->scheduleNote(beatTime(cs, t0, countInBeats + k, o.outputLatencySec) + (offsetMs + jitterMs(k)) / 1000.0,
                                41.2, 0.3);
            return f;
        };
    }
};

bool waitRunning(MainWindow& w, bool running) {
    return pumpUntil([&] {
        return w.tempoView()->startButton()->text() == (running ? QString("Parar") : QString("Iniciar")) &&
               w.tempoView()->isRunning() == running;
    });
}

}  // namespace

TEST(UiSmoke, DeviceDefaults) {
    const DeviceLists d = fakeDevices();
    EXPECT_EQ(defaultInputIndex(d.inputs), 1);
    EXPECT_EQ(defaultOutputIndex(d.outputs), 1);

    std::vector<AudioDevice> ins = {{"a", "Microfone"}, {"alsa_input.usb-USB_PnP_Sound_Device-00.mono", "Placa USB"}};
    EXPECT_EQ(defaultInputIndex(ins), 1);
    std::vector<AudioDevice> none = {{"x", "Primeiro"}, {"y", "Segundo"}};
    EXPECT_EQ(defaultInputIndex(none), 0);
    EXPECT_EQ(defaultOutputIndex(none), 0);
    EXPECT_EQ(defaultInputIndex({}), -1);
    EXPECT_EQ(defaultOutputIndex({}), -1);
}

TEST(UiSmoke, IconTabsAndTheme) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.resize(1180, 760);
    w.show();  // offscreen platform: never visible
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));

    EXPECT_FALSE(w.windowIcon().isNull());
    EXPECT_FALSE(w.windowIcon().pixmap(32, 32).isNull());
    EXPECT_FALSE(QApplication::windowIcon().isNull());

    QTabWidget* t = w.tabs();
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->count(), 3);
    // Displayed titles (QTabBar treats '&' as a mnemonic marker: "&&" shows a literal '&').
    auto shown = [&](int i) { return t->tabText(i).replace("&&", "&"); };
    EXPECT_EQ(shown(0), QString("AFINADOR"));
    EXPECT_EQ(shown(1), QString("TREINO DE TEMPO"));
    EXPECT_EQ(shown(2), QString::fromUtf8("MÚSICAS & MIDI"));
    EXPECT_EQ(t->currentIndex(), 1);
    EXPECT_EQ(t->currentWidget(), w.tempoView());

    const QColor canvas("#09090B");
    EXPECT_EQ(theme::bg_canvas, canvas);
    EXPECT_EQ(w.palette().color(QPalette::Window), canvas);
    const QImage img = w.grab().toImage();
    ASSERT_FALSE(img.isNull());
    const QColor corner = img.pixelColor(img.width() - 4, img.height() - 4);
    EXPECT_EQ(corner.rgb(), canvas.rgb()) << corner.name().toStdString();

    // Default devices: PCM2902 in, HDMI out.
    EXPECT_EQ(w.inputCombo()->currentIndex(), 1);
    EXPECT_EQ(w.outputCombo()->currentIndex(), 1);
    // Numbers use a monospace stack: tabular digits (fontconfig does not flag Noto Sans Mono as
    // fixed pitch, so measure the glyphs instead of asking QFontInfo::fixedPitch()).
    const QFontMetricsF fm(theme::monoFont(14, true));
    for (const char* d : {"0", "1", "4", "7", "-", "+", "."})
        EXPECT_DOUBLE_EQ(fm.horizontalAdvance(QString(d)), fm.horizontalAdvance(QString("8"))) << d;
}

TEST(UiSmoke, TrainingSessionWithFakeBackend) {
    FakeFactory ff;
    ff.notes = 16;
    // Slow enough that Parar lands well before the 2nd unplayed slot is finalised as a Miss
    // (1.3 s of virtual time after the last note = 650 ms of wall time).
    ff.speed = 2;
    MainWindow w(ff.make(), fakeDevices);
    w.resize(1180, 760);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));

    TempoTrainingView* v = w.tempoView();
    v->bpmSpin()->setValue(120);
    EXPECT_EQ(v->countInBeats(), 4);  // default: 1 compasso
    EXPECT_EQ(v->subdivision(), 1);
    EXPECT_EQ(v->startButton()->objectName(), QString("PrimaryAction"));
    EXPECT_EQ(v->startButton()->text(), QString("Iniciar"));

    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    EXPECT_TRUE(v->startButton()->property("running").toBool());
    EXPECT_EQ(ff.calls, 1);
    EXPECT_EQ(ff.in, fakeDevices().inputs[1].name);
    EXPECT_EQ(ff.out, fakeDevices().outputs[1].name);

    ASSERT_TRUE(pumpUntil([&] { return v->timeline()->noteCount() >= 8; }));
    w.grab().save(UI_SMOKE_RUNNING_PNG);  // mid-session look (pulse, live timeline)
    ASSERT_TRUE(pumpUntil([&] { return v->timeline()->noteCount() >= 16; })) << v->timeline()->noteCount();
    v->startButton()->click();  // Parar
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_FALSE(v->startButton()->property("running").toBool());

    EXPECT_EQ(v->timeline()->noteCount(), 16);
    EXPECT_GT(v->summaryCard()->shown().total, 90.0);
    // The last hit is only final (and in the summary) once its window has passed: 15 or 16.
    EXPECT_GE(v->summaryCard()->shown().hits, 15);
    EXPECT_LE(v->summaryCard()->shown().slots - v->summaryCard()->shown().hits, 1);
    bool ok = false;
    const double shownTotal = v->summaryCard()->totalText().toDouble(&ok);
    EXPECT_TRUE(ok) << v->summaryCard()->totalText().toStdString();
    EXPECT_GT(shownTotal, 90.0);
    // The pulse flashed through the count-in and the scored beats.
    EXPECT_GE(v->pulse()->flashCount(), 4);
    EXPECT_EQ(v->pulse()->firstLabel(), QString("contagem 1"));
    EXPECT_TRUE(w.errorBanner()->isHidden());

    const QPixmap shot = w.grab();
    EXPECT_TRUE(shot.save(UI_SMOKE_PNG));
}

TEST(UiSmoke, ErrorShownOnBackendFailure) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TempoTrainingView* v = w.tempoView();
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    ASSERT_NE(ff.last, nullptr);
    FakeBackend* f = ff.last;
    const double t0 = f->nowSec();
    ASSERT_TRUE(pumpUntil([&] { return f->nowSec() > t0 + 1.0; }));
    f->simulateFailure();

    ASSERT_TRUE(pumpUntil([&] { return !w.errorBanner()->isHidden(); }));
    EXPECT_EQ(w.errorBanner()->text(), QString::fromUtf8("O dispositivo de áudio foi desconectado."));
    ASSERT_TRUE(waitRunning(w, false));

    // Still responsive: timers fire, and a new session can start (banner cleared).
    bool fired = false;
    QTimer::singleShot(0, &w, [&] { fired = true; });
    ASSERT_TRUE(pumpUntil([&] { return fired; }, 10.0));
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    EXPECT_TRUE(w.errorBanner()->isHidden());
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
}

TEST(UiSmoke, ErrorShownWhenBackendCannotBeCreated) {
    MainWindow w([](const std::string&, const std::string&) { return std::unique_ptr<IAudioBackend>(); }, fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    w.tempoView()->startButton()->click();
    ASSERT_TRUE(pumpUntil([&] { return !w.errorBanner()->isHidden(); }));
    EXPECT_FALSE(w.errorBanner()->text().isEmpty());
    EXPECT_EQ(w.tempoView()->startButton()->text(), QString("Iniciar"));
}

TEST(UiSmoke, CountInZeroSupported) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TempoTrainingView* v = w.tempoView();
    QComboBox* c = v->countInCombo();
    ASSERT_EQ(c->count(), 3);
    c->setCurrentIndex(0);
    EXPECT_EQ(v->countInBeats(), 0);
    c->setCurrentIndex(2);
    EXPECT_EQ(v->countInBeats(), 8);
    c->setCurrentIndex(0);

    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    ASSERT_TRUE(pumpUntil([&] { return v->pulse()->flashCount() >= 2; }));
    EXPECT_EQ(v->pulse()->firstLabel(), QString("1"));
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_TRUE(w.errorBanner()->isHidden());
}

TEST(UiSmoke, WarningShownNonFatal) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TempoTrainingView* v = w.tempoView();
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));

    UiEvent warn;
    warn.type = UiEvent::Warning;
    warn.text = "Entrada sobrecarregada: parte do áudio foi descartada.";
    w.dispatchEvents({warn});
    EXPECT_FALSE(w.warningNotice()->isHidden());
    EXPECT_EQ(w.warningNotice()->text(), QString::fromStdString(warn.text));
    EXPECT_TRUE(w.errorBanner()->isHidden());
    // The session goes on.
    QCoreApplication::processEvents();
    EXPECT_TRUE(v->isRunning());
    EXPECT_EQ(v->startButton()->text(), QString("Parar"));
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
}

TEST(UiSmoke, UiThreadDoesNoHeavyWork) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.resize(1180, 760);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));

    std::vector<UiEvent> burst;
    burst.reserve(5000);
    int notes = 0, misses = 0;
    for (int i = 0; i < 5000; ++i) {
        UiEvent e;
        switch (i % 5) {
        case 0:
        case 1:
            e.type = UiEvent::Note;
            e.n = i;
            e.a = double((i * 37) % 200) - 100.0;
            e.b = 80;
            ++notes;
            break;
        case 2:
            e.type = UiEvent::Miss;
            e.n = i;
            ++misses;
            break;
        case 3:
            e.type = UiEvent::Extra;
            e.n = i;
            e.a = 250;
            break;
        default:
            e.type = UiEvent::Level;
            e.a = 0.01 * double(i % 100);
            break;
        }
        burst.push_back(std::move(e));
    }
    QElapsedTimer t;
    t.start();
    w.dispatchEvents(burst);
    const qint64 dispatchMs = t.elapsed();
    EXPECT_LT(dispatchMs, 250) << "dispatch of 5000 events took " << dispatchMs << " ms";
    EXPECT_EQ(w.tempoView()->timeline()->noteCount(), notes);
    EXPECT_EQ(w.tempoView()->timeline()->missCount(), misses);
    EXPECT_EQ(w.tempoView()->timeline()->pointCount(), 32);  // only the last 32 are kept

    // One repaint of the whole window afterwards stays cheap too.
    t.restart();
    const QPixmap shot = w.grab();
    EXPECT_FALSE(shot.isNull());
    EXPECT_LT(t.elapsed(), 1000);
}

TEST(UiSmoke, CloseDuringSessionIsClean) {
    FakeFactory ff;
    auto w = std::make_unique<MainWindow>(ff.make(), fakeDevices);
    w->show();
    ASSERT_TRUE(pumpUntil([&] { return w->devicesReady(); }));
    w->tempoView()->startButton()->click();
    ASSERT_TRUE(waitRunning(*w, true));
    ASSERT_TRUE(pumpUntil([&] { return w->tempoView()->pulse()->flashCount() >= 1; }));
    w->close();
    w.reset();  // stops the session, joins its threads, then destroys the widgets
    QCoreApplication::processEvents();
    SUCCEED();
}

#if defined(__SANITIZE_THREAD__)
// Qt is not built with ThreadSanitizer: its internal thread pools (image/raster work) synchronise
// through futexes TSAN cannot see, so allocations inside Qt look racy, and Qt's pooled threads
// are not joined before exit. Ignore the interceptors called from the Qt libraries and those
// thread leaks only; races in our own code (UI, Session, backend) are still reported.
extern "C" const char* __tsan_default_suppressions() {
    return "called_from_lib:libQt6Core.so.6\n"
           "called_from_lib:libQt6Gui.so.6\n"
           "called_from_lib:libQt6Widgets.so.6\n"
           "thread:QThread::start\n";
}
#endif

int main(int argc, char** argv) {
    // Never open a visible window on the developer's desktop.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    theme::apply(app);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
