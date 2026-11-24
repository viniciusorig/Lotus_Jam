#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QIcon>
#include <QImage>
#include <QAbstractItemView>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QTemporaryDir>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/session.h"
#include "app/settings.h"
#include "audio/click_scheduler.h"
#include "audio/fake_backend.h"
#include "ui/device_select.h"
#include "ui/level_meter.h"
#include "ui/music_training_view.h"
#include "ui/piano_roll_widget.h"
#include "ui/main_window.h"
#include "ui/pulse_widget.h"
#include "ui/score_summary_card.h"
#include "ui/tempo_training_view.h"
#include "ui/theme.h"
#include "ui/timeline_widget.h"
#include "ui/tuner_widget.h"
#include "dsp/notes.h"
#include <QDoubleSpinBox>

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

// A FakeBackend that counts its start() calls (each Session start/restart) and can fail them
// (as a PipeWire stream that cannot connect).
struct TestBackend : FakeBackend {
    TestBackend(const Options& o, bool fail, std::atomic<int>* starts) : FakeBackend(o), fail_(fail), starts_(starts) {}
    bool start(std::string& err) override {
        starts_->fetch_add(1);
        if (fail_) {
            err = "Não foi possível iniciar o áudio (teste).";
            return false;
        }
        return FakeBackend::start(err);
    }
    bool fail_;
    std::atomic<int>* starts_;
};

// Factory handing out FakeBackends; remembers the last one and the requested nodes.
struct FakeFactory {
    FakeBackend* last = nullptr;
    int calls = 0;
    double pauseAt = 0;
    std::string in, out;
    double speed = 4;
    int notes = 0;            // synthetic player: notes on the first scored beats
    int countInBeats = 4;     // must match the UI's count-in for the notes to land on the grid
    double bpm = 120;
    double offsetMs = 8;
    // If > 0: the fake clock freezes this long after the audible time of the last scored note
    // (FakeBackend::Options::pauseAtSec), so what is finalised does not depend on wall time.
    double pauseAfterLastSec = 0;
    // Sustained tones {startOffset, endOffset, hz, amp} (offsets from the stream origin) and an
    // optional freeze point (offset) for the tuner tests.
    struct Tone {
        double start, end, hz, amp;
    };
    std::vector<Tone> tones;
    double pauseAtOffset = 0;
    bool failStart = false;  // the backend's start() fails (helper-thread failure path)
    std::atomic<int> starts{0};  // backend start() calls (one per Session start)

    BackendFactory make() {
        return [this](const std::string& inNode, const std::string& outNode) -> std::unique_ptr<IAudioBackend> {
            ++calls;
            in = inNode;
            out = outNode;
            FakeBackend::Options o;
            o.speed = speed;
            o.backpressure = true;
            o.noiseSigma = 0.002;
            ClickScheduler cs(kSr, bpm, countInBeats);
            const double t0 = o.startSec;  // nowSec() is frozen at startSec until start()
            if (pauseAfterLastSec > 0 && notes > 0)
                o.pauseAtSec = beatTime(cs, t0, countInBeats + notes - 1, o.outputLatencySec) + pauseAfterLastSec;
            if (pauseAtOffset > 0) o.pauseAtSec = t0 + pauseAtOffset;
            pauseAt = o.pauseAtSec;
            auto f = std::make_unique<TestBackend>(o, failStart, &starts);
            last = f.get();
            for (const Tone& t : tones) f->scheduleTone(t0 + t.start, t0 + t.end, t.hz, t.amp);
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

bool waitTuner(MainWindow& w, bool running, double limit = timeoutSec()) {
    TunerWidget* t = w.tunerView();
    return pumpUntil(
        [&] {
            return t->startButton()->text() == (running ? QString("Parar") : QString("Iniciar")) &&
                   t->isRunning() == running;
        },
        limit);
}

void selectInstrument(TunerWidget* t, const char* id) {
    const int i = t->instrumentCombo()->findData(QString(id));
    ASSERT_GE(i, 0) << id;
    t->instrumentCombo()->setCurrentIndex(i);
}

double hzOf(int midi, double cents) { return midiToHz(midi) * std::pow(2.0, cents / 1200.0); }

// Counts the "lit" pixels (text, arrows) of a widget grab.
int litPixels(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) n += qGray(img.pixel(x, y)) > 70;
    return n;
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
    // The fake clock freezes 0.6 s after the last note: its slot (window 0.25 s + 0.1 s) is
    // final, the next, unplayed slot (0.5 s later + 0.35 s) is not. Parar can come at any
    // wall time after that: the result is deterministic.
    ff.pauseAfterLastSec = 0.6;
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
    ASSERT_TRUE(pumpUntil([&] { return ff.last->nowSec() >= ff.pauseAt; }));
    // Let the scoring thread finalise every slot of the frozen clock (summary card updated).
    ASSERT_TRUE(pumpUntil([&] { return v->summaryCard()->shown().slots >= 16; }));
    v->startButton()->click();  // Parar
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_FALSE(v->startButton()->property("running").toBool());

    EXPECT_EQ(v->timeline()->noteCount(), 16);
    EXPECT_GT(v->summaryCard()->shown().total, 90.0);
    EXPECT_EQ(v->summaryCard()->shown().hits, 16);
    EXPECT_EQ(v->summaryCard()->shown().slots, 16);
    EXPECT_EQ(v->timeline()->missCount(), 0);
    bool ok = false;
    const double shownTotal = QLocale().toDouble(v->summaryCard()->totalText(), &ok);
    EXPECT_TRUE(ok) << v->summaryCard()->totalText().toStdString();
    EXPECT_GT(shownTotal, 90.0);
    // The pulse flashed through the count-in and the scored beats.
    EXPECT_GE(v->pulse()->flashCount(), 4);
    EXPECT_EQ(v->pulse()->firstLabel(), QString("contagem 1"));
    EXPECT_TRUE(w.errorBanner()->isHidden());

    const QPixmap shot = w.grab();
    EXPECT_TRUE(shot.save(UI_SMOKE_PNG));
}

// The delay of the audio path (HDMI) can be larger than the scoring window: the notes then
// show up as extras and the plain mean error of the hits says nothing. Calibrar uses every
// note (circular mean over one beat step) and still finds the offset.
TEST(UiSmoke, CalibrateFindsDelayBeyondTheScoringWindow) {
    FakeFactory ff;
    ff.notes = 16;
    ff.offsetMs = 260;  // 120 BPM: step 500 ms, window 200 ms: every note is an extra (-240 ms)
    ff.pauseAfterLastSec = 0.6;
    MainWindow w(ff.make(), fakeDevices);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TempoTrainingView* v = w.tempoView();
    v->bpmSpin()->setValue(120);
    EXPECT_FALSE(v->calibrateButton()->isEnabled());
    const double before = w.compensationSpin()->value();

    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    ASSERT_TRUE(pumpUntil([&] { return v->calibrateButton()->isEnabled(); }));
    ASSERT_TRUE(pumpUntil([&] { return ff.last->nowSec() >= ff.pauseAt; }));
    v->startButton()->click();  // Parar
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(v->summaryCard()->shown().hits, 0);

    v->calibrateButton()->click();
    EXPECT_NEAR(w.compensationSpin()->value(), before - 240.0, 4.0);
    EXPECT_FALSE(v->calibrationLabel()->isHidden());
    EXPECT_TRUE(v->calibrationLabel()->text().contains("Compensação ajustada"));
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

// The engine badge must show every state text in full (QSS letter-spacing included), also
// after state changes (text + repolish), which is where "MOTOR PARADO" was clipped.
TEST(UiSmoke, EngineBadgeFitsEveryState) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.resize(1180, 760);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    QLabel* b = w.engineBadge();
    const QStringList texts = MainWindow::engineBadgeTexts();
    ASSERT_GE(texts.size(), 4);
    auto settle = [] {
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    };
    auto check = [&](const char* when) {
        settle();
        EXPECT_TRUE(texts.contains(b->text())) << b->text().toStdString();
        const QFontMetrics fm(b->font());  // advance includes the font's letter spacing
        const int frame = b->width() - b->contentsRect().width();
        for (const QString& t : texts) {
            const int needed = fm.horizontalAdvance(t) + frame;
            EXPECT_GE(b->width(), needed) << when << ": " << t.toStdString();
            EXPECT_GE(b->contentsRect().width(), fm.horizontalAdvance(t)) << when << ": " << t.toStdString();
        }
        EXPECT_GT(b->font().letterSpacing(), 0.0) << when;
    };
    check("idle");
    EXPECT_EQ(b->text(), QString("MOTOR PARADO"));
    w.tempoView()->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    check("running");
    w.tempoView()->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(b->text(), QString("MOTOR PARADO"));
    check("after stop");
    const QImage img = b->grab().toImage();
    EXPECT_FALSE(img.isNull());
}

// While the first device scan runs, Iniciar is disabled and no misleading error is shown.
TEST(UiSmoke, StartWaitsForDeviceScan) {
    std::atomic<bool> release{false};
    FakeFactory ff;
    MainWindow w(ff.make(), [&] {
        while (!release.load()) QThread::msleep(1);
        return fakeDevices();
    });
    w.show();
    QCoreApplication::processEvents();
    TempoTrainingView* v = w.tempoView();
    EXPECT_FALSE(w.devicesReady());
    EXPECT_FALSE(v->startButton()->isEnabled());
    EXPECT_EQ(w.inputCombo()->placeholderText(), QString::fromUtf8("Procurando dispositivos…"));
    // The placeholder is actually DRAWN (not just set): more lit pixels than the same combo
    // with no placeholder.
    {
        QComboBox* c = w.inputCombo();
        EXPECT_EQ(c->currentIndex(), -1);
        const int withText = litPixels(c->grab().toImage());
        c->setPlaceholderText(QString());
        const int empty = litPixels(c->grab().toImage());
        c->setPlaceholderText(QString::fromUtf8("Procurando dispositivos…"));
        EXPECT_GT(withText, empty + 40) << withText << " vs " << empty;
    }
    v->startButton()->click();  // disabled: nothing happens
    QCoreApplication::processEvents();
    EXPECT_TRUE(w.errorBanner()->isHidden());
    EXPECT_EQ(ff.calls, 0);
    release = true;
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    EXPECT_TRUE(v->startButton()->isEnabled());
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
}

// ↻ stays disabled during a session, even when a scan result arrives meanwhile.
TEST(UiSmoke, RefreshLockedWhileRunning) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    EXPECT_TRUE(w.refreshButton()->isEnabled());
    w.tempoView()->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    EXPECT_FALSE(w.refreshButton()->isEnabled());
    EXPECT_FALSE(w.inputCombo()->isEnabled());
    w.refreshDevices();  // ignored while running
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    EXPECT_FALSE(w.refreshButton()->isEnabled());
    EXPECT_FALSE(w.inputCombo()->isEnabled());
    w.tempoView()->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_TRUE(w.refreshButton()->isEnabled());
    EXPECT_TRUE(w.inputCombo()->isEnabled());
}

// Only a device failure turns the badge into FALHA; a WAV write error does not.
TEST(UiSmoke, BadgeFailedOnlyOnDeviceFailure) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TempoTrainingView* v = w.tempoView();
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    EXPECT_EQ(w.engineBadge()->text(), QString("MOTOR ATIVO"));
    UiEvent wav;
    wav.type = UiEvent::Error;
    wav.text = "Não foi possível gravar a amostra.";
    w.dispatchEvents({wav});
    EXPECT_FALSE(w.errorBanner()->isHidden());
    EXPECT_EQ(w.engineBadge()->text(), QString("MOTOR ATIVO"));
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(w.engineBadge()->text(), QString("MOTOR PARADO"));
    w.dispatchEvents({wav});  // late writer error after Stopped
    EXPECT_EQ(w.engineBadge()->text(), QString("MOTOR PARADO"));

    // A real device failure does.
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    FakeBackend* f = ff.last;
    const double t0 = f->nowSec();
    ASSERT_TRUE(pumpUntil([&] { return f->nowSec() > t0 + 0.5; }));
    f->simulateFailure();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(w.engineBadge()->text(), QString("FALHA"));
}

// The recording indicator reports what actually happened.
TEST(UiSmoke, RecordingOutcomeShown) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.setSampleDir(dir.path());
    w.setSampleSeconds(0.5);
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TempoTrainingView* v = w.tempoView();
    QLabel* rec = v->recordingLabel();

    // 1. Completed while running, no error: saved, and the file exists.
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    ASSERT_TRUE(v->recordButton()->isEnabled());
    v->recordButton()->click();
    EXPECT_TRUE(rec->text().startsWith(QString::fromUtf8("● Gravando")));
    ASSERT_TRUE(pumpUntil([&] { return rec->text().startsWith("Amostra salva"); })) << rec->text().toStdString();
    const QStringList wavs = QDir(dir.path()).entryList({"*.wav"}, QDir::Files);
    ASSERT_EQ(wavs.size(), 1);
    EXPECT_TRUE(rec->text().contains(wavs.front()));
    EXPECT_GT(QFileInfo(dir.filePath(wavs.front())).size(), 44);

    // 2. Stopped before the end: interrupted.
    w.setSampleSeconds(30);
    ASSERT_TRUE(pumpUntil([&] { return v->recordButton()->isEnabled(); }));
    v->recordButton()->click();
    v->startButton()->click();  // Parar
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(rec->text(), QString::fromUtf8("Gravação interrompida"));

    // 3. Next Start clears it; a write error is reported as a failure, never as saved.
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    EXPECT_TRUE(rec->isHidden() || rec->text().isEmpty());
    w.setSampleSeconds(0.5);
    v->recordButton()->click();
    UiEvent err;
    err.type = UiEvent::Error;
    err.text = "Não foi possível gravar a amostra.";
    w.dispatchEvents({err});
    for (int i = 0; i < 50; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    ASSERT_TRUE(pumpUntil([&] { return !rec->text().startsWith(QString::fromUtf8("● Gravando")); }));
    EXPECT_EQ(rec->text(), QString::fromUtf8("Falha ao gravar a amostra"));
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(rec->text(), QString::fromUtf8("Falha ao gravar a amostra"));
}

// Long device descriptions: full text (and node name) in the item tooltip, wide popup.
TEST(UiSmoke, DeviceCombosReadable) {
    DeviceLists d;
    d.inputs = {{"alsa_input.usb-BurrBrown_from_Texas_Instruments_USB_AUDIO_CODEC-00.analog-mono",
                 "PCM2902 Audio Codec Monofônico analógico"}};
    d.outputs = {{"alsa_output.pci-0000_01_00.1.hdmi-stereo-extra1",
                  "TU102 High Definition Audio Controller Digital Stereo (HDMI 2)"}};
    FakeFactory ff;
    MainWindow w(ff.make(), [d] { return d; });
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    for (QComboBox* c : {w.inputCombo(), w.outputCombo()}) {
        const AudioDevice& dev = c == w.inputCombo() ? d.inputs[0] : d.outputs[0];
        const QString tip = c->itemData(0, Qt::ToolTipRole).toString();
        EXPECT_TRUE(tip.contains(QString::fromStdString(dev.description))) << tip.toStdString();
        EXPECT_TRUE(tip.contains(QString::fromStdString(dev.name))) << tip.toStdString();
        EXPECT_EQ(c->toolTip(), tip);
        const int textW = c->view()->fontMetrics().horizontalAdvance(QString::fromStdString(dev.description));
        EXPECT_GE(c->view()->minimumWidth(), textW) << dev.description;
    }
}

// Decimal separators follow QLocale (like the spin boxes).
TEST(UiSmoke, NumbersFollowLocale) {
    const QLocale saved;
    QLocale::setDefault(QLocale(QLocale::Portuguese, QLocale::Brazil));
    {
        ScoreSummaryCard card;
        card.setSummary(GridScorer::Summary{97.5, 96.25, 16, 16, 1, 8.5, 2.25});
        EXPECT_EQ(card.totalText(), QString("97,5"));
        LevelMeter m;
        m.setPeak(0.25);  // -12.04 dBFS
        EXPECT_EQ(m.readoutText(), QString("-12,0 dB"));
    }
    QLocale::setDefault(QLocale::c());
    {
        ScoreSummaryCard card;
        card.setSummary(GridScorer::Summary{97.5, 96.25, 16, 16, 1, 8.5, 2.25});
        EXPECT_EQ(card.totalText(), QString("97.5"));
    }
    QLocale::setDefault(saved);
}


// "Nenhum dispositivo encontrado" is drawn in the combo when the scan finds nothing.
TEST(UiSmoke, NoDevicePlaceholderDrawn) {
    FakeFactory ff;
    MainWindow w(ff.make(), [] { return DeviceLists{}; });
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    QComboBox* c = w.inputCombo();
    EXPECT_EQ(c->currentIndex(), -1);
    EXPECT_EQ(c->placeholderText(), QString::fromUtf8("Nenhum dispositivo encontrado"));
    const int withText = litPixels(c->grab().toImage());
    c->setPlaceholderText(QString());
    const int empty = litPixels(c->grab().toImage());
    EXPECT_GT(withText, empty + 40) << withText << " vs " << empty;
}

// ---------------------------------------------------------------- tuner (Task 11)

TEST(UiSmoke, TunerShowsStringAndCents) {
    const QLocale saved;
    QLocale::setDefault(QLocale(QLocale::Portuguese, QLocale::Brazil));
    {
        FakeFactory ff;
        ff.tones = {{0.1, 1e6, hzOf(40, 15.0), 0.3}};  // E2 + 15 cents, sustained
        MainWindow w(ff.make(), fakeDevices);
        w.resize(1180, 780);
        w.show();
        ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
        TunerWidget* t = w.tunerView();
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(w.tabs()->widget(0), t);  // first tab: AFINADOR
        selectInstrument(t, "violao");
        t->tuningCombo()->setCurrentIndex(0);  // Padrão
        EXPECT_EQ(ff.calls, 0);                // no session while the tab is not shown
        w.tabs()->setCurrentWidget(t);
        ASSERT_TRUE(waitTuner(w, true));
        EXPECT_EQ(ff.calls, 1);
        EXPECT_EQ(ff.in, fakeDevices().inputs[1].name);  // the global input/output selection
        EXPECT_EQ(ff.out, fakeDevices().outputs[1].name);

        ASSERT_TRUE(pumpUntil([&] { return t->noteText() == "E2"; })) << t->statusText().toStdString();
        EXPECT_EQ(t->highlightedString(), 1);
        ASSERT_TRUE(pumpUntil([&] { return t->centsShown() >= 13.0 && t->centsShown() <= 17.0; }))
            << t->centsShown();
        EXPECT_FALSE(t->glowActive());
        // pt-BR readout: "83,12 Hz  •  +15,0 cents".
        const QString s = t->statusText();
        EXPECT_TRUE(QRegularExpression(QString::fromUtf8("^\\d+,\\d{2} Hz  •  \\+1[3-7],\\d cents$"))
                        .match(s)
                        .hasMatch())
            << s.toStdString();
        t->startButton()->click();  // Parar
        ASSERT_TRUE(waitTuner(w, false));
    }
    QLocale::setDefault(saved);
}

TEST(UiSmoke, TunerNoSignalMessage) {
    // 1. Silence (noise floor only).
    {
        FakeFactory ff;
        MainWindow w(ff.make(), fakeDevices);
        w.resize(1180, 780);
        w.show();
        ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
        TunerWidget* t = w.tunerView();
        selectInstrument(t, "violao");
        w.tabs()->setCurrentWidget(t);
        ASSERT_TRUE(waitTuner(w, true));
        ASSERT_TRUE(pumpUntil([&] { return t->readingCount() >= 10; }));
        EXPECT_EQ(t->statusText().compare("Sem sinal", Qt::CaseInsensitive), 0) << t->statusText().toStdString();
        EXPECT_EQ(t->statusText(), QString("Sem sinal"));
        EXPECT_FALSE(t->glowActive());
        EXPECT_EQ(t->highlightedString(), 0);
        EXPECT_TRUE(t->noteText().isEmpty());
        EXPECT_DOUBLE_EQ(t->centsShown(), 0.0);
        EXPECT_TRUE(w.grab().save(UI_TUNER_NOSIGNAL_PNG));
    }
    // 2. baixo4: an in-tune E1, then quiet 60 Hz hum (below the level gate) and 440 Hz (out of range).
    {
        FakeFactory ff;
        ff.tones = {{0.1, 1.5, hzOf(28, 0.0), 0.3}, {2.0, 4.0, 60.0, 0.004}, {4.5, 6.5, 440.0, 0.3}};
        ff.pauseAtOffset = 6.6;
        MainWindow w(ff.make(), fakeDevices);
        w.show();
        ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
        TunerWidget* t = w.tunerView();
        selectInstrument(t, "baixo4");
        std::vector<TunerReading> seen;
        int trailingUnvoiced = 0;
        bool sawE1 = false;  // observed per reading: independent of when the UI polls
        QObject::connect(t, &TunerWidget::readingApplied, t, [&](const TunerReading& r) {
            seen.push_back(r);
            trailingUnvoiced = r.voiced ? 0 : trailingUnvoiced + 1;
            if (r.voiced && r.stringIndex == 0 && t->noteText() == "E1") sawE1 = true;
        });
        w.tabs()->setCurrentWidget(t);
        ASSERT_TRUE(waitTuner(w, true));
        ASSERT_TRUE(pumpUntil([&] { return sawE1; }));
        // 60 Hz and 440 Hz (and the gaps) are 5 s of input: ~100 unvoiced readings follow E1.
        ASSERT_TRUE(pumpUntil([&] { return trailingUnvoiced >= 80; })) << trailingUnvoiced;
        for (const TunerReading& r : seen)
            if (r.voiced) {
                EXPECT_NEAR(r.hz, hzOf(28, 0.0), 1.0) << "voiced reading at " << r.hz << " Hz";
            }
        EXPECT_EQ(t->statusText().compare("Sem sinal", Qt::CaseInsensitive), 0) << t->statusText().toStdString();
        EXPECT_FALSE(t->glowActive());
        EXPECT_EQ(t->highlightedString(), 0);
    }
}

TEST(UiSmoke, TunerGlowWhenInTune) {
    FakeFactory ff;
    // The virtual clock freezes inside the in-tune tone: the last reading is in tune whatever
    // the wall-clock pace of the UI.
    ff.tones = {{0.1, 1e6, 82.41, 0.3}};
    ff.pauseAtOffset = 1.5;
    MainWindow w(ff.make(), fakeDevices);
    w.resize(1180, 780);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TunerWidget* t = w.tunerView();
    selectInstrument(t, "violao");
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    ASSERT_TRUE(pumpUntil([&] { return t->glowActive(); })) << t->statusText().toStdString();
    EXPECT_EQ(t->noteText(), QString("E2"));
    EXPECT_EQ(t->highlightedString(), 1);
    EXPECT_TRUE(t->animating());  // the glow ring pulses while in tune
    ASSERT_TRUE(pumpUntil([&] { return std::fabs(t->centsShown()) < 2.0; }));
    EXPECT_TRUE(w.grab().save(UI_TUNER_INTUNE_PNG));
    t->startButton()->click();
    ASSERT_TRUE(waitTuner(w, false));
    EXPECT_FALSE(t->glowActive());

    // +20 cents (sustained): no glow.
    FakeFactory ff2;
    ff2.tones = {{0.1, 1e6, hzOf(40, 20.0), 0.3}};
    MainWindow w2(ff2.make(), fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w2.devicesReady(); }));
    TunerWidget* t2 = w2.tunerView();
    selectInstrument(t2, "violao");
    bool glowSeen = false;
    QObject::connect(t2, &TunerWidget::readingApplied, t2, [&] { glowSeen = glowSeen || t2->glowActive(); });
    w2.tabs()->setCurrentWidget(t2);
    ASSERT_TRUE(waitTuner(w2, true));
    ASSERT_TRUE(pumpUntil([&] { return t2->centsShown() > 18.0; })) << t2->centsShown();
    EXPECT_FALSE(t2->glowActive());
    EXPECT_FALSE(glowSeen);
    EXPECT_EQ(t2->noteText(), QString("E2"));
}

TEST(UiSmoke, TunerSwitchesInstrument) {
    TunerWidget t;
    const QStringList std3 = {QString::fromUtf8("Padrão"), "Drop D", "Meio-tom abaixo", "Cromático"};
    const QStringList std4 = {QString::fromUtf8("Padrão"), "Drop D", "Meio-tom abaixo", "DADGAD", "Cromático"};
    auto tunings = [&] {
        QStringList l;
        for (int i = 0; i < t.tuningCombo()->count(); ++i) l << t.tuningCombo()->itemText(i);
        return l;
    };
    ASSERT_EQ(t.instrumentCombo()->count(), 5);
    EXPECT_EQ(t.instrumentCombo()->itemText(0), QString::fromUtf8("Violão"));
    int changes = 0;
    QObject::connect(&t, &TunerWidget::configChanged, &t, [&] { ++changes; });

    selectInstrument(&t, "baixo4");
    EXPECT_EQ(t.stringLabels(), (QStringList{"E1", "A1", "D2", "G2"}));
    EXPECT_EQ(tunings(), std3);
    EXPECT_EQ(changes, 1);  // one notification per user change
    selectInstrument(&t, "baixo5");
    EXPECT_EQ(t.stringLabels(), (QStringList{"B0", "E1", "A1", "D2", "G2"}));
    EXPECT_EQ(tunings(), std3);
    selectInstrument(&t, "baixo6");
    EXPECT_EQ(t.stringLabels(), (QStringList{"B0", "E1", "A1", "D2", "G2", "C3"}));
    EXPECT_EQ(tunings(), std3);
    selectInstrument(&t, "guitarra");
    EXPECT_EQ(tunings(), std4);
    selectInstrument(&t, "violao");
    EXPECT_EQ(t.stringLabels(), (QStringList{"E2", "A2", "D3", "G3", "B3", "E4"}));
    EXPECT_EQ(tunings(), std4);
    EXPECT_EQ(t.instrument().id, std::string("violao"));

    t.tuningCombo()->setCurrentIndex(3);  // DADGAD
    EXPECT_EQ(t.stringLabels(), (QStringList{"D2", "A2", "D3", "G3", "A3", "D4"}));
    t.tuningCombo()->setCurrentIndex(1);  // Drop D
    EXPECT_EQ(t.stringLabels().front(), QString("D2"));
    EXPECT_EQ(t.tuning().name, std::string("Drop D"));

    // A4: default 440.0, step 0.5.
    EXPECT_DOUBLE_EQ(t.a4Spin()->value(), 440.0);
    EXPECT_DOUBLE_EQ(t.a4Spin()->singleStep(), 0.5);
    EXPECT_LE(t.a4Spin()->minimum(), 415.0);
    EXPECT_GE(t.a4Spin()->maximum(), 466.0);
    const int before = changes;
    t.a4Spin()->setValue(442.0);
    EXPECT_DOUBLE_EQ(t.a4(), 442.0);
    EXPECT_EQ(changes, before + 1);

    // A stale reading for a string the current tuning does not have is ignored.
    selectInstrument(&t, "baixo4");
    t.setReading(TunerReading{true, 5, 330.0, 329.6, 2.0});
    EXPECT_EQ(t.highlightedString(), 0);
    EXPECT_EQ(t.statusText(), QString("Sem sinal"));
}

// Pointer smoothing: a step 0 -> +30 cents reaches 90 % within < 100 ms of 16 ms frames,
// without overshoot; unvoiced parks it back at the centre.
TEST(UiSmoke, TunerRespondsWithin100ms) {
    TunerWidget t;
    selectInstrument(&t, "violao");
    t.setReading(TunerReading{true, 0, 82.41, 82.41, 0.0});
    for (int i = 0; i < 100; ++i) t.advance(0.016);
    EXPECT_NEAR(t.centsShown(), 0.0, 1e-6);
    t.setReading(TunerReading{true, 0, hzOf(40, 30.0), 82.41, 30.0});
    int frames = 0;
    while (t.centsShown() < 27.0 && frames < 100) {
        t.advance(0.016);
        ++frames;
        EXPECT_LE(t.centsShown(), 30.0);
    }
    EXPECT_GE(frames, 2) << "no smoothing at all";
    EXPECT_LT(frames * 16, 100) << frames << " frames";
    // Same with the analytic step: 90 % within tau * ln(10).
    EXPECT_LT(TunerWidget::kPointerTauSec * std::log(10.0), 0.1);
    EXPECT_NEAR(TunerWidget::smoothStep(0.0, 30.0, 1.0), 30.0, 1e-6);

    // Far flat (-25 cents): LED crown in dark magenta, no glow (screenshot for review).
    t.resize(1000, 640);
    t.setReading(TunerReading{true, 1, hzOf(45, -25.0), midiToHz(45), -25.0});
    for (int i = 0; i < 40; ++i) t.advance(0.016);
    EXPECT_NEAR(t.centsShown(), -25.0, 0.1);
    EXPECT_FALSE(t.glowActive());
    EXPECT_EQ(t.noteText(), QString("A2"));
    EXPECT_TRUE(t.grab().save(UI_TUNER_FAR_PNG));

    t.setReading(TunerReading{false, -1, 0, 0, 0});
    EXPECT_EQ(t.statusText(), QString("Sem sinal"));
    for (int i = 0; i < 10; ++i) t.advance(0.016);
    EXPECT_NEAR(t.centsShown(), 0.0, 0.5);
    // The animation stops once the pointer has settled and there is no glow.
    for (int i = 0; i < 100; ++i) t.advance(0.016);
    QCoreApplication::processEvents();
    EXPECT_FALSE(t.animating());
}

// Only one session at a time: the tuner runs while its tab is shown; leaving the tab stops
// it; the training tab keeps its own Iniciar/Parar and is stopped when the tuner tab opens.
TEST(UiSmoke, TunerTabSwitchingStartsAndStopsSession) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TunerWidget* t = w.tunerView();
    TempoTrainingView* v = w.tempoView();
    EXPECT_EQ(w.tabs()->currentWidget(), v);
    EXPECT_EQ(ff.calls, 0);

    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    EXPECT_EQ(w.engineBadge()->text(), QString("MOTOR ATIVO"));
    EXPECT_FALSE(v->isRunning());

    w.tabs()->setCurrentWidget(v);  // leaving the tuner stops it
    ASSERT_TRUE(waitTuner(w, false));
    EXPECT_EQ(w.engineBadge()->text(), QString("MOTOR PARADO"));
    EXPECT_TRUE(w.errorBanner()->isHidden());
    EXPECT_TRUE(v->startButton()->isEnabled());

    v->startButton()->click();  // training
    ASSERT_TRUE(waitRunning(w, true));
    EXPECT_FALSE(t->isRunning());
    w.tabs()->setCurrentWidget(t);  // stops the training, starts the tuner
    ASSERT_TRUE(waitTuner(w, true));
    EXPECT_FALSE(v->isRunning());
    EXPECT_EQ(v->startButton()->text(), QString("Iniciar"));

    w.tabs()->setCurrentIndex(2);  // MÚSICAS & MIDI
    ASSERT_TRUE(waitTuner(w, false));
    w.tabs()->setCurrentWidget(t);  // AFINADOR -> runs again
    ASSERT_TRUE(waitTuner(w, true));

    // Parar on the tuner holds (no automatic restart) until Iniciar or the tab is re-entered.
    t->startButton()->click();
    ASSERT_TRUE(waitTuner(w, false));
    for (int i = 0; i < 30; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    EXPECT_FALSE(t->isRunning());
    EXPECT_EQ(t->startButton()->text(), QString("Iniciar"));
    t->startButton()->click();
    ASSERT_TRUE(waitTuner(w, true));

    // Tab changes while a start is in flight: the late start is stopped right away.
    w.tabs()->setCurrentWidget(v);
    ASSERT_TRUE(waitTuner(w, false));
    w.tabs()->setCurrentWidget(t);
    w.tabs()->setCurrentWidget(v);  // before the helper thread reported back
    ASSERT_TRUE(pumpUntil([&] {
        return !t->isRunning() && t->startButton()->text() == QString("Iniciar") &&
               w.engineBadge()->text() == QString("MOTOR PARADO");
    }));
    for (int i = 0; i < 30; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    EXPECT_FALSE(t->isRunning());
    EXPECT_EQ(w.engineBadge()->text(), QString("MOTOR PARADO"));
    EXPECT_TRUE(v->startButton()->isEnabled());
    v->startButton()->click();  // the training still works afterwards
    ASSERT_TRUE(waitRunning(w, true));
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(ff.calls, 1);  // one backend (same devices), one Session, reused
}

// Instrument, tuning, A4 and gain changes restart the tuner session with the new values.
TEST(UiSmoke, TunerRestartsOnConfigChange) {
    FakeFactory ff;
    ff.tones = {{0.1, 1e6, hzOf(28, 0.0), 0.3}};  // E1: out of the violão's range
    MainWindow w(ff.make(), fakeDevices);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TunerWidget* t = w.tunerView();
    selectInstrument(t, "violao");
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    FakeBackend* f = ff.last;
    ASSERT_TRUE(pumpUntil([&] { return t->readingCount() >= 10; }));
    EXPECT_EQ(t->statusText(), QString("Sem sinal"));

    double s0 = f->streamStartSec();
    selectInstrument(t, "baixo4");
    ASSERT_TRUE(pumpUntil([&] { return t->isRunning() && f->streamStartSec() > s0; }));
    ASSERT_TRUE(pumpUntil([&] { return t->noteText() == "E1"; })) << t->statusText().toStdString();
    EXPECT_EQ(t->highlightedString(), 1);

    s0 = f->streamStartSec();
    t->tuningCombo()->setCurrentIndex(2);  // Meio-tom abaixo: E1 is now 100 cents sharp of Eb1
    ASSERT_TRUE(pumpUntil([&] { return t->isRunning() && f->streamStartSec() > s0; }));

    s0 = f->streamStartSec();
    t->a4Spin()->setValue(441.0);
    ASSERT_TRUE(pumpUntil([&] { return t->isRunning() && f->streamStartSec() > s0; }));

    s0 = f->streamStartSec();
    w.gainSpin()->setValue(6.0);  // global gain
    ASSERT_TRUE(pumpUntil([&] { return t->isRunning() && f->streamStartSec() > s0; }));
    EXPECT_TRUE(w.errorBanner()->isHidden());
    EXPECT_EQ(ff.calls, 1);
}

// A start failure shows the pt-BR banner, leaves the pill at "Iniciar" and is not retried
// in a loop.
TEST(UiSmoke, TunerStartFailureShowsBanner) {
    FakeFactory ff;
    ff.failStart = true;
    MainWindow w(ff.make(), fakeDevices);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TunerWidget* t = w.tunerView();
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(pumpUntil([&] { return !w.errorBanner()->isHidden(); }));
    EXPECT_EQ(w.errorBanner()->text(), QString::fromUtf8("Não foi possível iniciar o áudio (teste)."));
    ASSERT_TRUE(waitTuner(w, false));
    EXPECT_TRUE(t->startButton()->isEnabled());
    EXPECT_EQ(w.engineBadge()->text(), QString("FALHA"));
    t->a4Spin()->setValue(441.0);  // a config change while stopped does not start it
    for (int i = 0; i < 30; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    EXPECT_FALSE(t->isRunning());
    EXPECT_EQ(t->startButton()->text(), QString("Iniciar"));

    // The backend cannot even be created.
    MainWindow w2([](const std::string&, const std::string&) { return std::unique_ptr<IAudioBackend>(); },
                  fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w2.devicesReady(); }));
    w2.tabs()->setCurrentWidget(w2.tunerView());
    ASSERT_TRUE(pumpUntil([&] { return !w2.errorBanner()->isHidden(); }));
    EXPECT_FALSE(w2.errorBanner()->text().isEmpty());
    EXPECT_EQ(w2.tunerView()->startButton()->text(), QString("Iniciar"));
    EXPECT_FALSE(w2.tunerView()->isRunning());
}

TEST(UiSmoke, TunerCloseDuringSessionIsClean) {
    FakeFactory ff;
    ff.tones = {{0.1, 1e6, 82.41, 0.3}};
    auto w = std::make_unique<MainWindow>(ff.make(), fakeDevices);
    w->show();
    ASSERT_TRUE(pumpUntil([&] { return w->devicesReady(); }));
    w->tabs()->setCurrentWidget(w->tunerView());
    ASSERT_TRUE(waitTuner(*w, true));
    ASSERT_TRUE(pumpUntil([&] { return w->tunerView()->glowActive(); }));
    w->close();
    EXPECT_FALSE(w->tunerView()->isRunning());
    w.reset();  // stops the session, joins its threads, then destroys the widgets
    QCoreApplication::processEvents();

    // Destroyed while a tuner start is in flight.
    auto w2 = std::make_unique<MainWindow>(ff.make(), fakeDevices);
    ASSERT_TRUE(pumpUntil([&] { return w2->devicesReady(); }));
    w2->tabs()->setCurrentWidget(w2->tunerView());
    w2.reset();
    QCoreApplication::processEvents();
    SUCCEED();
}

// A device scan (↻) that lands while a session runs neither fails nor restarts it: the combo
// refill is silent; only a change of the selected nodes would restart the tuner (once).
TEST(UiSmoke, DeviceScanDuringSessionKeepsIt) {
    std::atomic<int> scans{0}, allowed{1};
    std::atomic<int> listed{0};
    const DeviceLists normal = fakeDevices();
    DeviceLists swapped = normal;  // same devices, other order: the selection must not move
    std::swap(swapped.inputs[0], swapped.inputs[1]);
    std::swap(swapped.outputs[0], swapped.outputs[1]);
    FakeFactory ff;
    MainWindow w(ff.make(), [&] {
        const int n = scans.fetch_add(1);
        while (n >= allowed.load()) QThread::msleep(1);
        listed.fetch_add(1);
        return n % 2 == 1 ? swapped : normal;
    });
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    const std::string in = w.inputCombo()->currentData().toString().toStdString();
    const std::string out = w.outputCombo()->currentData().toString().toStdString();
    TunerWidget* t = w.tunerView();
    TempoTrainingView* v = w.tempoView();
    auto settle = [] {
        for (int i = 0; i < 40; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(2);
        }
    };

    // 1. Tuner: scan in flight, the tuner starts, the scan lands.
    w.refreshDevices();
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    const int starts = ff.starts.load();
    allowed = 2;
    ASSERT_TRUE(pumpUntil([&] { return w.inputCombo()->itemData(0).toString().toStdString() == swapped.inputs[0].name; }));
    settle();
    EXPECT_TRUE(w.errorBanner()->isHidden()) << w.errorBanner()->text().toStdString();
    EXPECT_TRUE(t->isRunning());
    EXPECT_EQ(t->startButton()->text(), QString("Parar"));
    EXPECT_EQ(ff.starts.load(), starts) << "the tuner was restarted by the scan";
    EXPECT_EQ(w.inputCombo()->currentData().toString().toStdString(), in);
    EXPECT_EQ(w.outputCombo()->currentData().toString().toStdString(), out);
    EXPECT_EQ(ff.calls, 1);

    // 2. Training: scan in flight, training starts, the scan lands.
    w.tabs()->setCurrentWidget(v);
    ASSERT_TRUE(waitTuner(w, false));
    w.refreshDevices();
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    const int starts2 = ff.starts.load();
    allowed = 3;
    ASSERT_TRUE(pumpUntil([&] { return w.inputCombo()->itemData(0).toString().toStdString() == normal.inputs[0].name; }));
    settle();
    EXPECT_TRUE(w.errorBanner()->isHidden()) << w.errorBanner()->text().toStdString();
    EXPECT_TRUE(v->isRunning());
    EXPECT_EQ(ff.starts.load(), starts2);
    EXPECT_EQ(w.inputCombo()->currentData().toString().toStdString(), in);
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));
    EXPECT_EQ(ff.calls, 1);
    EXPECT_EQ(listed.load(), 3);
}

// Restart storms: A4 and gain do not restart per keystroke, and a burst of quick changes
// (spin arrows held, typing) restarts the tuner once, after the debounce.
TEST(UiSmoke, TunerRestartDebounced) {
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TunerWidget* t = w.tunerView();
    EXPECT_FALSE(t->a4Spin()->keyboardTracking());
    EXPECT_FALSE(w.gainSpin()->keyboardTracking());
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    const int starts = ff.starts.load();
    for (int i = 1; i <= 10; ++i) {
        if (i % 2)
            w.gainSpin()->setValue(0.5 * i);
        else
            t->a4Spin()->setValue(440.0 + 0.5 * i);
        // ~20 ms between changes (quicker than the debounce).
        QElapsedTimer e;
        e.start();
        while (e.elapsed() < 20) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    ASSERT_TRUE(pumpUntil([&] { return ff.starts.load() > starts && t->isRunning(); }));
    // Generous quiet period: no further restart.
    QElapsedTimer e;
    e.start();
    while (e.elapsed() < 800) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    EXPECT_EQ(ff.starts.load(), starts + 1);
    EXPECT_TRUE(t->isRunning());
    EXPECT_TRUE(w.errorBanner()->isHidden());

    // A pending restart is dropped when the tab is left; closing with one pending is clean.
    w.gainSpin()->setValue(1.0);
    w.tabs()->setCurrentWidget(w.tempoView());
    ASSERT_TRUE(waitTuner(w, false));
    const int after = ff.starts.load();
    e.restart();
    while (e.elapsed() < 600) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    EXPECT_EQ(ff.starts.load(), after);
    EXPECT_FALSE(t->isRunning());
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    w.gainSpin()->setValue(2.0);
    w.close();
    e.restart();
    while (e.elapsed() < 600) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    EXPECT_FALSE(t->isRunning());
}

// ---- settings (Task 12) ----
// Every test uses its own INI file in a QTemporaryDir: the user's ~/.config/lotus-jam is never
// read nor written (main() also points XDG_CONFIG_HOME at a temporary directory).

namespace {

const std::string kMic = "alsa_input.pci-0000_00_1f.3.analog-stereo";
const std::string kPcm = "alsa_input.usb-BurrBrown_from_Texas_Instruments_USB_AUDIO_CODEC-00.analog-stereo";
const std::string kAnalogOut = "alsa_output.pci-0000_00_1f.3.analog-stereo";
const std::string kHdmi = "alsa_output.pci-0000_01_00.1.hdmi-stereo";

std::string currentNode(QComboBox* c) { return c->currentData().toString().toStdString(); }

// Releases a blocked test lister when the test returns (also on a failed ASSERT): declared after
// the window, it runs before the window's destructor joins the listing thread.
struct ReleaseOnExit {
    std::atomic<int>& allowed;
    ~ReleaseOnExit() { allowed = 1 << 20; }
};

void settle(int ms) {
    QElapsedTimer e;
    e.start();
    while (e.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
}

}  // namespace

TEST(UiSmoke, SettingsAppliedOnStartup) {
    // 1. Saved devices present: they are selected; every control shows its saved value, and
    //    already before the first scan has finished.
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        QSettings ini(dir.filePath("lotus-jam.conf"), QSettings::IniFormat);
        Settings s;
        s.inputNode = kMic;  // not the default (PCM2902)
        s.outputNode = kAnalogOut;  // not the default (HDMI)
        s.bpm = 97;
        s.countInBeats = 8;
        s.subdivision = 2;
        s.gainDb = 4.5;
        s.a4 = 442;
        s.instrumentId = "guitarra";
        s.tuningName = "Drop D";
        s.compensationMs = 21;
        saveSettings(ini, s);
        setCompensationFor(ini, kPcm, kHdmi, 5);

        std::atomic<bool> release{false};
        FakeFactory ff;
        MainWindow w(ff.make(), [&] {
            while (!release.load()) QThread::msleep(1);
            return fakeDevices();
        }, &ini);
        TempoTrainingView* v = w.tempoView();
        TunerWidget* t = w.tunerView();
        auto checkControls = [&] {
            EXPECT_EQ(v->bpm(), 97);
            EXPECT_EQ(v->countInBeats(), 8);
            EXPECT_EQ(v->subdivision(), 2);
            EXPECT_DOUBLE_EQ(w.gainSpin()->value(), 4.5);
            EXPECT_DOUBLE_EQ(t->a4(), 442.0);
            EXPECT_EQ(t->instrument().id, "guitarra");
            EXPECT_EQ(t->tuning().name, "Drop D");
            EXPECT_DOUBLE_EQ(w.compensationSpin()->value(), 21.0);
        };
        EXPECT_FALSE(w.devicesReady());
        checkControls();
        release = true;
        ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
        EXPECT_EQ(currentNode(w.inputCombo()), kMic);
        EXPECT_EQ(currentNode(w.outputCombo()), kAnalogOut);
        checkControls();
    }
    // 2. Saved input absent (USB unplugged): the default device is selected (and that pair's
    //    compensation shown), but the saved node is kept in the settings, also after a close.
    {
        QTemporaryDir dir;
        QSettings ini(dir.filePath("lotus-jam.conf"), QSettings::IniFormat);
        Settings s;
        s.inputNode = "alsa_input.usb-Gone_Interface-00.analog-stereo";
        s.outputNode = kHdmi;
        s.compensationMs = 40;
        saveSettings(ini, s);
        setCompensationFor(ini, kPcm, kHdmi, 5);
        FakeFactory ff;
        MainWindow w(ff.make(), fakeDevices, &ini);
        ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
        EXPECT_EQ(currentNode(w.inputCombo()), kPcm);
        EXPECT_EQ(currentNode(w.outputCombo()), kHdmi);
        EXPECT_DOUBLE_EQ(w.compensationSpin()->value(), 5.0);
        // A session started on the fallback device does not forget the saved one.
        w.tempoView()->startButton()->click();
        ASSERT_TRUE(waitRunning(w, true));
        w.tempoView()->startButton()->click();
        ASSERT_TRUE(waitRunning(w, false));
        w.close();
        const Settings after = loadSettings(ini);
        EXPECT_EQ(after.inputNode, s.inputNode);
        EXPECT_EQ(after.outputNode, kHdmi);
        EXPECT_DOUBLE_EQ(compensationFor(ini, s.inputNode, kHdmi), 40.0);
        EXPECT_DOUBLE_EQ(compensationFor(ini, kPcm, kHdmi), 5.0);
    }
    // 3. Empty settings: the prototype's defaults (and device_select's devices).
    {
        QTemporaryDir dir;
        QSettings ini(dir.filePath("lotus-jam.conf"), QSettings::IniFormat);
        FakeFactory ff;
        MainWindow w(ff.make(), fakeDevices, &ini);
        ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
        EXPECT_EQ(w.tempoView()->bpm(), 80);
        EXPECT_EQ(w.tempoView()->countInBeats(), 4);
        EXPECT_EQ(w.tempoView()->subdivision(), 1);
        EXPECT_EQ(w.tunerView()->instrument().id, "baixo4");
        EXPECT_EQ(w.tunerView()->tuning().name, "Padrão");
        EXPECT_DOUBLE_EQ(w.tunerView()->a4(), 440.0);
        EXPECT_DOUBLE_EQ(w.gainSpin()->value(), 0.0);
        EXPECT_DOUBLE_EQ(w.compensationSpin()->value(), 0.0);
        EXPECT_EQ(currentNode(w.inputCombo()), kPcm);
        EXPECT_EQ(currentNode(w.outputCombo()), kHdmi);
    }
    // 4. Nothing saved and the PCM2902 unplugged: the automatic fallback (first device) is used
    //    but not saved, so the PCM2902 wins again at the next start once it is back.
    {
        QTemporaryDir dir;
        QSettings ini(dir.filePath("lotus-jam.conf"), QSettings::IniFormat);
        DeviceLists unplugged = fakeDevices();
        unplugged.inputs.erase(unplugged.inputs.begin() + 1);
        FakeFactory ff;
        {
            MainWindow w(ff.make(), [unplugged] { return unplugged; }, &ini);
            ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
            EXPECT_EQ(currentNode(w.inputCombo()), kMic);
            w.close();
        }
        EXPECT_EQ(loadSettings(ini).inputNode, "");
        MainWindow w(ff.make(), fakeDevices, &ini);
        ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
        EXPECT_EQ(currentNode(w.inputCombo()), kPcm);
    }
}

TEST(UiSmoke, SettingsSavedOnStartAndClose) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("lotus-jam.conf");
    QSettings ini(path, QSettings::IniFormat);
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices, &ini);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TempoTrainingView* v = w.tempoView();
    TunerWidget* t = w.tunerView();

    // Training start saves the training settings and the devices.
    v->bpmSpin()->setValue(120);  // FakeFactory's grid
    v->subdivisionCombo()->setCurrentIndex(v->subdivisionCombo()->findData(2));
    w.gainSpin()->setValue(3.0);
    w.inputCombo()->setCurrentIndex(w.inputCombo()->findData(QString::fromStdString(kMic)));
    w.compensationSpin()->setValue(-12.0);
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, true));
    {
        const Settings s = loadSettings(ini);
        EXPECT_EQ(s.bpm, 120.0);
        EXPECT_EQ(s.subdivision, 2);
        EXPECT_EQ(s.gainDb, 3.0);
        EXPECT_EQ(s.inputNode, kMic);       // chosen by the user
        EXPECT_EQ(s.outputNode, "");        // automatic (device_select's default): not pinned
        EXPECT_EQ(compensationFor(ini, kMic, kHdmi), -12.0);
    }
    v->startButton()->click();
    ASSERT_TRUE(waitRunning(w, false));

    // Tuner start saves the tuner settings.
    selectInstrument(t, "baixo5");
    t->tuningCombo()->setCurrentIndex(t->tuningCombo()->findText("Meio-tom abaixo"));
    t->a4Spin()->setValue(443.0);
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    {
        const Settings s = loadSettings(ini);
        EXPECT_EQ(s.instrumentId, "baixo5");
        EXPECT_EQ(s.tuningName, "Meio-tom abaixo");
        EXPECT_EQ(s.a4, 443.0);
    }

    // Close saves what changed since (no debounce wait) and it reaches the file on disk.
    v->countInCombo()->setCurrentIndex(v->countInCombo()->findData(0));
    w.close();
    ASSERT_TRUE(waitTuner(w, false));
    {
        QSettings fresh(path, QSettings::IniFormat);  // a reader of the file
        EXPECT_EQ(loadSettings(fresh).countInBeats, 0);
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        EXPECT_TRUE(text.contains("countInBeats=0")) << text.toStdString();
        EXPECT_TRUE(text.contains("instrument=baixo5")) << text.toStdString();
    }
}

TEST(UiSmoke, CompensationFollowsDevicePair) {
    QTemporaryDir dir;
    QSettings ini(dir.filePath("lotus-jam.conf"), QSettings::IniFormat);
    setCompensationFor(ini, kPcm, kHdmi, 30);
    setCompensationFor(ini, kMic, kHdmi, 12);
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices, &ini);
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    QComboBox* in = w.inputCombo();
    QComboBox* out = w.outputCombo();
    QDoubleSpinBox* comp = w.compensationSpin();
    ASSERT_EQ(currentNode(in), kPcm);
    EXPECT_DOUBLE_EQ(comp->value(), 30.0);

    in->setCurrentIndex(in->findData(QString::fromStdString(kMic)));
    EXPECT_DOUBLE_EQ(comp->value(), 12.0);
    comp->setValue(7.0);  // stored for the current pair only
    EXPECT_DOUBLE_EQ(compensationFor(ini, kMic, kHdmi), 7.0);
    EXPECT_DOUBLE_EQ(compensationFor(ini, kPcm, kHdmi), 30.0);

    in->setCurrentIndex(in->findData(QString::fromStdString(kPcm)));
    EXPECT_DOUBLE_EQ(comp->value(), 30.0);
    out->setCurrentIndex(out->findData(QString::fromStdString(kAnalogOut)));
    EXPECT_DOUBLE_EQ(comp->value(), 0.0);  // unknown pair: the default
    EXPECT_DOUBLE_EQ(compensationFor(ini, kPcm, kHdmi), 30.0);  // loading wrote nothing
    EXPECT_DOUBLE_EQ(compensationFor(ini, kMic, kHdmi), 7.0);

    // A training session uses the shown (pair's) compensation.
    out->setCurrentIndex(out->findData(QString::fromStdString(kHdmi)));
    in->setCurrentIndex(in->findData(QString::fromStdString(kMic)));
    EXPECT_DOUBLE_EQ(comp->value(), 7.0);
}

// A scan that changes the selection while the tuner runs (the saved USB interface was plugged
// back in) restarts the tuner exactly once, on the new device, with that pair's compensation.
TEST(UiSmoke, ScanSelectionChangeRestartsTunerOnce) {
    QTemporaryDir dir;
    QSettings ini(dir.filePath("lotus-jam.conf"), QSettings::IniFormat);
    const std::string usb = "alsa_input.usb-Late_Interface-00.analog-stereo";
    Settings s;
    s.inputNode = usb;
    s.outputNode = kHdmi;
    saveSettings(ini, s);
    setCompensationFor(ini, usb, kHdmi, 18);
    std::atomic<int> scans{0}, allowed{1};
    DeviceLists plugged = fakeDevices();
    plugged.inputs.push_back({usb, "Late USB Interface"});
    FakeFactory ff;
    MainWindow w(ff.make(), [&] {
        const int n = scans.fetch_add(1);
        while (n >= allowed.load()) QThread::msleep(1);
        return n == 0 ? fakeDevices() : plugged;
    }, &ini);
    ReleaseOnExit release{allowed};
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    ASSERT_EQ(currentNode(w.inputCombo()), kPcm);  // the saved interface is absent
    TunerWidget* t = w.tunerView();
    w.refreshDevices();  // in flight (a scan never starts under a running session)
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    ASSERT_EQ(ff.in, kPcm);
    const int starts = ff.starts.load();
    const int calls = ff.calls;

    allowed = 2;
    ASSERT_TRUE(pumpUntil([&] { return currentNode(w.inputCombo()) == usb; }));
    ASSERT_TRUE(pumpUntil([&] { return ff.starts.load() > starts && t->isRunning(); }));
    settle(800);  // well past the restart debounce
    EXPECT_EQ(ff.starts.load(), starts + 1);
    EXPECT_EQ(ff.calls, calls + 1);  // a new backend for the new pair
    EXPECT_EQ(ff.in, usb);
    EXPECT_TRUE(t->isRunning());
    EXPECT_TRUE(w.errorBanner()->isHidden()) << w.errorBanner()->text().toStdString();
    EXPECT_DOUBLE_EQ(w.compensationSpin()->value(), 18.0);
}

// A device chosen by the user while a scan is in flight is honoured: the running tuner moves
// to it (once) and the scan's arrival neither reverts it nor restarts again.
TEST(UiSmoke, UserSelectionDuringScanHonoured) {
    std::atomic<int> scans{0}, allowed{1}, listed{0};
    FakeFactory ff;
    MainWindow w(ff.make(), [&] {
        const int n = scans.fetch_add(1);
        while (n >= allowed.load()) QThread::msleep(1);
        listed.fetch_add(1);
        return fakeDevices();
    });
    ReleaseOnExit release{allowed};
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    TunerWidget* t = w.tunerView();
    w.refreshDevices();  // blocked in the lister (a scan never starts under a running session)
    w.tabs()->setCurrentWidget(t);
    ASSERT_TRUE(waitTuner(w, true));
    ASSERT_EQ(ff.in, kPcm);
    const int starts = ff.starts.load();

    w.inputCombo()->setCurrentIndex(w.inputCombo()->findData(QString::fromStdString(kMic)));
    ASSERT_TRUE(pumpUntil([&] { return ff.starts.load() > starts && t->isRunning(); }));
    EXPECT_EQ(ff.in, kMic);
    allowed = 2;
    ASSERT_TRUE(pumpUntil([&] { return listed.load() == 2; }));
    settle(800);
    EXPECT_EQ(currentNode(w.inputCombo()), kMic);
    EXPECT_EQ(ff.starts.load(), starts + 1);
    EXPECT_EQ(ff.in, kMic);
    EXPECT_TRUE(t->isRunning());
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
    // Never touch the user's configuration: any QSettings not given an explicit file would land
    // in this temporary directory (the tests pass their own temporary INI files anyway).
    QTemporaryDir config;
    if (!config.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    theme::apply(app);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// Music tab: a MIDI file imported, then Iniciar. Repro for "nao inicia".
TEST(UiSmoke, MusicSessionWithFakeBackend) {
    const QString mid = QDir::tempPath() + "/music_smoke.mid";
    {
        // One track, 480 ticks, 120 BPM: C4 then E4, a quarter note each.
        const unsigned char b[] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0,
                                   'M','T','r','k',0,0,0,0x17,
                                   0,0x90,60,80, 0x83,0x60,0x80,60,0, 0,0x90,64,80, 0x83,0x60,0x80,64,0,
                                   0,0xFF,0x2F,0};
        QFile f(mid);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(reinterpret_cast<const char*>(b), sizeof b);
    }
    FakeFactory ff;
    MainWindow w(ff.make(), fakeDevices);
    w.resize(1180, 760);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    MusicTrainingView* v = w.musicView();
    w.tabs()->setCurrentWidget(v);
    v->loadFile(mid);
    ASSERT_TRUE(v->hasSong());
    ASSERT_EQ(v->song().tracks[0].notes.size(), 2u);
    ASSERT_TRUE(v->startButton()->isEnabled());
    int badgeW = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        v->startButton()->click();
        ASSERT_TRUE(pumpUntil([&] { return v->isRunning() && w.engineBadge()->text() == "MOTOR ATIVO"; }))
            << w.engineBadge()->text().toStdString() << " / " << w.errorBanner()->text().toStdString();
        if (v->isRunning()) v->startButton()->click();  // Parar (the short song may have ended already)
        ASSERT_TRUE(pumpUntil([&] { return !v->isRunning(); }));
        EXPECT_TRUE(w.errorBanner()->isHidden());
        if (cycle == 0) badgeW = w.engineBadge()->width();
        EXPECT_EQ(w.engineBadge()->width(), badgeW);  // badge does not grow with each start/stop
    }
}

// Pausar / Continuar / Reiniciar on the music tab, and the song clock origin.
TEST(UiSmoke, MusicPauseResumeRestart) {
    const QString mid = QDir::tempPath() + "/music_smoke2.mid";
    {
        // 120 BPM, 8 quarter notes C4..: long enough to pause in the middle.
        std::vector<unsigned char> b = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0,'M','T','r','k',0,0,0,0};
        for (int i = 0; i < 8; ++i) for (unsigned char x : {(unsigned char)0, (unsigned char)0x90, (unsigned char)(60 + i), (unsigned char)80,
                                                             (unsigned char)0x83, (unsigned char)0x60, (unsigned char)0x80, (unsigned char)(60 + i), (unsigned char)0}) b.push_back(x);
        for (unsigned char x : {0, 0xFF, 0x2F, 0}) b.push_back(x);
        const size_t len = b.size() - 22;
        b[18] = len >> 24; b[19] = len >> 16; b[20] = len >> 8; b[21] = len;
        QFile f(mid);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(reinterpret_cast<const char*>(b.data()), qint64(b.size()));
    }
    FakeFactory ff;
    ff.speed = 1;  // real-time fake clock so the song position moves slowly
    MainWindow w(ff.make(), fakeDevices);
    w.resize(1180, 760);
    w.show();
    ASSERT_TRUE(pumpUntil([&] { return w.devicesReady(); }));
    MusicTrainingView* v = w.musicView();
    w.tabs()->setCurrentWidget(v);
    v->loadFile(mid);
    ASSERT_TRUE(v->hasSong());
    EXPECT_FALSE(v->pauseButton()->isEnabled());
    EXPECT_FALSE(v->restartButton()->isEnabled());

    v->startButton()->click();
    ASSERT_TRUE(pumpUntil([&] { return v->isRunning(); }));
    EXPECT_TRUE(v->pauseButton()->isEnabled());
    // The count-in is 4 beats (2 s): the roll starts at a negative time and reaches song time 0.
    ASSERT_TRUE(pumpUntil([&] { return v->pianoRoll()->currentTime() > 0.3; }));
    v->pauseButton()->click();  // Pausar
    ASSERT_TRUE(pumpUntil([&] { return !v->isRunning(); }));
    EXPECT_TRUE(v->isPaused());
    EXPECT_EQ(v->pauseButton()->text(), QString("Continuar"));
    const double pausedAt = v->pianoRoll()->currentTime();

    v->pauseButton()->click();  // Continuar
    ASSERT_TRUE(pumpUntil([&] { return v->isRunning(); }));
    EXPECT_FALSE(v->isPaused());
    ASSERT_TRUE(pumpUntil([&] { return v->pianoRoll()->currentTime() > pausedAt + 0.2; }))
        << v->pianoRoll()->currentTime() << " vs " << pausedAt;

    v->restartButton()->click();  // Reiniciar
    ASSERT_TRUE(pumpUntil([&] { return v->isRunning() && v->pianoRoll()->currentTime() < 0.0; }))
        << v->pianoRoll()->currentTime();
    v->startButton()->click();  // Parar
    ASSERT_TRUE(pumpUntil([&] { return !v->isRunning(); }));
    EXPECT_FALSE(v->isPaused());
    EXPECT_FALSE(v->pauseButton()->isEnabled());

    // Seek (5 s steps, clamped to [0, end of the song = 4 s]): idle parks at the new position
    // ("Continuar"); running restarts the session from it.
    EXPECT_TRUE(v->forwardButton()->isEnabled());
    v->forwardButton()->click();
    EXPECT_TRUE(v->isPaused());
    EXPECT_GT(v->pianoRoll()->currentTime(), 3.5);
    v->backButton()->click();
    EXPECT_DOUBLE_EQ(v->pianoRoll()->currentTime(), 0.0);
    v->startButton()->click();
    ASSERT_TRUE(pumpUntil([&] { return v->isRunning(); }));
    ASSERT_TRUE(pumpUntil([&] { return v->pianoRoll()->currentTime() > 0.3; }));
    v->forwardButton()->click();
    ASSERT_TRUE(pumpUntil([&] { return v->isRunning(); }));
    EXPECT_FALSE(v->isPaused());
    v->startButton()->click();
    ASSERT_TRUE(pumpUntil([&] { return !v->isRunning(); }));
}
