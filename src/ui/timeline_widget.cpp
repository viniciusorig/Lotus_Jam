#include "ui/timeline_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "ui/theme.h"

namespace {
QColor alpha(const QColor& c, double a) {
    QColor r = c;
    r.setAlphaF(float(a));
    return r;
}
constexpr double kLeft = 44.0;  // room for the ms labels
constexpr double kPadY = 10.0;
}  // namespace

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
    bg_ = QBrush(theme::timeline_bg);
    bandExact_ = QBrush(alpha(theme::zone_exact, 0.12));
    bandWarn_ = QBrush(alpha(theme::zone_warn, 0.10));
    bandMiss_ = QBrush(alpha(theme::zone_miss, 0.10));
    dotExact_ = QBrush(theme::zone_exact);
    dotWarn_ = QBrush(theme::zone_warn);
    dotMiss_ = QBrush(theme::zone_miss);
    haloExact_ = QBrush(alpha(theme::zone_exact, 0.25));
    haloWarn_ = QBrush(alpha(theme::zone_warn, 0.25));
    haloMiss_ = QBrush(alpha(theme::zone_miss, 0.25));
    axisPen_ = QPen(theme::text_secondary, 1);
    gridPen_ = QPen(theme::border_subtle, 1, Qt::DotLine);
    linePen_ = QPen(alpha(theme::nana_pink, 0.55), 1.5);
    missPen_ = QPen(theme::zone_miss, 2.2, Qt::SolidLine, Qt::RoundCap);
    labelPen_ = QPen(theme::text_muted);
    framePen_ = QPen(theme::border_subtle, 1);
    labelFont_ = theme::monoFont(10);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void TimelineWidget::push(const Point& p) {
    if (size_ < kCapacity) {
        ring_[size_t((head_ + size_) % kCapacity)] = p;
        ++size_;
    } else {
        ring_[size_t(head_)] = p;
        head_ = (head_ + 1) % kCapacity;
    }
    update();
}

void TimelineWidget::addNote(int64_t slot, double errMs) {
    ++notes_;
    push({slot, std::isfinite(errMs) ? errMs : 0.0, false});
}

void TimelineWidget::addMiss(int64_t slot) {
    ++misses_;
    push({slot, 0.0, true});
}

void TimelineWidget::clear() {
    head_ = size_ = notes_ = misses_ = 0;
    update();
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF all = QRectF(rect());
    p.setPen(Qt::NoPen);
    p.setBrush(bg_);
    p.drawRect(all);

    const QRectF plot(kLeft, kPadY, all.width() - kLeft - 12.0, all.height() - 2 * kPadY);
    const double mid = plot.center().y();
    const double pxPerMs = plot.height() / (2 * kRangeMs);
    auto yFor = [&](double ms) { return mid - std::clamp(ms, -kRangeMs, kRangeMs) * pxPerMs; };  // late = up

    // Zone bands.
    p.setBrush(bandMiss_);
    p.drawRect(QRectF(QPointF(plot.left(), plot.top()), QPointF(plot.right(), plot.bottom())));
    p.setBrush(bg_);
    p.drawRect(QRectF(QPointF(plot.left(), yFor(50)), QPointF(plot.right(), yFor(-50))));
    p.setBrush(bandWarn_);
    p.drawRect(QRectF(QPointF(plot.left(), yFor(50)), QPointF(plot.right(), yFor(-50))));
    p.setBrush(bg_);
    p.drawRect(QRectF(QPointF(plot.left(), yFor(15)), QPointF(plot.right(), yFor(-15))));
    p.setBrush(bandExact_);
    p.drawRect(QRectF(QPointF(plot.left(), yFor(15)), QPointF(plot.right(), yFor(-15))));

    // Grid + labels.
    p.setFont(labelFont_);
    for (double ms : {-120.0, -50.0, -15.0, 15.0, 50.0, 120.0}) {
        const double y = yFor(ms);
        p.setPen(gridPen_);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(labelPen_);
        p.drawText(QRectF(0, y - 7, kLeft - 6, 14), Qt::AlignRight | Qt::AlignVCenter,
                   (ms > 0 ? "+" : "") + QString::number(int(ms)));
    }
    p.setPen(axisPen_);
    p.drawLine(QPointF(plot.left(), mid), QPointF(plot.right(), mid));
    p.setPen(labelPen_);
    p.drawText(QRectF(0, mid - 7, kLeft - 6, 14), Qt::AlignRight | Qt::AlignVCenter, "0 ms");
    p.drawText(QRectF(plot.left() + 6, plot.top() + 2, 120, 14), Qt::AlignLeft | Qt::AlignTop, "atrasou");
    p.drawText(QRectF(plot.left() + 6, plot.bottom() - 16, 120, 14), Qt::AlignLeft | Qt::AlignBottom, "adiantou");

    if (size_ == 0) {
        p.drawText(plot, Qt::AlignCenter, QString::fromUtf8("As últimas 32 notas aparecem aqui"));
        return;
    }

    const double step = plot.width() / double(kCapacity);
    auto xFor = [&](int i) { return plot.right() - step * (double(size_ - 1 - i) + 0.5); };  // newest at the right

    // Connection line between consecutive notes.
    p.setPen(linePen_);
    p.setBrush(Qt::NoBrush);
    bool have = false;
    QPointF prev;
    for (int i = 0; i < size_; ++i) {
        const Point& pt = at(i);
        if (pt.miss) {
            have = false;
            continue;
        }
        const QPointF cur(xFor(i), yFor(pt.errMs));
        if (have) p.drawLine(prev, cur);
        prev = cur;
        have = true;
    }

    // Dots and misses.
    const double r = std::clamp(step * 0.22, 3.0, 5.5);
    for (int i = 0; i < size_; ++i) {
        const Point& pt = at(i);
        const double x = xFor(i);
        if (pt.miss) {
            p.setPen(missPen_);
            const double s = r * 1.1;
            p.drawLine(QPointF(x - s, mid - s), QPointF(x + s, mid + s));
            p.drawLine(QPointF(x - s, mid + s), QPointF(x + s, mid - s));
            continue;
        }
        const double a = std::fabs(pt.errMs);
        const QBrush& dot = a <= 15.0 ? dotExact_ : (a <= 50.0 ? dotWarn_ : dotMiss_);
        const QBrush& halo = a <= 15.0 ? haloExact_ : (a <= 50.0 ? haloWarn_ : haloMiss_);
        const QPointF c(x, yFor(pt.errMs));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(c, r * 2.0, r * 2.0);
        p.setBrush(dot);
        p.drawEllipse(c, r, r);
    }
}
