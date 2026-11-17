#pragma once
#include <QElapsedTimer>
#include <QFont>
#include <QPen>
#include <QRadialGradient>
#include <QTimer>
#include <QWidget>
#include <cstdint>

// Beat indicator (briefing §5.3): a geometric circle that flashes at the AUDIBLE time of each
// beat, the same colour for every beat (no accent). Labels: "contagem N" during the count-in
// (beat index n < countIn), then the beat number in the 4/4 bar (1..4).
// scheduleBeat() arms a QTimer::singleShot(max(0, (audibleSec - nowSec) * 1000)) owned by this
// widget (Ruling R1: both times on the backend's clock), so destroying the widget cancels it.
class PulseWidget : public QWidget {
    Q_OBJECT
public:
    explicit PulseWidget(QWidget* parent = nullptr);

    void scheduleBeat(int64_t n, double audibleSec, double nowSec, int countIn);
    void reset();  // new session: forgets labels and counters, cancels pending flashes
    void idle();   // session ended: cancels pending flashes and clears the label (keeps counters)

    int flashCount() const { return flashes_; }
    QString label() const { return label_; }
    QString firstLabel() const { return first_; }
    static QString labelFor(int64_t n, int countIn);

    QSize sizeHint() const override { return {260, 200}; }
    QSize minimumSizeHint() const override { return {160, 150}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void flash(int64_t n, int countIn, unsigned generation);

    unsigned generation_ = 0;  // bumped by reset(): stale flashes are ignored
    int flashes_ = 0;
    bool countIn_ = false;
    QString label_, first_;
    QElapsedTimer sinceFlash_;
    QTimer anim_;  // repaints while the flash decays
    QPen ringPen_, idlePen_, labelPen_, countPen_;
    QBrush idleBrush_;
    QRadialGradient glow_;
    QFont labelFont_, smallFont_;
};
