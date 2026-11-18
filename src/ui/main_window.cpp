#include "ui/main_window.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "app/settings.h"
#include "audio/pipewire_backend.h"
#include "ui/level_meter.h"
#include "ui/pulse_widget.h"
#include "ui/score_summary_card.h"
#include "ui/tempo_training_view.h"
#include "ui/theme.h"
#include "ui/timeline_widget.h"
#include "ui/tuner_widget.h"

DeviceLists listPipeWireDevices() { return DeviceLists{listInputDevices(), listOutputDevices()}; }

namespace {

void restyle(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QWidget* labelled(const QString& caption, QWidget* control, QWidget* parent) {
    auto* box = new QWidget(parent);
    box->setObjectName("Field");
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    auto* cap = new QLabel(caption, box);
    cap->setProperty("role", "caption");
    v->addWidget(cap);
    v->addWidget(control);
    return box;
}

QWidget* placeholder(const QString& title, const QString& text, QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->addStretch(1);
    auto* t = new QLabel(title, w);
    t->setProperty("role", "placeholderTitle");
    t->setAlignment(Qt::AlignCenter);
    auto* l = new QLabel(text, w);
    l->setProperty("role", "placeholder");
    l->setAlignment(Qt::AlignCenter);
    v->addWidget(t);
    v->addWidget(l);
    v->addStretch(1);
    return w;
}

// Combo box that elides the shown device description to its width (the popup and the
// tooltips carry the full text). With no current item it shows the placeholder text
// ("Procurando dispositivos…", "Nenhum dispositivo encontrado") in the muted colour: Qt only
// substitutes the placeholder inside QComboBox::paintEvent, which this replaces.
class ElidingComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter p(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        if (currentIndex() < 0 && !placeholderText().isEmpty()) {
            opt.currentText = placeholderText();
            for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                opt.palette.setColor(group, QPalette::ButtonText, theme::text_muted);
                opt.palette.setColor(group, QPalette::Text, theme::text_muted);
            }
        }
        p.drawComplexControl(QStyle::CC_ComboBox, opt);
        const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        opt.currentText = fontMetrics().elidedText(opt.currentText, Qt::ElideRight, field.width());
        p.drawControl(QStyle::CE_ComboBoxLabel, opt);
    }
};

// Silent refill (no currentIndexChanged storm: clear() alone emits it with an empty combo);
// the caller compares the selected node before/after. Selection: `preferred` (saved or chosen by
// the user) if listed, else the previous selection if still listed, else `fallback`.
void fillCombo(QComboBox* c, const std::vector<AudioDevice>& devs, const std::string& preferred, int fallback) {
    const QSignalBlocker block(c);
    const QString prev = c->currentData().toString();
    c->clear();
    int widest = 0;
    const QFontMetrics fm = c->view()->fontMetrics();
    for (const auto& d : devs) {
        const QString name = QString::fromStdString(d.name);
        const QString desc = d.description.empty() ? name : QString::fromStdString(d.description);
        c->addItem(desc, name);
        c->setItemData(c->count() - 1, desc + "\n" + name, Qt::ToolTipRole);
        widest = std::max(widest, fm.horizontalAdvance(desc));
    }
    c->view()->setMinimumWidth(widest + 48);  // the popup shows full descriptions
    c->setPlaceholderText(devs.empty() ? QString::fromUtf8("Nenhum dispositivo encontrado") : QString());
    const int pref = preferred.empty() ? -1 : c->findData(QString::fromStdString(preferred));
    const int keep = prev.isEmpty() ? -1 : c->findData(prev);
    c->setCurrentIndex(pref >= 0 ? pref : keep >= 0 ? keep : fallback);
}

std::string nodeOf(const QComboBox* c) { return c->currentData().toString().toStdString(); }

void syncTooltip(QComboBox* c, const QString& fallback) {
    const QString tip = c->currentIndex() >= 0 ? c->itemData(c->currentIndex(), Qt::ToolTipRole).toString() : QString();
    c->setToolTip(tip.isEmpty() ? fallback : tip);
}

}  // namespace

MainWindow::MainWindow(BackendFactory factory, QWidget* parent)
    : MainWindow(std::move(factory), DeviceLister(listPipeWireDevices), nullptr, parent) {
    // Applied before the event loop delivers the first scan (queued to this thread).
    ownSettings_ = std::make_unique<QSettings>("lotus-jam", "lotus-jam");  // ~/.config/lotus-jam/
    settings_ = ownSettings_.get();
    applySettings();
}

MainWindow::MainWindow(BackendFactory factory, DeviceLister lister, QSettings* settings, QWidget* parent)
    : QMainWindow(parent), factory_(std::move(factory)), lister_(std::move(lister)), settings_(settings) {
    theme::initResources();
    setWindowTitle("Lotus Jam");
    setWindowIcon(theme::appIcon());
    events_.reserve(4096);
    buildUi();
    applySettings();

    pollTimer_.setInterval(16);
    connect(&pollTimer_, &QTimer::timeout, this, &MainWindow::poll);
    pollTimer_.start();
    summaryTimer_.setInterval(100);
    connect(&summaryTimer_, &QTimer::timeout, this, [this] {
        if (running_ && session_ && sessMode_ == Mode::Training) tempo_->summaryCard()->setSummary(session_->summary());
    });
    summaryTimer_.start();
    warningTimer_.setSingleShot(true);
    warningTimer_.setInterval(8000);
    connect(&warningTimer_, &QTimer::timeout, warning_, &QWidget::hide);

    refreshDevices();
}

MainWindow::~MainWindow() {
    // No tab/tuner/combo signal may reach this window while QWidget's destructor deletes the
    // children (removing tabs emits currentChanged).
    tabs_->disconnect(this);
    tuner_->disconnect(this);
    for (QObject* o : {static_cast<QObject*>(inCombo_), static_cast<QObject*>(outCombo_), static_cast<QObject*>(gain_),
                       static_cast<QObject*>(comp_), static_cast<QObject*>(tempo_->bpmSpin()),
                       static_cast<QObject*>(tempo_->countInCombo()), static_cast<QObject*>(tempo_->subdivisionCombo())})
        o->disconnect(this);
    if (saveTimer_.isActive()) persistSettings();  // a change not saved yet (no close)
    saveTimer_.stop();
    settings_ = nullptr;  // an injected QSettings may go away right after the window
    tunerArmed_ = false;
    shutdown();
}

void MainWindow::shutdown() {
    restartTimer_.stop();
    tunerRestart_ = false;
    pollTimer_.stop();
    summaryTimer_.stop();
    if (listThread_.joinable()) listThread_.join();
    if (startThread_.joinable()) startThread_.join();
    starting_ = false;
    session_.reset();  // stops and joins the session threads before any widget goes away
    backend_ = nullptr;
    running_ = false;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    tunerArmed_ = false;  // no automatic restart after the window was closed
    flushSettings();
    shutdown();
    tempo_->setRunning(false);
    tuner_->setRunning(false);
    tuner_->clearReading();
    lockInputs(false);
    setEngine(Engine::Idle);
    QMainWindow::closeEvent(e);
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    central->setObjectName("Canvas");
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    // Header.
    auto* header = new QFrame(central);
    header->setObjectName("Header");
    auto* hv = new QVBoxLayout(header);
    hv->setContentsMargins(18, 12, 18, 14);
    hv->setSpacing(12);

    auto* top = new QHBoxLayout;
    top->setSpacing(10);
    auto* icon = new QLabel(header);
    icon->setPixmap(theme::appIcon().pixmap(QSize(34, 34)));
    icon->setFixedSize(34, 34);
    top->addWidget(icon);
    auto* brand = new QLabel("LOTUS JAM", header);
    brand->setObjectName("Brand");
    top->addWidget(brand);
    badge_ = new QLabel(header);
    badge_->setObjectName("EngineBadge");
    top->addWidget(badge_, 0, Qt::AlignVCenter);
    top->addStretch(1);

    inCombo_ = new ElidingComboBox(header);
    outCombo_ = new ElidingComboBox(header);
    for (QComboBox* c : {inCombo_, outCombo_}) {
        c->setMinimumWidth(230);
        c->setMaximumWidth(300);
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(24);
        c->setPlaceholderText(QString::fromUtf8("Procurando dispositivos…"));
    }
    inCombo_->setToolTip("Dispositivo de entrada (PipeWire)");
    outCombo_->setToolTip(QString::fromUtf8("Dispositivo de saída (PipeWire)"));
    connect(inCombo_, &QComboBox::currentIndexChanged, this,
            [this] { syncTooltip(inCombo_, "Dispositivo de entrada (PipeWire)"); });
    connect(outCombo_, &QComboBox::currentIndexChanged, this,
            [this] { syncTooltip(outCombo_, QString::fromUtf8("Dispositivo de saída (PipeWire)")); });
    refresh_ = new QPushButton(QString::fromUtf8("↻"), header);
    refresh_->setObjectName("RefreshButton");
    refresh_->setToolTip(QString::fromUtf8("Atualizar a lista de dispositivos"));
    refresh_->setFixedWidth(40);
    top->addWidget(labelled("ENTRADA", inCombo_, header));
    top->addWidget(labelled(QString::fromUtf8("SAÍDA"), outCombo_, header));
    top->addWidget(refresh_, 0, Qt::AlignBottom);
    hv->addLayout(top);

    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(16);
    level_ = new LevelMeter(header);
    bottom->addWidget(labelled(QString::fromUtf8("NÍVEL DE ENTRADA (dBFS)"), level_, header), 1);
    gain_ = new QDoubleSpinBox(header);
    gain_->setRange(0.0, 24.0);
    gain_->setSingleStep(0.5);
    gain_->setDecimals(1);
    gain_->setSuffix(" dB");
    gain_->setToolTip(QString::fromUtf8("Ganho digital aplicado à análise (vale a partir do próximo início)"));
    gain_->setMinimumWidth(110);
    bottom->addWidget(labelled("GANHO", gain_, header));
    comp_ = new QDoubleSpinBox(header);
    comp_->setRange(-1000.0, 1000.0);
    comp_->setSingleStep(1.0);
    comp_->setDecimals(1);
    comp_->setSuffix(" ms");
    comp_->setToolTip(QString::fromUtf8("Compensação de latência (vale a partir do próximo início)"));
    comp_->setMinimumWidth(120);
    bottom->addWidget(labelled(QString::fromUtf8("COMPENSAÇÃO"), comp_, header));
    hv->addLayout(bottom);
    root->addWidget(header);

    // Banners.
    errorBanner_ = new QLabel(central);
    errorBanner_->setObjectName("ErrorBanner");
    errorBanner_->setWordWrap(true);
    errorBanner_->hide();
    root->addWidget(errorBanner_);
    warning_ = new QLabel(central);
    warning_->setObjectName("WarningNotice");
    warning_->setWordWrap(true);
    warning_->hide();
    root->addWidget(warning_);

    // Tabs.
    tabs_ = new QTabWidget(central);
    tabs_->setDocumentMode(true);
    tabs_->tabBar()->setDrawBase(false);
    tuner_ = new TunerWidget(tabs_);
    tabs_->addTab(tuner_, "AFINADOR");
    tempo_ = new TempoTrainingView(tabs_);
    tabs_->addTab(tempo_, "TREINO DE TEMPO");
    tabs_->addTab(placeholder(QString::fromUtf8("MÚSICAS & MIDI"), QString::fromUtf8("Em breve — Partes 2 e 3"), tabs_),
                  QString::fromUtf8("MÚSICAS && MIDI"));  // "&&": a literal '&' (not a mnemonic)
    tabs_->setCurrentIndex(1);
    root->addWidget(tabs_, 1);

    setCentralWidget(central);
    setMinimumSize(980, 680);
    resize(1180, 780);
    setEngine(Engine::Idle);

    connect(refresh_, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(tempo_, &TempoTrainingView::startRequested, this, [this] { startSession(Mode::Training); });
    connect(tempo_, &TempoTrainingView::stopRequested, this, &MainWindow::stopSession);
    connect(tempo_, &TempoTrainingView::recordRequested, this, &MainWindow::recordSample);
    connect(tempo_, &TempoTrainingView::calibrateRequested, this, &MainWindow::calibrate);

    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(tuner_, &TunerWidget::startRequested, this, [this] {
        tunerArmed_ = true;
        reconcile();
    });
    connect(tuner_, &TunerWidget::stopRequested, this, [this] {
        tunerArmed_ = false;
        reconcile();
    });
    connect(tuner_, &TunerWidget::configChanged, this, &MainWindow::onTunerConfigChanged);
    // The tuner keeps the device combos and the gain enabled: a change restarts it.
    // currentIndexChanged comes only from the user (or tests): a scan refills the combos with
    // their signals blocked and onDevicesListed reports a changed selection once. A choice made
    // while a scan is in flight counts (the scan then keeps it: it becomes the preferred node).
    for (QComboBox* c : {inCombo_, outCombo_})
        connect(c, &QComboBox::currentIndexChanged, this, [this, c] {
            const std::string node = nodeOf(c);
            if (!node.empty()) (c == inCombo_ ? prefIn_ : prefOut_) = node;
            loadPairCompensation();
            scheduleSave();
            onTunerConfigChanged();
        });
    connect(gain_, &QDoubleSpinBox::valueChanged, this, &MainWindow::onTunerConfigChanged);
    // Settings: COMPENSAÇÃO belongs to the device pair; the rest is saved debounced.
    connect(comp_, &QDoubleSpinBox::valueChanged, this, [this](double ms) {
        if (!settings_ || applying_) return;
        const std::string in = nodeOf(inCombo_), out = nodeOf(outCombo_);
        if (!in.empty() && !out.empty()) setCompensationFor(*settings_, in, out, ms);
    });
    connect(gain_, &QDoubleSpinBox::valueChanged, this, &MainWindow::scheduleSave);
    connect(tempo_->bpmSpin(), &QSpinBox::valueChanged, this, &MainWindow::scheduleSave);
    connect(tempo_->countInCombo(), &QComboBox::currentIndexChanged, this, &MainWindow::scheduleSave);
    connect(tempo_->subdivisionCombo(), &QComboBox::currentIndexChanged, this, &MainWindow::scheduleSave);
    connect(tuner_, &TunerWidget::configChanged, this, &MainWindow::scheduleSave);
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(kSaveDebounceMs);
    connect(&saveTimer_, &QTimer::timeout, this, &MainWindow::persistSettings);
    // Restart storms: no restart per keystroke, and a burst of changes restarts once.
    gain_->setKeyboardTracking(false);
    tuner_->a4Spin()->setKeyboardTracking(false);
    restartTimer_.setSingleShot(true);
    restartTimer_.setInterval(kRestartDebounceMs);
    connect(&restartTimer_, &QTimer::timeout, this, [this] {
        if (sessMode_ != Mode::Tuner || !(running_ || starting_)) return;
        tunerRestart_ = true;
        reconcile();
    });
}

QStringList MainWindow::engineBadgeTexts() { return {"MOTOR PARADO", "INICIANDO", "MOTOR ATIVO", "FALHA"}; }

void MainWindow::setEngine(Engine e) {
    switch (e) {
    case Engine::Idle:
        badge_->setText("MOTOR PARADO");
        badge_->setProperty("state", "idle");
        break;
    case Engine::Starting:
        badge_->setText("INICIANDO");
        badge_->setProperty("state", "starting");
        break;
    case Engine::Running:
        badge_->setText("MOTOR ATIVO");
        badge_->setProperty("state", "running");
        break;
    case Engine::Failed:
        badge_->setText("FALHA");
        badge_->setProperty("state", "failed");
        break;
    }
    restyle(badge_);
    // Fixed width = widest state text (QSS letter-spacing included in the polished font), so
    // the badge never clips nor makes the header jump between states.
    badge_->ensurePolished();
    const QString current = badge_->text();
    int widest = 0;
    for (const QString& t : engineBadgeTexts()) {
        badge_->setText(t);  // invalidates QLabel's cached size hint
        widest = std::max(widest, badge_->sizeHint().width());
    }
    badge_->setText(current);
    badge_->setAlignment(Qt::AlignCenter);
    badge_->setFixedWidth(widest + 2);
}

void MainWindow::showError(const QString& text) {
    errorBanner_->setText(text);
    errorBanner_->show();
}

void MainWindow::showWarning(const QString& text) {
    warning_->setText(text);
    warning_->show();
    warningTimer_.start();
}

void MainWindow::lockInputs(bool locked) {
    inCombo_->setEnabled(!locked);
    outCombo_->setEnabled(!locked);
    gain_->setEnabled(!locked);
    comp_->setEnabled(!locked);
}

// ---- devices ----

void MainWindow::refreshDevices() {
    if (listing_ || starting_ || running_) return;  // never re-list under a session
    if (listThread_.joinable()) listThread_.join();  // finished: its result was delivered
    listing_ = true;
    refresh_->setEnabled(false);
    listThread_ = std::thread([this, lister = lister_] {
        DeviceLists d = lister ? lister() : DeviceLists{};
        {
            std::lock_guard<std::mutex> lk(handoff_);
            listed_ = std::move(d);
        }
        QMetaObject::invokeMethod(this, "onDevicesListed", Qt::QueuedConnection);
    });
}

void MainWindow::onDevicesListed() {
    if (listThread_.joinable()) listThread_.join();
    DeviceLists d;
    {
        std::lock_guard<std::mutex> lk(handoff_);
        d = std::move(listed_);
    }
    listing_ = false;
    refresh_->setEnabled(!starting_ && !running_);
    const QString prevIn = inCombo_->currentData().toString(), prevOut = outCombo_->currentData().toString();
    fillCombo(inCombo_, d.inputs, prefIn_, defaultInputIndex(d.inputs));
    fillCombo(outCombo_, d.outputs, prefOut_, defaultOutputIndex(d.outputs));
    const bool selectionChanged =
        inCombo_->currentData().toString() != prevIn || outCombo_->currentData().toString() != prevOut;
    syncTooltip(inCombo_, "Dispositivo de entrada (PipeWire)");
    syncTooltip(outCombo_, QString::fromUtf8("Dispositivo de saída (PipeWire)"));
    const bool first = !devicesReady_;
    if (first || selectionChanged) loadPairCompensation();
    devicesReady_ = true;
    syncStartAllowed();
    if (!first && selectionChanged) onTunerConfigChanged();  // a running tuner follows the new device
    reconcile();  // the tuner tab may be waiting for the first scan
}

// Iniciar of each tab: allowed once the devices are known and while the other tab's session
// is not starting (its helper thread owns the Session meanwhile).
void MainWindow::syncStartAllowed() {
    tempo_->setStartAllowed(devicesReady_ && !(starting_ && sessMode_ == Mode::Tuner));
    tuner_->setStartAllowed(devicesReady_ && !(starting_ && sessMode_ == Mode::Training));
}

void MainWindow::onTabChanged() {
    // A pending (debounced) tuner restart is applied now: reconcile() either stops the tuner
    // (tab left) or restarts it with the current config (never left running on a stale one).
    if (restartTimer_.isActive()) {
        restartTimer_.stop();
        tunerRestart_ = true;
    }
    if (tabs_->currentWidget() == tuner_) tunerArmed_ = true;  // entering the tab (re)starts it
    reconcile();
}

void MainWindow::onTunerConfigChanged() {
    ++tunerGen_;  // readings of the old configuration are dropped from now on
    if (sessMode_ != Mode::Tuner || !(running_ || starting_)) return;  // applies at the next start
    restartTimer_.start();  // debounced: a burst of changes restarts the stream once
}

void MainWindow::reconcile() {
    if (starting_ || reconciling_) return;  // onStartFinished() calls it again
    reconciling_ = true;
    QWidget* current = tabs_->currentWidget();
    const bool wantTuner = current == tuner_ && tunerArmed_ && devicesReady_;
    if (running_) {
        const bool keep = sessMode_ == Mode::Tuner ? wantTuner && !tunerRestart_ : current == tempo_;
        if (!keep) stopSession();  // synchronous: the Stopped event is dispatched here
    }
    if (!running_ && wantTuner) {
        tunerRestart_ = false;
        startSession(Mode::Tuner);
    }
    reconciling_ = false;
}

// ---- session ----

void MainWindow::startSession(Mode mode) {
    if (starting_ || running_ || !devicesReady_) return;  // Iniciar is disabled until the first scan
    const bool tuner = mode == Mode::Tuner;
    // A tuner that cannot start stays stopped (pill "Iniciar") until the user asks again.
    auto failTuner = [this, tuner] {
        if (!tuner) return;
        tunerArmed_ = false;
        tuner_->setRunning(false);
    };
    if (!pollTimer_.isActive()) pollTimer_.start();  // stopped by a previous close
    if (!summaryTimer_.isActive()) summaryTimer_.start();
    poll();  // drain what is left of a previous session (its Stopped, a late WAV error)
    if (inCombo_->count() == 0 || outCombo_->count() == 0) {
        showError(QString::fromUtf8("Nenhum dispositivo de áudio encontrado. Conecte a interface e clique em ↻."));
        failTuner();
        return;
    }
    flushSettings();  // what the user starts with is what the next launch shows
    const std::string in = inCombo_->currentData().toString().toStdString();
    const std::string out = outCombo_->currentData().toString().toStdString();
    if (!session_ || in != sessIn_ || out != sessOut_) {
        session_.reset();
        backend_ = nullptr;
        std::unique_ptr<IAudioBackend> b = factory_ ? factory_(in, out) : nullptr;
        if (!b) {
            showError(QString::fromUtf8("Não foi possível abrir os dispositivos de áudio selecionados."));
            setEngine(Engine::Failed);
            failTuner();
            return;
        }
        backend_ = b.get();
        session_ = std::make_unique<Session>(std::move(b));
        sessIn_ = in;
        sessOut_ = out;
    }

    SessionConfig cfg;
    cfg.mode = mode;
    cfg.compensationMs = comp_->value();
    if (!tuner) {
        sessComp_ = cfg.compensationMs;
        stepMs_ = 60000.0 / tempo_->bpm() / tempo_->subdivision();
        offsets_.clear();
        tempo_->setCalibrationAvailable(false);
        tempo_->setCalibrationMessage({});
    }
    cfg.gainDb = gain_->value();
    if (tuner) {
        // Fixed for the session (the DSP Tuner has no setter): configChanged restarts it.
        cfg.instrument = &tuner_->instrument();
        cfg.tuning = tuner_->tuning();
        cfg.a4 = tuner_->a4();
        sessTunerGen_ = tunerGen_;
    } else {
        cfg.bpm = tempo_->bpm();
        cfg.countIn = tempo_->countInBeats();
        cfg.subdivision = tempo_->subdivision();
    }
    countIn_ = cfg.countIn;
    sessMode_ = mode;

    errorBanner_->hide();
    warning_->hide();
    level_->reset();
    recPending_ = false;
    userStop_ = false;
    starting_ = true;
    if (tuner) {
        tuner_->clearReading();
        tuner_->setStarting(true);
    } else {
        tempo_->pulse()->reset();
        tempo_->timeline()->clear();
        tempo_->summaryCard()->clear();
        tempo_->setRecordingState(TempoTrainingView::Rec::None);
        tempo_->setStarting(true);
        lockInputs(true);
    }
    syncStartAllowed();
    refresh_->setEnabled(false);
    setEngine(Engine::Starting);

    Session* s = session_.get();
    startThread_ = std::thread([this, s, cfg] {
        std::string err;
        const bool ok = s->start(cfg, err);
        {
            std::lock_guard<std::mutex> lk(handoff_);
            startOk_ = ok;
            startErr_ = std::move(err);
        }
        QMetaObject::invokeMethod(this, "onStartFinished", Qt::QueuedConnection);
    });
}

void MainWindow::onStartFinished() {
    if (!starting_) return;  // shut down meanwhile
    if (startThread_.joinable()) startThread_.join();
    bool ok;
    std::string err;
    {
        std::lock_guard<std::mutex> lk(handoff_);
        ok = startOk_;
        err = std::move(startErr_);
    }
    starting_ = false;
    const bool tuner = sessMode_ == Mode::Tuner;
    syncStartAllowed();
    if (!ok) {
        refresh_->setEnabled(!listing_);
        running_ = false;
        if (tuner) {
            tunerArmed_ = false;  // no retry loop: the pill shows "Iniciar"
            tunerRestart_ = false;
            tuner_->setRunning(false);
        } else {
            tempo_->setRunning(false);
        }
        lockInputs(false);
        setEngine(Engine::Failed);
        showError(QString::fromStdString(err));
        return;
    }
    running_ = true;
    if (tuner)
        tuner_->setRunning(true);
    else
        tempo_->setRunning(true);
    setEngine(Engine::Running);
    reconcile();  // the tab, the tuner config or the pill may have changed meanwhile
}

void MainWindow::stopSession() {
    if (starting_ || !session_) return;
    userStop_ = true;
    session_->stop();
    poll();  // delivers the Stopped event (and the final summary) right away
}

void MainWindow::recordSample() {
    if (!running_ || !session_) return;
    if (recPending_) return;
    QString dir = sampleDir_;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty()) dir = QDir::homePath() + "/.local/share/lotus-jam";
        dir += "/amostras";
    }
    QDir().mkpath(dir);
    const QString path =
        dir + "/amostra-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".wav";
    session_->recordSample(path.toStdString(), sampleSeconds_);
    recPending_ = true;
    recError_ = false;
    recPath_ = path;
    // Judged on the backend clock: the take plus a grace for the background WAV write (an
    // Error from the writer arrives within it).
    recDeadline_ = (backend_ ? backend_->nowSec() : 0.0) + sampleSeconds_ + kRecGraceSec;
    tempo_->setRecordingState(TempoTrainingView::Rec::Recording, path);
}

void MainWindow::poll() {
    if (starting_ || !session_) return;
    events_.clear();
    if (session_->pollEvents(events_)) dispatchEvents(events_);
    if (recPending_ && running_ && backend_ && backend_->nowSec() >= recDeadline_) {
        recPending_ = false;
        tempo_->setRecordingState(recError_ ? TempoTrainingView::Rec::Failed : TempoTrainingView::Rec::Saved, recPath_);
    }
}

void MainWindow::addOffset(double ms) {
    if (!std::isfinite(ms)) return;
    offsets_.push_back(ms);
    if (offsets_.size() == 8) tempo_->setCalibrationAvailable(true);
}

// Circular mean of the offsets over one beat step: it still finds the delay when it is larger
// than the scoring window (notes then show up as extras, which the plain mean of hits misses).
void MainWindow::calibrate() {
    const size_t n = offsets_.size();
    if (n < 8 || stepMs_ <= 0.0) {
        tempo_->setCalibrationMessage(QString::fromUtf8("Toque pelo menos 8 notas junto ao clique antes de calibrar."));
        return;
    }
    const double twoPi = 6.283185307179586;
    double c = 0.0, s = 0.0;
    for (double e : offsets_) {
        c += std::cos(twoPi * e / stepMs_);
        s += std::sin(twoPi * e / stepMs_);
    }
    c /= double(n);
    s /= double(n);
    const double r = std::hypot(c, s);
    if (r < 0.5) {
        tempo_->setCalibrationMessage(QString::fromUtf8(
            "As notas não seguem o clique (concentração %1%). Toque notas regulares, uma por batida, e tente de novo.")
                                          .arg(int(r * 100)));
        return;
    }
    const double bias = std::atan2(s, c) * stepMs_ / twoPi;
    const double now = std::clamp(sessComp_ + bias, comp_->minimum(), comp_->maximum());
    comp_->setValue(now);
    tempo_->setCalibrationMessage(QString::fromUtf8("Compensação ajustada para %1 ms (%2 notas, concentração %3%). Inicie de novo para testar.")
                                      .arg(now, 0, 'f', 1)
                                      .arg(n)
                                      .arg(int(r * 100)));
}

void MainWindow::dispatchEvents(const std::vector<UiEvent>& events) {
    double peak = -1.0;
    bool stopped = false;
    for (const UiEvent& e : events) {
        switch (e.type) {
        case UiEvent::Beat: {
            const double now = backend_ ? backend_->nowSec() : e.a;
            tempo_->pulse()->scheduleBeat(e.n, e.a, now, countIn_);
            break;
        }
        case UiEvent::Note:
            tempo_->timeline()->addNote(e.n, e.a);
            addOffset(e.a);
            break;
        case UiEvent::Miss:
            tempo_->timeline()->addMiss(e.n);
            break;
        case UiEvent::Extra:  // counted in the summary; still useful to calibrate the latency
            addOffset(e.a);
            break;
        case UiEvent::Tuner:  // only readings of the tuner's current configuration
            if (sessMode_ == Mode::Tuner && running_ && sessTunerGen_ == tunerGen_) tuner_->setReading(e.tuner);
            break;
        case UiEvent::Level:
            peak = std::max(peak, e.a);
            break;
        case UiEvent::Error:  // the badge turns FALHA only on a device failure (see Stopped)
            showError(QString::fromStdString(e.text));
            if (recPending_) recError_ = true;
            break;
        case UiEvent::Warning:
            showWarning(QString::fromStdString(e.text));
            break;
        case UiEvent::Stopped:
            stopped = true;
            break;
        }
    }
    if (peak >= 0.0) level_->setPeak(peak);
    if (stopped && running_) {
        running_ = false;
        lockInputs(false);
        refresh_->setEnabled(!listing_);
        level_->reset();
        if (sessMode_ == Mode::Tuner) {
            tuner_->setRunning(false);
            tuner_->clearReading();
            if (!userStop_) tunerArmed_ = false;  // device failure: no automatic restart
        } else {
            tempo_->setRunning(false);
            if (session_) tempo_->summaryCard()->setSummary(session_->summary());
            tempo_->pulse()->idle();
        }
        if (recPending_) {
            recPending_ = false;
            tempo_->setRecordingState(TempoTrainingView::Rec::Interrupted, recPath_);
        }
        // Not asked for by Parar: the device failed (its Error is already in the banner).
        setEngine(userStop_ ? Engine::Idle : Engine::Failed);
    }
}

// ---- settings ----

void MainWindow::applySettings() {
    if (!settings_) return;
    const Settings v = loadSettings(*settings_);
    applying_ = true;
    prefIn_ = v.inputNode;
    prefOut_ = v.outputNode;
    tempo_->bpmSpin()->setValue(int(std::lround(v.bpm)));
    if (const int i = tempo_->countInCombo()->findData(v.countInBeats); i >= 0) tempo_->countInCombo()->setCurrentIndex(i);
    if (const int i = tempo_->subdivisionCombo()->findData(v.subdivision); i >= 0)
        tempo_->subdivisionCombo()->setCurrentIndex(i);
    gain_->setValue(v.gainDb);
    if (const int i = tuner_->instrumentCombo()->findData(QString::fromStdString(v.instrumentId)); i >= 0)
        tuner_->instrumentCombo()->setCurrentIndex(i);  // rebuilds the tunings
    if (const int i = tuner_->tuningCombo()->findText(QString::fromStdString(v.tuningName)); i >= 0)
        tuner_->tuningCombo()->setCurrentIndex(i);
    tuner_->a4Spin()->setValue(v.a4);
    comp_->setValue(v.compensationMs);  // the saved pair's (reloaded once the devices are known)
    applying_ = false;
    saveTimer_.stop();  // loading is not a change
}

void MainWindow::loadPairCompensation() {
    if (!settings_) return;
    const std::string in = nodeOf(inCombo_), out = nodeOf(outCombo_);
    if (in.empty() || out.empty()) return;
    applying_ = true;
    comp_->setValue(compensationFor(*settings_, in, out));
    applying_ = false;
}

void MainWindow::scheduleSave() {
    if (settings_ && !applying_) saveTimer_.start();
}

void MainWindow::persistSettings() {
    saveTimer_.stop();
    if (!settings_) return;
    Settings v;
    const std::string in = nodeOf(inCombo_), out = nodeOf(outCombo_);
    // Only a device the user chose (now or in an earlier run) is saved, also while it is absent;
    // an automatic choice is not (empty = device_select's default at every start, so a fallback
    // picked while the PCM2902 is unplugged never takes its place). The current pair's
    // compensation is stored by the COMPENSAÇÃO handler.
    v.inputNode = prefIn_;
    v.outputNode = prefOut_;
    // saveSettings stores compensationMs under (inputNode, outputNode): the shown value belongs
    // to the current pair only.
    v.compensationMs = v.inputNode == in && v.outputNode == out ? comp_->value()
                                                                : compensationFor(*settings_, v.inputNode, v.outputNode);
    if (!in.empty() && !out.empty()) setCompensationFor(*settings_, in, out, comp_->value());
    v.bpm = tempo_->bpm();
    v.countInBeats = tempo_->countInBeats();
    v.subdivision = tempo_->subdivision();
    v.gainDb = gain_->value();
    v.a4 = tuner_->a4();
    v.instrumentId = tuner_->instrument().id;
    v.tuningName = tuner_->tuning().name;
    saveSettings(*settings_, v);
}

void MainWindow::flushSettings() {
    if (!settings_) return;
    persistSettings();
    settings_->sync();  // a few hundred bytes; only on start and close
    if (settings_->status() != QSettings::NoError && !settingsWarned_) {
        settingsWarned_ = true;  // once: an unwritable config must not flood the log
        qWarning("Lotus Jam: could not write the settings to %s", qPrintable(settings_->fileName()));
    }
}
