#include "ui/score_summary_card.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

ScoreSummaryCard::ScoreSummaryCard(QWidget* parent) : QFrame(parent) {
    setObjectName("CardPanel");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(16, 14, 16, 14);
    row->setSpacing(12);
    row->addWidget(tile(QString::fromUtf8("PONTUAÇÃO TOTAL"), "%", &total_, true), 3);
    row->addWidget(tile(QString::fromUtf8("ÚLTIMAS 16 NOTAS"), "%", &last_), 3);
    row->addWidget(tile(QString::fromUtf8("MÉDIA DE ERRO"), "ms", &mean_), 3);
    row->addWidget(tile(QString::fromUtf8("DESVIO PADRÃO"), "ms", &std_), 3);
    row->addWidget(tile("NOTAS EXTRAS", "", &extras_), 2);
    clear();
}

QWidget* ScoreSummaryCard::tile(const QString& caption, const QString& unit, QLabel** value, bool accent) {
    auto* t = new QFrame(this);
    t->setObjectName("StatTile");
    auto* v = new QVBoxLayout(t);
    v->setContentsMargins(14, 10, 14, 10);
    v->setSpacing(2);
    auto* cap = new QLabel(caption, t);
    cap->setProperty("role", "caption");
    v->addWidget(cap);
    auto* line = new QHBoxLayout;
    line->setSpacing(4);
    *value = new QLabel(t);
    (*value)->setProperty("role", "statValue");
    (*value)->setProperty("accent", accent);
    line->addWidget(*value, 0, Qt::AlignBaseline);
    if (!unit.isEmpty()) {
        auto* u = new QLabel(unit, t);
        u->setProperty("role", "statUnit");
        line->addWidget(u, 0, Qt::AlignBaseline);
    }
    line->addStretch(1);
    v->addLayout(line);
    return t;
}

namespace {
// Numbers follow the default QLocale (decimal comma in pt-BR), like the spin boxes.
QString num(double v) { return QLocale().toString(v, 'f', 1); }
QString ms(double v) { return (v > 0.0 ? QString("+") : QString()) + num(v); }
}  // namespace

void ScoreSummaryCard::setSummary(const GridScorer::Summary& s) {
    shown_ = s;
    const bool any = s.slots > 0;
    total_->setText(any ? num(s.total) : QString::fromUtf8("—"));
    last_->setText(any ? num(s.lastN) : QString::fromUtf8("—"));
    mean_->setText(s.hits > 0 ? ms(s.meanErrMs) : QString::fromUtf8("—"));
    std_->setText(s.hits > 0 ? num(s.stdErrMs) : QString::fromUtf8("—"));
    extras_->setText(QString::number(s.extras));
}

void ScoreSummaryCard::clear() { setSummary(GridScorer::Summary{0, 0, 0, 0, 0, 0, 0}); }

QString ScoreSummaryCard::totalText() const { return total_->text(); }
