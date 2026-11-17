#include "ui/pulse_widget.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

#include "ui/theme.h"

namespace {
constexpr double kDecayMs = 220.0;
}

PulseWidget::PulseWidget(QWidget* parent) : QWidget(parent) {
    ringPen_ = QPen(theme::nana_pink, 3);
    idlePen_ = QPen(theme::border_punk, 2);
    labelPen_ = QPen(theme::text_primary);
    countPen_ = QPen(theme::text_secondary);
    idleBrush_ = QBrush(theme::bg_surface);
    glow_ = QRadialGradient(0.5, 0.5, 0.5);
    glow_.setCoordinateMode(QGradient::ObjectMode);
    glow_.setColorAt(0.0, theme::nana_glow);
    glow_.setColorAt(0.55, theme::nana_pink);
    glow_.setColorAt(0.8, QColor(theme::magenta_deep.red(), theme::magenta_deep.green(), theme::magenta_deep.blue(), 90));
    glow_.setColorAt(1.0, QColor(theme::magenta_deep.red(), theme::magenta_deep.green(), theme::magenta_deep.blue(), 0));
    labelFont_ = theme::monoFont(30, true);
    smallFont_ = theme::uiFont(11, QFont::DemiBold);
    anim_.setInterval(16);
    connect(&anim_, &QTimer::timeout, this, [this] {
        if (double(sinceFlash_.elapsed()) > kDecayMs) anim_.stop();
        update();
    });
}

QString PulseWidget::labelFor(int64_t n, int countIn) {
    if (n < countIn) return QString("contagem %1").arg(n + 1);
    return QString::number((n - countIn) % 4 + 1);
}

void PulseWidget::scheduleBeat(int64_t n, double audibleSec, double nowSec, int countIn) {
    const double ms = std::max(0.0, (audibleSec - nowSec) * 1000.0);
    const int delay = std::isfinite(ms) ? int(std::min(ms, 5000.0)) : 0;
    const unsigned gen = generation_;
    QTimer::singleShot(delay, Qt::PreciseTimer, this, [this, n, countIn, gen] { flash(n, countIn, gen); });
}

void PulseWidget::flash(int64_t n, int countIn, unsigned generation) {
    if (generation != generation_) return;
    label_ = labelFor(n, countIn);
    countIn_ = n < countIn;
    if (flashes_ == 0) first_ = label_;
    ++flashes_;
    sinceFlash_.restart();
    if (!anim_.isActive()) anim_.start();
    update();
}

void PulseWidget::idle() {
    ++generation_;
    label_.clear();
    countIn_ = false;
    anim_.stop();
    sinceFlash_.invalidate();
    update();
}

void PulseWidget::reset() {
    flashes_ = 0;
    first_.clear();
    idle();
}

void PulseWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double side = std::min(width(), height() - 28) - 8.0;
    const QRectF circle((width() - side) / 2.0, 4.0, side, side);
    const double inset = side * 0.18;
    const QRectF core = circle.adjusted(inset, inset, -inset, -inset);

    // Idle disc.
    p.setPen(idlePen_);
    p.setBrush(idleBrush_);
    p.drawEllipse(core);

    // Flash: same colour and intensity for every beat.
    const double t = sinceFlash_.isValid() ? double(sinceFlash_.elapsed()) : 1e9;
    const double k = std::clamp(1.0 - t / kDecayMs, 0.0, 1.0);
    if (k > 0.0) {
        p.save();
        p.setOpacity(k);
        p.setPen(Qt::NoPen);
        p.setBrush(glow_);
        p.drawEllipse(circle);
        p.setPen(ringPen_);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(core);
        p.restore();
    }

    // Label: count-in text below the disc, beat number inside.
    if (!label_.isEmpty()) {
        if (countIn_) {
            p.setFont(smallFont_);
            p.setPen(countPen_);
            p.drawText(QRectF(0, circle.bottom() + 2, width(), 22), Qt::AlignCenter, label_.toUpper());
            p.setFont(labelFont_);
            p.setPen(labelPen_);
            p.drawText(core, Qt::AlignCenter, label_.section(' ', 1));
        } else {
            p.setFont(labelFont_);
            p.setPen(labelPen_);
            p.drawText(core, Qt::AlignCenter, label_);
            p.setFont(smallFont_);
            p.setPen(countPen_);
            p.drawText(QRectF(0, circle.bottom() + 2, width(), 22), Qt::AlignCenter, "BATIDA");
        }
    } else {
        p.setFont(smallFont_);
        p.setPen(countPen_);
        p.drawText(QRectF(0, circle.bottom() + 2, width(), 22), Qt::AlignCenter, "PULSO");
    }
}
