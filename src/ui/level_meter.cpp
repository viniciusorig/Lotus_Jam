#include "ui/level_meter.h"

#include <QPainter>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

#include "ui/theme.h"

namespace {
constexpr double kFallDbPerSec = 30.0;
constexpr int kValueW = 64;  // right-hand numeric readout
constexpr int kBarH = 10;
}  // namespace

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    tickPen_ = QPen(theme::text_muted, 1);
    floorPen_ = QPen(theme::text_secondary, 1, Qt::DashLine);
    holdPen_ = QPen(theme::nana_glow, 2);
    textPen_ = QPen(theme::text_primary);
    trackBrush_ = QBrush(theme::bg_elevated);
    tickFont_ = theme::monoFont(10);
    valueFont_ = theme::monoFont(12, true);
    since_.start();
    holdSince_.start();
    setToolTip(QString::fromUtf8("Nível de entrada (dBFS). Linha tracejada: ruído de fundo típico do PCM2902 (−39 dBFS)."));
}

double LevelMeter::toDb(double peak) {
    if (!(peak > 0.0)) return kMinDb;
    return std::clamp(20.0 * std::log10(peak), kMinDb, kMaxDb);
}

void LevelMeter::setPeak(double peak) {
    const double now = toDb(peak);
    const double fallen = db_ - kFallDbPerSec * double(since_.restart()) / 1000.0;
    const double next = std::max(now, std::max(fallen, kMinDb));
    bool changed = std::fabs(next - db_) > 0.05;
    db_ = next;
    if (now >= holdDb_ || holdSince_.elapsed() > 1500) {
        changed = changed || std::fabs(now - holdDb_) > 0.05;
        holdDb_ = now;
        holdSince_.restart();
    }
    if (changed) update();
}

void LevelMeter::reset() {
    db_ = holdDb_ = kMinDb;
    update();
}

double LevelMeter::xFor(double db, double w) const { return (db - kMinDb) / (kMaxDb - kMinDb) * w; }

void LevelMeter::resizeEvent(QResizeEvent* e) {
    const double w = double(e->size().width() - kValueW - 8);
    grad_ = QLinearGradient(0, 0, w, 0);
    // Weak region (noise floor) muted magenta, useful range pink, hot region amber/red.
    auto at = [&](double db) { return std::clamp(xFor(db, w) / w, 0.0, 1.0); };
    grad_.setColorAt(0.0, theme::magenta_deep);
    grad_.setColorAt(at(kNoiseFloorDb), theme::magenta_deep);
    grad_.setColorAt(at(-30.0), theme::nana_pink);
    grad_.setColorAt(at(-9.0), theme::nana_glow);
    grad_.setColorAt(at(-6.0), theme::zone_warn);
    grad_.setColorAt(1.0, theme::zone_miss);
    QWidget::resizeEvent(e);
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = double(width() - kValueW - 8);
    const double top = 4.0;
    const QRectF track(0, top, w, kBarH);
    p.setPen(Qt::NoPen);
    p.setBrush(trackBrush_);
    p.drawRoundedRect(track, 3, 3);
    const double fill = xFor(db_, w);
    if (fill > 0.5) {
        p.setBrush(grad_);
        p.drawRoundedRect(QRectF(0, top, fill, kBarH), 3, 3);
    }
    // Noise floor marker.
    p.setPen(floorPen_);
    const double xf = xFor(kNoiseFloorDb, w);
    p.drawLine(QPointF(xf, top - 2), QPointF(xf, top + kBarH + 2));
    // Peak hold.
    if (holdDb_ > kMinDb + 0.5) {
        p.setPen(holdPen_);
        const double xh = xFor(holdDb_, w);
        p.drawLine(QPointF(xh, top), QPointF(xh, top + kBarH));
    }
    // Scale.
    p.setFont(tickFont_);
    p.setPen(tickPen_);
    for (double db : {-45.0, -39.0, -30.0, -20.0, -10.0, -3.0, 0.0}) {
        const double x = xFor(db, w);
        p.drawLine(QPointF(x, top + kBarH + 2), QPointF(x, top + kBarH + 4));
        const QString t = QString::number(int(db));
        const QRectF r(x - 14, top + kBarH + 4, 28, 12);
        Qt::Alignment al = Qt::AlignHCenter | Qt::AlignTop;
        if (db == kMinDb) al = Qt::AlignLeft | Qt::AlignTop;
        if (db == kMaxDb) al = Qt::AlignRight | Qt::AlignTop;
        p.drawText(db == kMinDb ? r.translated(14, 0) : (db == kMaxDb ? r.translated(-14, 0) : r), al, t);
    }
    // Numeric readout.
    p.setFont(valueFont_);
    p.setPen(db_ <= kMinDb + 0.01 ? tickPen_ : textPen_);
    const QString v = db_ <= kMinDb + 0.01 ? QString::fromUtf8("−∞ dB") : QString::asprintf("%5.1f dB", db_);
    p.drawText(QRectF(width() - kValueW, 0, kValueW, height()), Qt::AlignRight | Qt::AlignVCenter, v);
}
