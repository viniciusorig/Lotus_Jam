#include "ui/music_training_view.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "dsp/bass_mapper.h"
#include "ui/piano_roll_widget.h"
#include "ui/theme.h"

namespace {

QWidget* field(const QString& caption, QWidget* control, QWidget* parent) {
    auto* box = new QWidget(parent);
    box->setObjectName("Field");
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    auto* cap = new QLabel(caption, box);
    cap->setProperty("role", "caption");
    v->addWidget(cap);
    v->addWidget(control);
    return box;
}

}  // namespace

MusicTrainingView::MusicTrainingView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 14, 0, 0);
    root->setSpacing(14);

    // Controls panel
    auto* controls = new QFrame(this);
    controls->setObjectName("CardPanel");
    auto* ch = new QHBoxLayout(controls);
    ch->setContentsMargins(18, 14, 18, 14);
    ch->setSpacing(16);

    // Import button
    importBtn_ = new QPushButton(QString::fromUtf8("Importar MIDI…"), controls);
    importBtn_->setMinimumWidth(140);
    importBtn_->setCursor(Qt::PointingHandCursor);
    ch->addWidget(importBtn_);

    // File label
    fileLabel_ = new QLabel(QString::fromUtf8("Nenhum arquivo"), controls);
    fileLabel_->setProperty("role", "secondary");
    fileLabel_->setMinimumWidth(200);
    ch->addWidget(fileLabel_);

    // Track selection
    trackCombo_ = new QComboBox(controls);
    trackCombo_->setMinimumWidth(150);
    trackCombo_->setEnabled(false);
    ch->addWidget(field("TRILHA", trackCombo_, controls));

    // Speed control
    speedSlider_ = new QSlider(Qt::Horizontal, controls);
    speedSlider_->setRange(50, 120);  // 50% to 120%
    speedSlider_->setValue(100);
    speedSlider_->setMinimumWidth(120);
    speedSlider_->setToolTip(QString::fromUtf8("Velocidade de reprodução (50%–120%)"));

    speedLabel_ = new QLabel("100%", controls);
    speedLabel_->setMinimumWidth(50);

    auto* speedBox = new QWidget(controls);
    auto* speedLayout = new QHBoxLayout(speedBox);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedLayout->setSpacing(8);
    speedLayout->addWidget(speedSlider_);
    speedLayout->addWidget(speedLabel_);
    ch->addWidget(field("VELOCIDADE", speedBox, controls));

    tabCheck_ = new QCheckBox(QString::fromUtf8("Corda:casa"), controls);
    tabCheck_->setToolTip(QString::fromUtf8(
        "Mostra em cada nota a corda e a casa onde tocá-la. Notas fora da extensão do instrumento são deslocadas por oitavas."));
    ch->addWidget(tabCheck_);

    ch->addStretch(1);

    back_ = new QPushButton(QString::fromUtf8("⏪ 5s"), controls);
    fwd_ = new QPushButton(QString::fromUtf8("5s ⏩"), controls);
    back_->setToolTip(QString::fromUtf8("Voltar 5 segundos na música"));
    fwd_->setToolTip(QString::fromUtf8("Avançar 5 segundos na música"));
    for (QPushButton* b : {back_, fwd_}) {
        b->setMinimumWidth(72);
        b->setCursor(Qt::PointingHandCursor);
        b->setEnabled(false);
        ch->addWidget(b, 0, Qt::AlignBottom);
    }

    pause_ = new QPushButton("Pausar", controls);
    pause_->setMinimumWidth(100);
    pause_->setCursor(Qt::PointingHandCursor);
    pause_->setEnabled(false);
    ch->addWidget(pause_, 0, Qt::AlignBottom);

    restart_ = new QPushButton(QString::fromUtf8("Reiniciar"), controls);
    restart_->setMinimumWidth(100);
    restart_->setCursor(Qt::PointingHandCursor);
    restart_->setEnabled(false);
    ch->addWidget(restart_, 0, Qt::AlignBottom);

    // Start/Stop button
    start_ = new QPushButton("Iniciar", controls);
    start_->setObjectName("PrimaryAction");
    start_->setProperty("running", false);
    start_->setMinimumWidth(132);
    start_->setCursor(Qt::PointingHandCursor);
    start_->setEnabled(false);
    start_->setToolTip(QString::fromUtf8("Importe um arquivo MIDI primeiro"));
    ch->addWidget(start_, 0, Qt::AlignBottom);

    root->addWidget(controls);

    // Piano roll
    roll_ = new PianoRollWidget(this);
    root->addWidget(roll_, 1);

    // Stats panel
    auto* statsCard = new QFrame(this);
    statsCard->setObjectName("CardPanel");
    auto* sv = new QVBoxLayout(statsCard);
    sv->setContentsMargins(16, 8, 16, 8);

    statsLabel_ = new QLabel(QString::fromUtf8("Importe um arquivo MIDI para começar"), statsCard);
    statsLabel_->setProperty("role", "secondary");
    sv->addWidget(statsLabel_);

    root->addWidget(statsCard);

    // Connections
    connect(importBtn_, &QPushButton::clicked, this, &MusicTrainingView::onImport);
    connect(trackCombo_, &QComboBox::currentIndexChanged, this, &MusicTrainingView::onTrackChanged);
    connect(speedSlider_, &QSlider::valueChanged, this, &MusicTrainingView::onSpeedChanged);
    connect(tabCheck_, &QCheckBox::toggled, this, [this] { refreshLabels(); });
    connect(back_, &QPushButton::clicked, this, [this] { Q_EMIT seekRequested(-5.0); });
    connect(fwd_, &QPushButton::clicked, this, [this] { Q_EMIT seekRequested(5.0); });
    connect(pause_, &QPushButton::clicked, this, &MusicTrainingView::pauseRequested);
    connect(restart_, &QPushButton::clicked, this, &MusicTrainingView::restartRequested);
    connect(start_, &QPushButton::clicked, this, [this] {
        if (running_)
            Q_EMIT stopRequested();
        else
            Q_EMIT startRequested();
    });
}

MusicTrainingView::~MusicTrainingView() = default;

void MusicTrainingView::onImport() {
    QString path = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("Importar Arquivo MIDI"),
        QString(),
        QString::fromUtf8("Arquivos MIDI (*.mid *.midi);;Todos os arquivos (*)")
    );
    if (!path.isEmpty()) loadFile(path);
}

void MusicTrainingView::loadFile(const QString& path) {
    paused_ = false;
    filePath_ = path;
    song_ = parseMidiFile(path.toStdString());

    // Update UI
    QFileInfo fi(path);
    fileLabel_->setText(fi.fileName());
    updateTrackCombo();

    if (!song_.tracks.empty()) {
        trackCombo_->setCurrentIndex(0);
        onTrackChanged(0);
        refreshButtons();
        start_->setEnabled(startAllowed_);
        start_->setToolTip(QString::fromUtf8("Iniciar treino com a música"));
        statsLabel_->setText(QString::fromUtf8("Pronto para treinar • %1 notas na trilha selecionada")
                            .arg(song_.tracks[0].notes.size()));
    } else {
        statsLabel_->setText(QString::fromUtf8("Arquivo sem trilhas válidas"));
        roll_->clear();
        start_->setEnabled(false);
        refreshButtons();
    }

    Q_EMIT configChanged();
}

void MusicTrainingView::updateTrackCombo() {
    const QSignalBlocker block(trackCombo_);
    trackCombo_->clear();

    for (size_t i = 0; i < song_.tracks.size(); ++i) {
        const auto& t = song_.tracks[i];
        QString label = QString("%1: %2 (%3 notas)")
            .arg(i + 1)
            .arg(QString::fromStdString(t.name))
            .arg(t.notes.size());
        trackCombo_->addItem(label);
    }

    trackCombo_->setEnabled(!song_.tracks.empty());
}

void MusicTrainingView::onTrackChanged(int index) {
    paused_ = false;  // a resume position belongs to one track
    refreshButtons();
    if (index >= 0 && index < static_cast<int>(song_.tracks.size())) {
        roll_->setSong(song_, index);
        refreshLabels();
        statsLabel_->setText(QString::fromUtf8("Trilha %1 • %2 notas")
                            .arg(index + 1)
                            .arg(song_.tracks[static_cast<size_t>(index)].notes.size()));
    }
    Q_EMIT configChanged();
}

void MusicTrainingView::onSpeedChanged(int value) {
    updateSpeedLabel();
    Q_EMIT configChanged();
}

void MusicTrainingView::updateSpeedLabel() {
    speedLabel_->setText(QString("%1%").arg(speedSlider_->value()));
}

QString MusicTrainingView::filePath() const { return filePath_; }

int MusicTrainingView::selectedTrack() const { return trackCombo_->currentIndex(); }

double MusicTrainingView::speedFactor() const {
    return speedSlider_->value() / 100.0;
}

void MusicTrainingView::refreshButtons() {
    pause_->setText(paused_ ? "Continuar" : "Pausar");
    pause_->setEnabled(running_ || paused_);
    restart_->setEnabled(running_ || paused_);
    back_->setEnabled(hasSong());
    fwd_->setEnabled(hasSong());
}

void MusicTrainingView::setTuning(const Tuning& t) {
    tuning_ = t;
    refreshLabels();
}

// "corda:casa" on each note (the string's letter, with its octave when two strings share one).
void MusicTrainingView::refreshLabels() {
    const int i = selectedTrack();
    if (!tabCheck_->isChecked() || tuning_.strings.empty() || i < 0 || i >= static_cast<int>(song_.tracks.size())) {
        roll_->setLabels({});
        return;
    }
    QStringList names;
    bool unique = true;
    for (int m : tuning_.strings) {
        QString n = QString::fromStdString(noteName(m));
        names << n.left(n.size() - 1);
        unique = unique && names.count(names.last()) == 1;
    }
    QStringList labels;
    for (const Fingering& f : mapToInstrument(song_.tracks[static_cast<size_t>(i)].notes, tuning_)) {
        const QString s = unique ? names[f.string] : QString::fromStdString(noteName(tuning_.strings[size_t(f.string)]));
        labels << QString("%1:%2").arg(s).arg(f.fret);
    }
    roll_->setLabels(labels);
}

double MusicTrainingView::songEndSec() const {
    const int i = selectedTrack();
    if (i < 0 || i >= static_cast<int>(song_.tracks.size())) return 0.0;
    double end = 0.0;
    for (const auto& n : song_.tracks[static_cast<size_t>(i)].notes) end = std::max(end, n.start + n.duration);
    return end;
}

void MusicTrainingView::setPaused(bool paused) {
    paused_ = paused && !running_;
    refreshButtons();
}

void MusicTrainingView::setRunning(bool running) {
    running_ = running;
    if (running) paused_ = false;
    refreshButtons();
    start_->setEnabled(running || (startAllowed_ && hasSong()));
    start_->setText(running ? "Parar" : "Iniciar");
    start_->setProperty("running", running);

    // Restyle
    start_->style()->unpolish(start_);
    start_->style()->polish(start_);
    start_->update();

    // Disable controls during playback
    importBtn_->setEnabled(!running);
    trackCombo_->setEnabled(!running && !song_.tracks.empty());
    speedSlider_->setEnabled(!running);
}

void MusicTrainingView::setStartAllowed(bool allowed) {
    startAllowed_ = allowed;
    start_->setEnabled((running_ || startAllowed_) && hasSong());
}

void MusicTrainingView::setTime(double nowSec) {
    roll_->setTime(nowSec);
}

void MusicTrainingView::setNoteResult(int noteIndex, int kind) {
    roll_->setNoteResult(noteIndex, static_cast<NoteResult::Kind>(kind));
}

void MusicTrainingView::showSummary(int hits, int misses, int wrongNotes, int early, int late, int extras, double score) {
    QString text = QString::fromUtf8(
        "🎵 Resultado: %1 acertos • %2 erros • %3 notas erradas • %4 cedo • %5 tarde • %6 extras • Pontuação: %7%")
        .arg(hits).arg(misses).arg(wrongNotes).arg(early).arg(late).arg(extras)
        .arg(score, 0, 'f', 1);
    statsLabel_->setText(text);
}
