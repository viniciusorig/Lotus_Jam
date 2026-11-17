#include "ui/main_window.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "audio/pipewire_backend.h"
#include "ui/level_meter.h"
#include "ui/pulse_widget.h"
#include "ui/score_summary_card.h"
#include "ui/tempo_training_view.h"
#include "ui/theme.h"
#include "ui/timeline_widget.h"

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

void fillCombo(QComboBox* c, const std::vector<AudioDevice>& devs, int fallback) {
    const QString prev = c->currentData().toString();
    c->clear();
    for (const auto& d : devs) {
        const QString name = QString::fromStdString(d.name);
        const QString desc = d.description.empty() ? name : QString::fromStdString(d.description);
        c->addItem(desc, name);
        c->setItemData(c->count() - 1, name, Qt::ToolTipRole);
    }
    const int keep = prev.isEmpty() ? -1 : c->findData(prev);
    c->setCurrentIndex(keep >= 0 ? keep : fallback);
}

}  // namespace

MainWindow::MainWindow(BackendFactory factory, QWidget* parent)
    : MainWindow(std::move(factory), DeviceLister(listPipeWireDevices), parent) {}

MainWindow::MainWindow(BackendFactory factory, DeviceLister lister, QWidget* parent)
    : QMainWindow(parent), factory_(std::move(factory)), lister_(std::move(lister)) {
    theme::initResources();
    setWindowTitle("Lotus Jam");
    setWindowIcon(theme::appIcon());
    events_.reserve(4096);
    buildUi();

    pollTimer_.setInterval(16);
    connect(&pollTimer_, &QTimer::timeout, this, &MainWindow::poll);
    pollTimer_.start();
    summaryTimer_.setInterval(100);
    connect(&summaryTimer_, &QTimer::timeout, this, [this] {
        if (running_ && session_) tempo_->summaryCard()->setSummary(session_->summary());
    });
    summaryTimer_.start();
    warningTimer_.setSingleShot(true);
    warningTimer_.setInterval(8000);
    connect(&warningTimer_, &QTimer::timeout, warning_, &QWidget::hide);

    refreshDevices();
}

MainWindow::~MainWindow() { shutdown(); }

void MainWindow::shutdown() {
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
    shutdown();
    tempo_->setRunning(false);
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

    inCombo_ = new QComboBox(header);
    inCombo_->setMinimumWidth(230);
    inCombo_->setToolTip("Dispositivo de entrada (PipeWire)");
    outCombo_ = new QComboBox(header);
    outCombo_->setMinimumWidth(230);
    outCombo_->setToolTip(QString::fromUtf8("Dispositivo de saída (PipeWire)"));
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
    tabs_->addTab(placeholder("AFINADOR", QString::fromUtf8("Afinador — em breve"), tabs_), "AFINADOR");
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
    connect(tempo_, &TempoTrainingView::startRequested, this, &MainWindow::startSession);
    connect(tempo_, &TempoTrainingView::stopRequested, this, &MainWindow::stopSession);
    connect(tempo_, &TempoTrainingView::recordRequested, this, &MainWindow::recordSample);
}

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
    if (listing_) return;
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
    refresh_->setEnabled(true);
    fillCombo(inCombo_, d.inputs, defaultInputIndex(d.inputs));
    fillCombo(outCombo_, d.outputs, defaultOutputIndex(d.outputs));
    devicesReady_ = true;
}

// ---- session ----

void MainWindow::startSession() {
    if (starting_ || running_) return;
    if (!pollTimer_.isActive()) pollTimer_.start();  // stopped by a previous close
    if (!summaryTimer_.isActive()) summaryTimer_.start();
    poll();  // drain what is left of a previous session (its Stopped, a late WAV error)
    if (inCombo_->count() == 0 || outCombo_->count() == 0) {
        showError(QString::fromUtf8("Nenhum dispositivo de áudio encontrado. Conecte a interface e clique em ↻."));
        return;
    }
    const std::string in = inCombo_->currentData().toString().toStdString();
    const std::string out = outCombo_->currentData().toString().toStdString();
    if (!session_ || in != sessIn_ || out != sessOut_) {
        session_.reset();
        backend_ = nullptr;
        std::unique_ptr<IAudioBackend> b = factory_ ? factory_(in, out) : nullptr;
        if (!b) {
            showError(QString::fromUtf8("Não foi possível abrir os dispositivos de áudio selecionados."));
            setEngine(Engine::Failed);
            return;
        }
        backend_ = b.get();
        session_ = std::make_unique<Session>(std::move(b));
        sessIn_ = in;
        sessOut_ = out;
    }

    SessionConfig cfg;
    cfg.mode = Mode::Training;
    cfg.bpm = tempo_->bpm();
    cfg.countIn = tempo_->countInBeats();
    cfg.subdivision = tempo_->subdivision();
    cfg.compensationMs = comp_->value();
    cfg.gainDb = gain_->value();
    countIn_ = cfg.countIn;

    errorBanner_->hide();
    warning_->hide();
    tempo_->pulse()->reset();
    tempo_->timeline()->clear();
    tempo_->summaryCard()->clear();
    level_->reset();
    starting_ = true;
    tempo_->setStarting(true);
    lockInputs(true);
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
    refresh_->setEnabled(!listing_);
    if (!ok) {
        running_ = false;
        tempo_->setRunning(false);
        lockInputs(false);
        setEngine(Engine::Failed);
        showError(QString::fromStdString(err));
        return;
    }
    running_ = true;
    tempo_->setRunning(true);
    setEngine(Engine::Running);
}

void MainWindow::stopSession() {
    if (starting_ || !session_) return;
    session_->stop();
    poll();  // delivers the Stopped event (and the final summary) right away
}

void MainWindow::recordSample() {
    if (!running_ || !session_) return;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.local/share/lotus-jam";
    dir += "/amostras";
    QDir().mkpath(dir);
    const QString path =
        dir + "/amostra-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".wav";
    session_->recordSample(path.toStdString(), 10.0);
    tempo_->showRecording(path, 10.0);
}

void MainWindow::poll() {
    if (starting_ || !session_) return;
    events_.clear();
    if (session_->pollEvents(events_)) dispatchEvents(events_);
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
            break;
        case UiEvent::Miss:
            tempo_->timeline()->addMiss(e.n);
            break;
        case UiEvent::Extra:  // counted in the summary
            break;
        case UiEvent::Tuner:  // tuner view: Task 11
            break;
        case UiEvent::Level:
            peak = std::max(peak, e.a);
            break;
        case UiEvent::Error:
            showError(QString::fromStdString(e.text));
            setEngine(Engine::Failed);
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
        tempo_->setRunning(false);
        lockInputs(false);
        refresh_->setEnabled(!listing_);
        if (session_) tempo_->summaryCard()->setSummary(session_->summary());
        level_->reset();
        tempo_->pulse()->idle();
        if (errorBanner_->isHidden()) setEngine(Engine::Idle);
    }
}
