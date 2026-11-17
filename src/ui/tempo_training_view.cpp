#include "ui/tempo_training_view.h"

#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

#include "ui/pulse_widget.h"
#include "ui/score_summary_card.h"
#include "ui/timeline_widget.h"

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

QFrame* card(QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setObjectName("CardPanel");
    return f;
}

QLabel* cardTitle(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("role", "cardTitle");
    return l;
}

}  // namespace

TempoTrainingView::TempoTrainingView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 14, 0, 0);
    root->setSpacing(14);

    // Session controls.
    auto* controls = card(this);
    auto* ch = new QHBoxLayout(controls);
    ch->setContentsMargins(18, 14, 18, 14);
    ch->setSpacing(16);

    bpm_ = new QSpinBox(controls);
    bpm_->setObjectName("BpmSpin");
    bpm_->setRange(30, 240);
    bpm_->setValue(120);
    bpm_->setSuffix(" BPM");
    bpm_->setMinimumWidth(120);
    bpm_->setAccelerated(true);
    ch->addWidget(field("ANDAMENTO", bpm_, controls));

    countIn_ = new QComboBox(controls);
    countIn_->addItem("Sem contagem", 0);
    countIn_->addItem("1 compasso", 4);
    countIn_->addItem("2 compassos", 8);
    countIn_->setCurrentIndex(1);
    ch->addWidget(field("CONTAGEM", countIn_, controls));

    subdivision_ = new QComboBox(controls);
    subdivision_->addItem("1 por batida", 1);
    subdivision_->addItem("2 por batida", 2);
    subdivision_->addItem("4 por batida", 4);
    ch->addWidget(field(QString::fromUtf8("SUBDIVISÃO"), subdivision_, controls));

    ch->addStretch(1);

    recLabel_ = new QLabel(controls);
    recLabel_->setObjectName("RecIndicator");
    recLabel_->hide();
    ch->addWidget(recLabel_, 0, Qt::AlignBottom);

    record_ = new QPushButton("Gravar amostra (10 s)", controls);
    record_->setEnabled(false);
    record_->setToolTip(QString::fromUtf8("Grava 10 s da entrada em WAV para diagnóstico do sinal."));
    ch->addWidget(record_, 0, Qt::AlignBottom);

    start_ = new QPushButton("Iniciar", controls);
    start_->setObjectName("PrimaryAction");
    start_->setProperty("running", false);
    start_->setMinimumWidth(132);
    start_->setCursor(Qt::PointingHandCursor);
    start_->setEnabled(false);
    start_->setToolTip(QString::fromUtf8("Procurando dispositivos…"));
    ch->addWidget(start_, 0, Qt::AlignBottom);
    root->addWidget(controls);

    // Pulse + timeline.
    auto* mid = new QHBoxLayout;
    mid->setSpacing(14);
    auto* pulseCard = card(this);
    auto* pv = new QVBoxLayout(pulseCard);
    pv->setContentsMargins(16, 12, 16, 12);
    pv->addWidget(cardTitle("PULSO", pulseCard));
    pulse_ = new PulseWidget(pulseCard);
    pv->addWidget(pulse_, 1);
    pulseCard->setMinimumWidth(240);
    mid->addWidget(pulseCard, 0);

    auto* tlCard = card(this);
    auto* tv = new QVBoxLayout(tlCard);
    tv->setContentsMargins(16, 12, 16, 16);
    tv->addWidget(cardTitle(QString::fromUtf8("ÚLTIMAS 32 NOTAS  ·  ERRO (ms)"), tlCard));
    timeline_ = new TimelineWidget(tlCard);
    tv->addWidget(timeline_, 1);
    mid->addWidget(tlCard, 1);
    root->addLayout(mid, 1);

    card_ = new ScoreSummaryCard(this);
    root->addWidget(card_);

    connect(start_, &QPushButton::clicked, this, [this] {
        if (running_)
            Q_EMIT stopRequested();
        else
            Q_EMIT startRequested();
    });
    connect(record_, &QPushButton::clicked, this, &TempoTrainingView::recordRequested);
}

int TempoTrainingView::bpm() const { return bpm_->value(); }
int TempoTrainingView::countInBeats() const { return countIn_->currentData().toInt(); }
int TempoTrainingView::subdivision() const { return subdivision_->currentData().toInt(); }

void TempoTrainingView::restyle(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void TempoTrainingView::setRunning(bool running) {
    running_ = running;
    start_->setEnabled(running || startAllowed_);
    start_->setText(running ? "Parar" : "Iniciar");
    start_->setProperty("running", running);
    restyle(start_);
    record_->setEnabled(running && rec_ != Rec::Recording);
    bpm_->setEnabled(!running);
    countIn_->setEnabled(!running);
    subdivision_->setEnabled(!running);
}

void TempoTrainingView::setStarting(bool starting) {
    if (!starting) return setRunning(running_);
    start_->setEnabled(false);
    start_->setText(QString::fromUtf8("Iniciando…"));
    bpm_->setEnabled(false);
    countIn_->setEnabled(false);
    subdivision_->setEnabled(false);
}

void TempoTrainingView::setStartAllowed(bool allowed) {
    startAllowed_ = allowed;
    start_->setToolTip(allowed ? QString() : QString::fromUtf8("Procurando dispositivos…"));
    if (start_->text() != QString::fromUtf8("Iniciando…")) start_->setEnabled(running_ || startAllowed_);
}

void TempoTrainingView::setRecordingState(Rec state, const QString& path) {
    rec_ = state;
    const QString file = QFileInfo(path).fileName();
    switch (state) {
    case Rec::None:
        recLabel_->clear();
        recLabel_->hide();
        break;
    case Rec::Recording:
        recLabel_->setProperty("state", "recording");
        recLabel_->setText(QString::fromUtf8("● Gravando amostra…"));
        break;
    case Rec::Saved:
        recLabel_->setProperty("state", "done");
        recLabel_->setText(QString::fromUtf8("Amostra salva: %1").arg(file));
        break;
    case Rec::Interrupted:
        recLabel_->setProperty("state", "done");
        recLabel_->setText(QString::fromUtf8("Gravação interrompida"));
        break;
    case Rec::Failed:
        recLabel_->setProperty("state", "recording");
        recLabel_->setText(QString::fromUtf8("Falha ao gravar a amostra"));
        break;
    }
    if (state != Rec::None) {
        recLabel_->setToolTip(path);
        recLabel_->show();
        restyle(recLabel_);
    }
    record_->setEnabled(running_ && state != Rec::Recording);
}
