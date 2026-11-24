#include "ui/piano_roll_widget.h"

#include <QPainter>
#include <algorithm>

#include "ui/theme.h"

namespace {
const char* const kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
QString noteLabel(int midi) { return QString("%1%2").arg(kNoteNames[midi % 12]).arg(midi / 12 - 1); }
}  // namespace

PianoRollWidget::PianoRollWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 200);
    setObjectName("PianoRoll");
}

void PianoRollWidget::setSong(const MidiSong& song, int trackIndex) {
    notes_.clear();
    if (trackIndex < 0 || trackIndex >= static_cast<int>(song.tracks.size())) return;

    const auto& track = song.tracks[static_cast<size_t>(trackIndex)];
    notes_.reserve(track.notes.size());

    int lo = 127, hi = 0;
    for (const auto& n : track.notes) {
        DisplayNote dn;
        dn.start = n.start;
        dn.duration = n.duration;
        dn.midi = n.midi;
        dn.hasResult = false;
        notes_.push_back(dn);
        lo = std::min(lo, n.midi);
        hi = std::max(hi, n.midi);
    }

    // Expand range with some margin
    midiLow_ = std::max(0, lo - 2);
    midiHigh_ = std::min(127, hi + 2);

    recalcGeometry();
    update();
}

void PianoRollWidget::setLabels(const QStringList& labels) {
    labels_ = labels;
    update();
}

void PianoRollWidget::clear() {
    notes_.clear();
    now_ = 0.0;
    update();
}

void PianoRollWidget::clearResultsFrom(double sec) {
    for (auto& n : notes_)
        if (n.start >= sec - 1e-9) n.hasResult = false;
    update();
}

void PianoRollWidget::setTime(double nowSec) {
    now_ = nowSec;
    update();
}

void PianoRollWidget::setNoteResult(int noteIndex, NoteResult::Kind kind) {
    if (noteIndex >= 0 && noteIndex < static_cast<int>(notes_.size())) {
        notes_[static_cast<size_t>(noteIndex)].result = kind;
        notes_[static_cast<size_t>(noteIndex)].hasResult = true;
        update();
    }
}

void PianoRollWidget::recalcGeometry() {
    const double totalTime = windowBefore_ + windowAfter_;
    pxPerSec_ = width() / totalTime;
    nowLineX_ = static_cast<int>(windowBefore_ * pxPerSec_);

    const int noteRange = midiHigh_ - midiLow_ + 1;
    pxPerNote_ = static_cast<double>(height()) / noteRange;
}

void PianoRollWidget::resizeEvent(QResizeEvent*) {
    recalcGeometry();
}

QColor PianoRollWidget::colorFor(const DisplayNote& n) const {
    if (!n.hasResult) {
        // Upcoming note - white/light gray
        return QColor(200, 200, 210);
    }
    switch (n.result) {
        case NoteResult::Hit:
            return theme::zone_exact;  // green
        case NoteResult::Early:
        case NoteResult::Late:
            return theme::zone_warn;   // yellow/orange
        case NoteResult::WrongNote:
        case NoteResult::Miss:
            return theme::zone_miss;   // red
        case NoteResult::Extra:
            return QColor(128, 128, 128);  // gray
    }
    return Qt::white;
}

void PianoRollWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(0x0B, 0x0B, 0x0E));

    // Grid lines for each note (subtle)
    p.setPen(QPen(QColor(30, 30, 40), 1));
    for (int midi = midiLow_; midi <= midiHigh_; ++midi) {
        const int y = height() - static_cast<int>((midi - midiLow_ + 0.5) * pxPerNote_);
        p.drawLine(0, y, width(), y);
    }

    // "Now" line
    p.setPen(QPen(theme::nana_pink, 2));
    p.drawLine(nowLineX_, 0, nowLineX_, height());

    // Draw notes
    for (size_t idx = 0; idx < notes_.size(); ++idx) {
        const auto& n = notes_[idx];
        const double relStart = n.start - now_ + windowBefore_;
        const double relEnd = relStart + n.duration;

        // Skip if completely outside view
        if (relEnd < 0 || relStart > windowBefore_ + windowAfter_) continue;

        const int x1 = static_cast<int>(relStart * pxPerSec_);
        const int x2 = static_cast<int>(relEnd * pxPerSec_);
        const int noteY = height() - static_cast<int>((n.midi - midiLow_ + 1) * pxPerNote_);
        const int noteH = std::max(4, static_cast<int>(pxPerNote_ * 0.8));

        QColor fill = colorFor(n);
        QColor border = fill.darker(120);

        // Note rectangle
        QRect nr(x1, noteY, std::max(4, x2 - x1), noteH);
        p.setPen(QPen(border, 1));
        p.setBrush(fill);
        p.drawRoundedRect(nr, 3, 3);

        // Note name on the not-yet-played (white) bars, clipped to the visible part.
        const QRect vis = nr.intersected(rect());
        if (!n.hasResult && noteH >= 9 && vis.width() >= 18) {
            QFont lf = theme::monoFont(9);
            lf.setPixelSize(std::min(noteH - 2, 12));
            p.setFont(lf);
            p.setPen(QColor(0x0B, 0x0B, 0x0E));
            p.drawText(vis, Qt::AlignCenter, idx < size_t(labels_.size()) ? labels_[qsizetype(idx)] : noteLabel(n.midi));
        }
    }

    // Draw MIDI note labels on the left
    p.setPen(theme::text_muted);
    QFont f = theme::monoFont(9);
    p.setFont(f);
    for (int midi = midiLow_; midi <= midiHigh_; midi += 12) {
        const int y = height() - static_cast<int>((midi - midiLow_ + 0.5) * pxPerNote_);
        p.drawText(4, y + 4, noteLabel(midi));
    }
}
