#pragma once
#include <QElapsedTimer>
#include <QFont>
#include <QLinearGradient>
#include <QPen>
#include <QWidget>

// Horizontal input level meter in dBFS, scale -45..0 dB (briefing §5.1). The PCM2902 noise
// floor (~-39 dBFS) is marked so that the weak-signal region is easy to read. Fed with
// UiEvent::Level peaks (0..1, after gain); the bar falls back at 30 dB/s between updates,
// with a 1.5 s peak-hold tick. All painting objects are cached.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    static constexpr double kMinDb = -45.0;
    static constexpr double kMaxDb = 0.0;
    static constexpr double kNoiseFloorDb = -39.0;

    explicit LevelMeter(QWidget* parent = nullptr);

    void setPeak(double peak);  // linear 0..1
    void reset();
    double levelDb() const { return db_; }
    static double toDb(double peak);

    QSize sizeHint() const override { return {220, 34}; }
    QSize minimumSizeHint() const override { return {150, 34}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    double xFor(double db, double w) const;

    double db_ = kMinDb, holdDb_ = kMinDb;
    QElapsedTimer since_, holdSince_;
    QLinearGradient grad_;
    QPen tickPen_, floorPen_, holdPen_, textPen_;
    QBrush trackBrush_;
    QFont tickFont_, valueFont_;
};
