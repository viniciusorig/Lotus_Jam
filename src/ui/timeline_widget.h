#pragma once
#include <QFont>
#include <QPen>
#include <QWidget>
#include <array>
#include <cstdint>

// Last 32 scored slots of the tempo training (briefing §5.3): background #0B0B0E, axis at
// 0 ms, vertical scale -120 ms (early, bottom) .. +120 ms (late, top), translucent bands for
// <= 15 ms (green 12%), 15-50 ms (amber 10%) and > 50 ms (red 10%). Notes are luminous dots
// joined by a line; misses are a red X on the axis. All pens/brushes/fonts are cached.
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCapacity = 32;
    static constexpr double kRangeMs = 120.0;

    explicit TimelineWidget(QWidget* parent = nullptr);

    void addNote(int64_t slot, double errMs);
    void addMiss(int64_t slot);
    void clear();

    int noteCount() const { return notes_; }   // Note events since the last clear()
    int missCount() const { return misses_; }  // Miss events since the last clear()
    int pointCount() const { return size_; }   // entries currently drawn (<= 32)

    QSize sizeHint() const override { return {640, 220}; }
    QSize minimumSizeHint() const override { return {320, 160}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Point {
        int64_t slot;
        double errMs;
        bool miss;
    };
    void push(const Point& p);
    const Point& at(int i) const { return ring_[size_t((head_ + i) % kCapacity)]; }  // 0 = oldest

    std::array<Point, kCapacity> ring_{};
    int head_ = 0, size_ = 0;
    int notes_ = 0, misses_ = 0;

    QBrush bg_, bandExact_, bandWarn_, bandMiss_;
    QBrush dotExact_, dotWarn_, dotMiss_, haloExact_, haloWarn_, haloMiss_;
    QPen axisPen_, gridPen_, linePen_, missPen_, labelPen_;
    QFont labelFont_;
};
