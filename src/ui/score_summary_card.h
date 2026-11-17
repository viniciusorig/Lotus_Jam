#pragma once
#include <QFrame>

#include "score/grid_scorer.h"

class QLabel;

// Score panel (briefing §5.3): total and last-16 scores (0-100 %), mean error and standard
// deviation (ms) and extra notes, in high-contrast tiles with tabular (monospace) numbers.
// Only setSummary() changes it; the owner refreshes it at <= 10 Hz.
class ScoreSummaryCard : public QFrame {
    Q_OBJECT
public:
    explicit ScoreSummaryCard(QWidget* parent = nullptr);

    void setSummary(const GridScorer::Summary& s);
    void clear();

    const GridScorer::Summary& shown() const { return shown_; }
    QString totalText() const;  // the number shown as the total score (no unit)

private:
    QWidget* tile(const QString& caption, const QString& unit, QLabel** value, bool accent = false);

    GridScorer::Summary shown_{};
    QLabel *total_ = nullptr, *last_ = nullptr, *mean_ = nullptr, *std_ = nullptr, *extras_ = nullptr;
};
