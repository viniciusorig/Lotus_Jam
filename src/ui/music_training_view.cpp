#include "ui/music_training_view.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

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

    ch->addStretch(1);

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
    if (path.isEmpty()) return;

    filePath_ = path;
    song_ = parseMidiFile(path.toStdString());

    // Update UI
    QFileInfo fi(path);
    fileLabel_->setText(fi.fileName());
    updateTrackCombo();

    if (!song_.tracks.empty()) {
        trackCombo_->setCurrentIndex(0);
        onTrackChanged(0);
        start_->setEnabled(startAllowed_);
        start_->setToolTip(QString::fromUtf8("Iniciar treino com a música"));
        statsLabel_->setText(QString::fromUtf8("Pronto para treinar • %1 notas na trilha selecionada")
                            .arg(song_.tracks[0].notes.size()));
    } else {
        statsLabel_->setText(QString::fromUtf8("Arquivo sem trilhas válidas"));
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
    if (index >= 0 && index < static_cast<int>(song_.tracks.size())) {
        roll_->setSong(song_, index);
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

void MusicTrainingView::setRunning(bool running) {
    running_ = running;
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
