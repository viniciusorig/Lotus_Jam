#include "ui/tuner_widget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadialGradient>
#include <QStyle>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "ui/theme.h"

namespace {

constexpr double kRangeCents = 50.0;  // dial: −50…+50 cents
constexpr double kSweepDeg = 60.0;    // ±50 cents -> ±60° from vertical
constexpr double kPulseHz = 1.1;      // glow ring pulse
constexpr double kLedStepCents = 5.0;

QColor withAlpha(QColor c, int a) {
    c.setAlpha(a);
    return c;
}

// Angle from vertical (degrees, clockwise) for a cents value.
double angleOf(double cents) { return std::clamp(cents, -kRangeCents, kRangeCents) / kRangeCents * kSweepDeg; }

// QPainter arc angle (1/16 °, counter-clockwise from 3 o'clock) for an angle from vertical.
int qtAngle(double degFromVertical) { return int(std::lround((90.0 - degFromVertical) * 16.0)); }

QPointF polar(QPointF c, double r, double degFromVertical) {
    const double a = degFromVertical * std::numbers::pi / 180.0;
    return {c.x() + r * std::sin(a), c.y() - r * std::cos(a)};
}

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

QString notesOf(const Tuning& t) {
    QStringList l;
    for (int m : t.strings) l << QString::fromStdString(noteName(m));
    return l.join(' ');
}

}  // namespace

// ---------------------------------------------------------------- dial

// The QPainter dial (briefing §5.2). Geometry, pens, fonts, gradients and the lotus pixmaps
// are built in resizeEvent / the constructor; paintEvent only draws (no allocation-heavy work).
class TunerDial : public QWidget {
public:
    explicit TunerDial(TunerWidget* owner) : QWidget(owner), t_(owner) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMinimumSize(520, 300);
        panelBrush_ = QBrush(theme::timeline_bg);
        panelPen_ = QPen(theme::border_subtle, 1);
        trackPen_ = QPen(QColor(0x17, 0x17, 0x1E), 12, Qt::SolidLine, Qt::FlatCap);
        zonePen_ = QPen(withAlpha(theme::nana_pink, 80), 12, Qt::SolidLine, Qt::FlatCap);
        zoneLitPen_ = QPen(theme::nana_pink, 12, Qt::SolidLine, Qt::FlatCap);
        zoneHaloPen_ = QPen(withAlpha(theme::nana_glow, 70), 26, Qt::SolidLine, Qt::FlatCap);
        ringPen_ = QPen(theme::nana_glow, 3, Qt::SolidLine, Qt::FlatCap);
        majorPen_ = QPen(theme::text_secondary, 2, Qt::SolidLine, Qt::FlatCap);
        minorPen_ = QPen(theme::text_muted, 1.2, Qt::SolidLine, Qt::FlatCap);
        zeroPen_ = QPen(theme::text_primary, 3, Qt::SolidLine, Qt::FlatCap);
        zeroLitPen_ = QPen(theme::nana_pink, 3.5, Qt::SolidLine, Qt::FlatCap);
        labelPen_ = QPen(theme::text_muted);
        zeroLabelPen_ = QPen(theme::text_secondary);
        glyphPen_ = QPen(theme::text_secondary);
        glyphLitPen_ = QPen(theme::nana_pink);
        flatFarPen_ = QPen(theme::magenta_neon);
        sharpFarPen_ = QPen(theme::zone_warn);
        ledOffPen_ = QPen(QColor(0x1C, 0x1C, 0x24), 9, Qt::SolidLine, Qt::FlatCap);
        ledPen_ = QPen(theme::nana_pink, 9, Qt::SolidLine, Qt::FlatCap);
        needlePen_ = QPen(theme::nana_pink, 3.5, Qt::SolidLine, Qt::RoundCap);
        needleDimPen_ = QPen(withAlpha(theme::text_muted, 150), 3, Qt::SolidLine, Qt::RoundCap);
        needleHaloPen_ = QPen(withAlpha(theme::nana_glow, 80), 10, Qt::SolidLine, Qt::RoundCap);
        hubBrush_ = QBrush(theme::bg_surface);
        hubPen_ = QPen(theme::border_subtle, 1.5);
        hubLitPen_ = QPen(theme::nana_pink, 2);
        notePen_ = QPen(theme::text_primary);
        noteLitPen_ = QPen(theme::nana_pink);
        noteDimPen_ = QPen(theme::text_muted);
        readPen_ = QPen(theme::text_secondary);
        noteFont_ = theme::monoFont(64, true);  // 48 pt at 96 dpi
        readFont_ = theme::monoFont(17);
        tickFont_ = theme::monoFont(10);
        glyphFont_ = theme::uiFont(30, QFont::Bold);
        renderer_ = new QSvgRenderer(QString(":/icons/lotus_jam.svg"), this);
    }

    QSize sizeHint() const override { return {820, 400}; }
    QSize minimumSizeHint() const override { return {520, 300}; }

protected:
    void resizeEvent(QResizeEvent*) override { layoutDial(); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), theme::bg_canvas);  // behind the rounded corners
        p.setPen(panelPen_);
        p.setBrush(panelBrush_);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);

        const TunerReading& r = t_->reading_;
        const bool voiced = r.voiced;
        const bool glow = t_->glowActive();
        const double pulse = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * kPulseHz * t_->pulsePhase_);
        const int full = qtAngle(-kSweepDeg), span = qtAngle(kSweepDeg) - full;

        // Track arc and the in-tune zone (±5 cents).
        p.setBrush(Qt::NoBrush);
        p.setPen(trackPen_);
        p.drawArc(arcRect_, full, span);
        const int zStart = qtAngle(-angleOf(TunerWidget::kGlowCents)), zSpan = qtAngle(angleOf(TunerWidget::kGlowCents)) - zStart;
        if (glow) {
            p.setPen(zoneHaloPen_);
            p.drawArc(arcRect_, zStart, zSpan);
        }
        p.setPen(glow ? zoneLitPen_ : zonePen_);
        p.drawArc(arcRect_, zStart, zSpan);

        // Pulsing outer ring (Nana Glow only).
        if (glow) {
            ringPen_.setColor(withAlpha(theme::nana_glow, int(50 + 150 * pulse)));
            p.setPen(ringPen_);
            p.drawArc(ringRect_, full, span);
        }

        // Ticks and labels.
        for (const Tick& k : ticks_) {
            p.setPen(k.zero ? (glow ? zeroLitPen_ : zeroPen_) : (k.major ? majorPen_ : minorPen_));
            p.drawLine(k.line);
        }
        p.setFont(tickFont_);
        for (const Label& l : labels_) {
            p.setPen(l.zero ? zeroLabelPen_ : labelPen_);
            p.drawText(l.rect, Qt::AlignCenter, l.text);
        }

        // LED crown: from the centre towards the reading; pink near the target, dark magenta
        // (flat, tighten) / amber (sharp, loosen) when |cents| > 15.
        const double c = voiced ? std::clamp(r.cents, -kRangeCents, kRangeCents) : 0.0;
        const bool far = voiced && std::fabs(r.cents) > TunerWidget::kFarCents;
        for (const Led& led : leds_) {
            const bool lit = voiced && (led.sign < 0 ? c <= led.from : c >= led.from) && std::fabs(c) > 0.5;
            if (lit) {
                ledPen_.setColor(far ? (led.sign < 0 ? theme::magenta_deep : theme::zone_warn) : theme::nana_pink);
                p.setPen(ledPen_);
            } else {
                p.setPen(ledOffPen_);
            }
            p.drawArc(ledRect_, led.start, led.span);
        }
        p.setFont(glyphFont_);
        const bool flat = voiced && r.cents < -TunerWidget::kGlowCents;
        const bool sharp = voiced && r.cents > TunerWidget::kGlowCents;
        p.setPen(flat ? (far ? flatFarPen_ : glyphLitPen_) : glyphPen_);
        p.drawText(flatRect_, Qt::AlignCenter, flatText_);
        p.setPen(sharp ? (far ? sharpFarPen_ : glyphLitPen_) : glyphPen_);
        p.drawText(sharpRect_, Qt::AlignCenter, sharpText_);

        // Hub halo (behind needle and hub).
        if (glow) {
            p.save();
            p.setOpacity(0.55 + 0.45 * pulse);
            p.setPen(Qt::NoPen);
            p.setBrush(haloBrush_);
            p.drawEllipse(haloRect_);
            p.restore();
        }

        // Needle.
        const QPointF tip = polar(centre_, needleLen_, angleOf(t_->shown_));
        const QPointF base = polar(centre_, hubR_ + 2, angleOf(t_->shown_));
        if (glow) {
            p.setPen(needleHaloPen_);
            p.drawLine(base, tip);
        }
        p.setPen(voiced ? needlePen_ : needleDimPen_);
        p.drawLine(base, tip);

        // Hub with the lotus.
        p.setPen(glow ? hubLitPen_ : hubPen_);
        p.setBrush(hubBrush_);
        p.drawEllipse(centre_, hubR_, hubR_);
        const QPixmap& lotus = glow ? lotusLit_ : lotusDim_;
        if (!lotus.isNull()) p.drawPixmap(lotusRect_.topLeft(), lotus);

        // Note and readout.
        p.setFont(noteFont_);
        if (voiced) {
            p.setPen(glow ? noteLitPen_ : notePen_);
            p.drawText(noteRect_, Qt::AlignHCenter | Qt::AlignVCenter, t_->note_);
        } else {
            p.setPen(noteDimPen_);
            p.drawText(noteRect_, Qt::AlignHCenter | Qt::AlignVCenter, dashText_);
        }
        p.setFont(readFont_);
        p.setPen(voiced ? readPen_ : noteDimPen_);
        p.drawText(readRect_, Qt::AlignHCenter | Qt::AlignVCenter, t_->status_);
    }

private:
    struct Tick {
        QLineF line;
        bool major, zero;
    };
    struct Label {
        QRectF rect;
        QString text;
        bool zero;
    };
    struct Led {
        int start, span;  // QPainter arc
        double from;      // lit when the reading passes this cents value (towards its side)
        int sign;
    };

    void layoutDial() {
        const double W = width(), H = height();
        constexpr double pad = 8, labelRoom = 26, textBlock = 96, hubGap = 4;
        // Vertical: labels + R + hub + note/readout; horizontal: the labels at ±60°.
        const double rByH = (H - 2 * pad - labelRoom - textBlock - hubGap) / 1.17;
        const double rByW = (W / 2 - pad - 34) / std::sin(kSweepDeg * std::numbers::pi / 180.0) - 30;
        R_ = std::clamp(std::min(rByH, rByW), 70.0, 330.0);
        hubR_ = std::max(20.0, 0.17 * R_);
        const double total = labelRoom + R_ + hubR_ + hubGap + textBlock;
        const double top = std::max(pad, (H - total) / 2.0);
        centre_ = QPointF(W / 2.0, top + labelRoom + R_);
        arcRect_ = QRectF(centre_.x() - R_, centre_.y() - R_, 2 * R_, 2 * R_);
        const double ringR = R_ + 12;
        ringRect_ = QRectF(centre_.x() - ringR, centre_.y() - ringR, 2 * ringR, 2 * ringR);
        const double ledR = R_ - 46;
        ledRect_ = QRectF(centre_.x() - ledR, centre_.y() - ledR, 2 * ledR, 2 * ledR);
        needleLen_ = R_ - 2;

        ticks_.clear();
        labels_.clear();
        const QFontMetricsF fm(tickFont_);
        for (int cents = -50; cents <= 50; cents += 5) {
            const bool zero = cents == 0, major = cents % 10 == 0;
            const double len = zero ? 26 : (major ? 16 : 9);
            const double a = angleOf(cents);
            const double r0 = R_ - 8;
            ticks_.push_back({QLineF(polar(centre_, r0, a), polar(centre_, r0 - len, a)), major, zero});
            if (major) {
                const QString text = cents > 0 ? QString("+%1").arg(cents) : QString::number(cents);
                const QPointF at = polar(centre_, R_ + 24, a);
                const double w = fm.horizontalAdvance(text) + 4, h = fm.height();
                labels_.push_back({QRectF(at.x() - w / 2, at.y() - h / 2, w, h), text, zero});
            }
        }

        leds_.clear();
        const double gapDeg = 1.4;
        for (int side : {-1, 1})
            for (int i = 0; i < int(kRangeCents / kLedStepCents); ++i) {
                const double c0 = side * i * kLedStepCents, c1 = side * (i + 1) * kLedStepCents;
                double a0 = angleOf(c0), a1 = angleOf(c1);
                if (side > 0) {
                    a0 += gapDeg / 2;
                    a1 -= gapDeg / 2;
                } else {
                    a0 -= gapDeg / 2;
                    a1 += gapDeg / 2;
                }
                const int s = qtAngle(a0);
                leds_.push_back({s, qtAngle(a1) - s, c0 + side * 0.5, side});
            }
        const QPointF fl = polar(centre_, ledR, -kSweepDeg - 9), sh = polar(centre_, ledR, kSweepDeg + 9);
        flatRect_ = QRectF(fl.x() - 20, fl.y() - 22, 40, 44);
        sharpRect_ = QRectF(sh.x() - 20, sh.y() - 22, 40, 44);

        const double haloR = hubR_ * 2.6;
        haloRect_ = QRectF(centre_.x() - haloR, centre_.y() - haloR, 2 * haloR, 2 * haloR);
        QRadialGradient g(centre_, haloR);
        g.setColorAt(0.0, withAlpha(theme::nana_glow, 170));
        g.setColorAt(0.45, withAlpha(theme::nana_pink, 70));
        g.setColorAt(1.0, withAlpha(theme::nana_pink, 0));
        haloBrush_ = QBrush(g);

        const double y = centre_.y() + hubR_ + hubGap;
        noteRect_ = QRectF(0, y, W, 70);
        readRect_ = QRectF(0, y + 70, W, 26);

        renderLotus();
    }

    // Renders the SVG once per hub size: the original colours (lit) and a dimmed grey copy.
    void renderLotus() {
        const double side = std::round(hubR_ * 1.5);
        const qreal dpr = devicePixelRatioF();
        const QSize px(int(side * dpr), int(side * dpr));
        lotusRect_ = QRectF(centre_.x() - side / 2, centre_.y() - side / 2, side, side);
        if (px == lotusPx_ && !lotusLit_.isNull()) return;
        lotusPx_ = px;
        if (px.isEmpty() || !renderer_->isValid()) return;
        QImage img(px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter ip(&img);
            ip.setRenderHint(QPainter::Antialiasing);
            renderer_->render(&ip);
        }
        QImage dim = img.convertToFormat(QImage::Format_ARGB32);
        for (int yy = 0; yy < dim.height(); ++yy) {
            auto* line = reinterpret_cast<QRgb*>(dim.scanLine(yy));
            for (int xx = 0; xx < dim.width(); ++xx) {
                const QRgb c = line[xx];
                const int g = qGray(c) / 2 + 20;
                line[xx] = qRgba(g, g, g + 6, qAlpha(c) * 3 / 4);
            }
        }
        lotusLit_ = QPixmap::fromImage(img);
        lotusLit_.setDevicePixelRatio(dpr);
        lotusDim_ = QPixmap::fromImage(dim);
        lotusDim_.setDevicePixelRatio(dpr);
    }

    TunerWidget* t_;
    QSvgRenderer* renderer_ = nullptr;
    QPointF centre_;
    double R_ = 100, hubR_ = 20, needleLen_ = 90;
    QRectF arcRect_, ringRect_, ledRect_, haloRect_, lotusRect_, noteRect_, readRect_, flatRect_, sharpRect_;
    std::vector<Tick> ticks_;
    std::vector<Label> labels_;
    std::vector<Led> leds_;
    QSize lotusPx_;
    QPixmap lotusLit_, lotusDim_;
    QBrush panelBrush_, hubBrush_, haloBrush_;
    QPen panelPen_, trackPen_, zonePen_, zoneLitPen_, zoneHaloPen_, ringPen_, majorPen_, minorPen_, zeroPen_,
        zeroLitPen_, labelPen_, zeroLabelPen_, glyphPen_, glyphLitPen_, flatFarPen_, sharpFarPen_, ledOffPen_, ledPen_, needlePen_, needleDimPen_, needleHaloPen_,
        hubPen_, hubLitPen_, notePen_, noteLitPen_, noteDimPen_, readPen_;
    QFont noteFont_, readFont_, tickFont_, glyphFont_;
    const QString flatText_ = QString::fromUtf8("♭"), sharpText_ = QString::fromUtf8("♯"),
                  dashText_ = QString::fromUtf8("—");
};

// ---------------------------------------------------------------- string strip

// One indicator per string of the current tuning (lowest first); the string of the current
// reading in pink with a glow.
class StringStrip : public QWidget {
public:
    explicit StringStrip(TunerWidget* owner) : QWidget(owner), t_(owner) {
        setObjectName("TunerStrip");  // transparent in the QSS: the card shows behind it
        setFixedHeight(52);
        offPen_ = QPen(theme::border_subtle, 1.2);
        onPen_ = QPen(theme::nana_pink, 2);
        offBrush_ = QBrush(theme::bg_elevated);
        onBrush_ = QBrush(QColor(0x2A, 0x10, 0x1E));
        haloPen_ = QPen(withAlpha(theme::nana_glow, 70), 7);
        textPen_ = QPen(theme::text_secondary);
        textOnPen_ = QPen(theme::nana_pink);
        numPen_ = QPen(theme::text_muted);
        font_ = theme::monoFont(17, true);
        numFont_ = theme::monoFont(10);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QStringList& labels = t_->labels_;  // cached per tuning change
        const int n = int(labels.size());
        if (n == 0) return;
        const bool chromatic = n == 12;
        const double w = chromatic ? 56 : 84, h = 42, gap = chromatic ? 6 : 14;
        const double total = n * w + (n - 1) * gap;
        double x = (width() - total) / 2.0;
        const double y = (height() - h) / 2.0;
        const int lit = t_->highlightedString();
        for (int i = 0; i < n; ++i, x += w + gap) {
            const QRectF r(x, y, w, h);
            const bool on = lit == i + 1;
            if (on) {
                p.setPen(haloPen_);
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 12, 12);
            }
            p.setPen(on ? onPen_ : offPen_);
            p.setBrush(on ? onBrush_ : offBrush_);
            p.drawRoundedRect(r, 10, 10);
            if (!chromatic) {
                p.setFont(numFont_);
                p.setPen(numPen_);
                p.drawText(r.adjusted(8, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, QString::number(i + 1));
            }
            p.setFont(font_);
            p.setPen(on ? textOnPen_ : textPen_);
            p.drawText(r, Qt::AlignCenter, labels[i]);
        }
    }

private:
    TunerWidget* t_;
    QPen offPen_, onPen_, haloPen_, textPen_, textOnPen_, numPen_;
    QBrush offBrush_, onBrush_;
    QFont font_, numFont_;
};

// ---------------------------------------------------------------- TunerWidget

TunerWidget::TunerWidget(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 14, 0, 0);
    root->setSpacing(14);

    auto* controls = new QFrame(this);
    controls->setObjectName("CardPanel");
    auto* ch = new QHBoxLayout(controls);
    ch->setContentsMargins(18, 14, 18, 14);
    ch->setSpacing(16);

    instrument_ = new QComboBox(controls);
    for (const Instrument& in : instruments())
        instrument_->addItem(QString::fromStdString(in.label), QString::fromStdString(in.id));
    instrument_->setToolTip(QString::fromUtf8("Instrumento a afinar (troca as cordas e as afinações disponíveis)"));
    instrument_->setMinimumWidth(170);
    ch->addWidget(field("INSTRUMENTO", instrument_, controls));

    tuningCombo_ = new QComboBox(controls);
    tuningCombo_->setMinimumWidth(170);
    ch->addWidget(field(QString::fromUtf8("AFINAÇÃO"), tuningCombo_, controls));

    a4_ = new QDoubleSpinBox(controls);
    a4_->setRange(415.0, 466.0);
    a4_->setSingleStep(0.5);
    a4_->setDecimals(1);
    a4_->setValue(440.0);
    a4_->setSuffix(" Hz");
    a4_->setMinimumWidth(130);
    a4_->setToolTip(QString::fromUtf8("Frequência de referência do Lá (A4); reinicia o afinador"));
    ch->addWidget(field(QString::fromUtf8("REFERÊNCIA (A4)"), a4_, controls));

    ch->addStretch(1);
    start_ = new QPushButton("Iniciar", controls);
    start_->setObjectName("PrimaryAction");
    start_->setProperty("running", false);
    start_->setMinimumWidth(132);
    start_->setCursor(Qt::PointingHandCursor);
    start_->setEnabled(false);
    start_->setToolTip(QString::fromUtf8("Procurando dispositivos…"));
    ch->addWidget(start_, 0, Qt::AlignBottom);
    root->addWidget(controls);

    dial_ = new TunerDial(this);
    root->addWidget(dial_, 1);

    auto* stripCard = new QFrame(this);
    stripCard->setObjectName("CardPanel");
    auto* sv = new QVBoxLayout(stripCard);
    sv->setContentsMargins(16, 8, 16, 6);
    sv->setSpacing(2);
    auto* title = new QLabel("CORDAS", stripCard);
    title->setProperty("role", "cardTitle");
    sv->addWidget(title);
    strip_ = new StringStrip(this);
    strip_->setParent(stripCard);
    sv->addWidget(strip_);
    root->addWidget(stripCard);

    rebuildTunings();
    refreshViews();

    anim_.setInterval(16);  // <= 60 Hz, only while the pointer moves or the glow pulses
    connect(&anim_, &QTimer::timeout, this, [this] {
        const double dt = std::clamp(double(frame_.restart()) / 1000.0, 0.0, 0.1);
        advance(dt);
    });
    connect(instrument_, &QComboBox::currentIndexChanged, this, [this] {
        rebuildTunings();
        clearReading();
        refreshViews();
        Q_EMIT configChanged();
    });
    connect(tuningCombo_, &QComboBox::currentIndexChanged, this, [this] {
        clearReading();
        refreshViews();
        Q_EMIT configChanged();
    });
    connect(a4_, &QDoubleSpinBox::valueChanged, this, [this] { Q_EMIT configChanged(); });
    connect(start_, &QPushButton::clicked, this, [this] {
        if (running_)
            Q_EMIT stopRequested();
        else
            Q_EMIT startRequested();
    });
}

TunerWidget::~TunerWidget() = default;

const Instrument& TunerWidget::instrument() const {
    const std::string id = instrument_->currentData().toString().toStdString();
    for (const Instrument& in : instruments())
        if (in.id == id) return in;
    return instruments().front();
}

Tuning TunerWidget::tuning() const {
    const Instrument& in = instrument();
    const int i = tuningCombo_->currentIndex();
    return i >= 0 && i < int(in.tunings.size()) ? in.tunings[size_t(i)] : in.tunings.front();
}

double TunerWidget::a4() const { return a4_->value(); }

void TunerWidget::rebuildTunings() {
    const Instrument& in = instrument();
    const QString keep = tuningCombo_->currentText();
    const QSignalBlocker block(tuningCombo_);
    tuningCombo_->clear();
    int sel = 0;
    for (size_t i = 0; i < in.tunings.size(); ++i) {
        const QString name = QString::fromStdString(in.tunings[i].name);
        tuningCombo_->addItem(name);
        tuningCombo_->setItemData(int(i), notesOf(in.tunings[i]), Qt::ToolTipRole);
        if (name == keep) sel = int(i);
    }
    tuningCombo_->setCurrentIndex(sel);
    tuningCombo_->setToolTip(QString::fromUtf8("Afinação (cordas: %1)").arg(notesOf(tuning())));
}

void TunerWidget::refreshViews() {
    labels_.clear();
    const Tuning t = tuning();
    if (t.strings.empty()) {
        // Chromatic mode: show all 12 pitch classes
        for (const char* n : {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"})
            labels_ << QString::fromUtf8(n);
    } else {
        for (int m : t.strings) labels_ << QString::fromStdString(noteName(m));
    }
    tuningCombo_->setToolTip(QString::fromUtf8("Afinação (cordas: %1)").arg(notesOf(t)));
    dial_->update();
    strip_->update();
}

double TunerWidget::smoothStep(double shown, double target, double dtSec) {
    if (!(dtSec > 0.0)) return shown;
    return shown + (target - shown) * (1.0 - std::exp(-dtSec / kPointerTauSec));
}

void TunerWidget::setReading(const TunerReading& in) {
    TunerReading r = in;
    const int strings = int(labels_.size());
    const bool chromatic = r.voiced && r.midi >= 0 && tuning().strings.empty();
    if (!r.voiced || !std::isfinite(r.cents) ||
        (!chromatic && (r.stringIndex < 0 || r.stringIndex >= strings)))
        r = TunerReading{false, -1, 0, 0, 0};
    reading_ = r;
    updateTexts();
    ++readings_;
    ensureAnimating();
    dial_->update();
    strip_->update();
    Q_EMIT readingApplied(r);
}

void TunerWidget::clearReading() {
    reading_ = TunerReading{false, -1, 0, 0, 0};
    updateTexts();
    ensureAnimating();
    dial_->update();
    strip_->update();
}

void TunerWidget::ensureAnimating() {
    if (anim_.isActive()) return;
    frame_.restart();
    anim_.start();
}

void TunerWidget::advance(double dtSec) {
    const double target = reading_.voiced ? std::clamp(reading_.cents, -kRangeCents, kRangeCents) : 0.0;
    shown_ = smoothStep(shown_, target, dtSec);
    const bool glow = glowActive();
    if (glow) {
        pulsePhase_ = std::fmod(pulsePhase_ + dtSec, 1000.0);
    } else {
        pulsePhase_ = 0.0;
    }
    if (std::fabs(shown_ - target) < 0.02) {
        shown_ = target;
        if (!glow) anim_.stop();
    }
    dial_->update();
}

void TunerWidget::restyle(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void TunerWidget::setRunning(bool running) {
    running_ = running;
    start_->setEnabled(running || startAllowed_);
    start_->setText(running ? "Parar" : "Iniciar");
    start_->setToolTip(running ? QString("Parar o afinador") : QString("Iniciar o afinador"));
    start_->setProperty("running", running);
    restyle(start_);
}

void TunerWidget::setStarting(bool starting) {
    if (!starting) return setRunning(running_);
    start_->setEnabled(false);
    start_->setText(QString::fromUtf8("Iniciando…"));
}

void TunerWidget::setStartAllowed(bool allowed) {
    startAllowed_ = allowed;
    if (start_->text() == QString::fromUtf8("Iniciando…")) return;
    start_->setEnabled(running_ || startAllowed_);
    if (!running_)
        start_->setToolTip(allowed ? QString("Iniciar o afinador") : QString::fromUtf8("Procurando dispositivos…"));
}

// Note and readout are formatted once per reading (the dial repaints up to 60 Hz).
void TunerWidget::updateTexts() {
    note_.clear();
    status_ = QString("Sem sinal");
    if (!reading_.voiced) return;
    if (reading_.midi >= 0)
        note_ = QString::fromStdString(noteName(reading_.midi));
    else if (reading_.stringIndex >= 0 && reading_.stringIndex < int(labels_.size()))
        note_ = labels_[reading_.stringIndex];
    const QLocale loc;  // pt-BR: "82,41 Hz  •  +0,4 cents"
    double c = std::round(reading_.cents * 10.0) / 10.0;
    if (c == 0.0) c = 0.0;  // no "-0,0"
    const QString cents = (c >= 0.0 ? QString("+") : QString()) + loc.toString(c, 'f', 1);
    status_ = loc.toString(reading_.hz, 'f', 2) + QString::fromUtf8(" Hz  •  ") + cents + " cents";
}

int TunerWidget::highlightedString() const {
    if (!reading_.voiced) return 0;
    if (reading_.midi >= 0) return ((reading_.midi % 12) + 12) % 12 + 1;  // chromatic: 1-12 (C=1)
    return reading_.stringIndex + 1;
}

bool TunerWidget::glowActive() const { return reading_.voiced && std::fabs(reading_.cents) <= kGlowCents; }


QStringList TunerWidget::stringLabels() const { return labels_; }
