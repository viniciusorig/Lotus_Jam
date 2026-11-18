#include "app/settings.h"

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <cmath>

#include "dsp/notes.h"

namespace {

constexpr double kMaxCompensationMs = 1000.0;  // Session rejects |compensation| > 1000 ms

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Number in [lo, hi] (finite), else def. Accepts numbers stored as strings (INI).
double number(QSettings& s, const char* key, double def, double lo, double hi) {
    const QVariant v = s.value(key);
    if (!v.isValid()) return def;
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok && std::isfinite(d) && d >= lo && d <= hi ? d : def;
}

int oneOf(QSettings& s, const char* key, int def, std::initializer_list<int> allowed) {
    const QVariant v = s.value(key);
    if (!v.isValid()) return def;
    bool ok = false;
    const int i = v.toInt(&ok);
    if (!ok) return def;
    for (int a : allowed)
        if (a == i) return i;
    return def;
}

std::string text(QSettings& s, const char* key, const std::string& def) {
    const QVariant v = s.value(key);
    return v.isValid() && v.canConvert<QString>() ? v.toString().toStdString() : def;
}

// "compensation/<in>|<out>" with the node names percent-encoded (no '/', '|', '%' or spaces
// left), so any node name maps to one flat key and the pair cannot be ambiguous.
QString pairKey(const std::string& in, const std::string& out) {
    return "compensation/" + QString::fromLatin1(QByteArray::fromStdString(in).toPercentEncoding()) + "|" +
           QString::fromLatin1(QByteArray::fromStdString(out).toPercentEncoding());
}

double validCompensation(double ms) { return std::isfinite(ms) && std::fabs(ms) <= kMaxCompensationMs ? ms : 0.0; }

}  // namespace

Settings loadSettings(QSettings& s) {
    const Settings def;
    Settings v;
    v.inputNode = text(s, "devices/input", def.inputNode);
    v.outputNode = text(s, "devices/output", def.outputNode);
    v.bpm = number(s, "training/bpm", def.bpm, 30, 240);  // the BPM spin box's range
    v.countInBeats = oneOf(s, "training/countInBeats", def.countInBeats, {0, 4, 8});
    v.subdivision = oneOf(s, "training/subdivision", def.subdivision, {1, 2, 4});
    v.gainDb = number(s, "audio/gainDb", def.gainDb, 0, 24);   // the gain spin box's range
    v.a4 = number(s, "tuner/a4", def.a4, 415, 466);            // the A4 spin box's range

    v.instrumentId = def.instrumentId;
    v.tuningName = def.tuningName;
    const std::string id = text(s, "tuner/instrument", def.instrumentId);
    const std::string tuning = text(s, "tuner/tuning", def.tuningName);
    for (const Instrument& in : instruments()) {
        if (in.id != id) continue;
        v.instrumentId = id;
        for (const Tuning& t : in.tunings)
            if (t.name == tuning) v.tuningName = tuning;
    }
    v.compensationMs = compensationFor(s, v.inputNode, v.outputNode);
    return v;
}

void saveSettings(QSettings& s, const Settings& v) {
    s.setValue("version", 1);
    s.setValue("devices/input", qs(v.inputNode));
    s.setValue("devices/output", qs(v.outputNode));
    s.setValue("training/bpm", v.bpm);
    s.setValue("training/countInBeats", v.countInBeats);
    s.setValue("training/subdivision", v.subdivision);
    s.setValue("audio/gainDb", v.gainDb);
    s.setValue("tuner/a4", v.a4);
    s.setValue("tuner/instrument", qs(v.instrumentId));
    s.setValue("tuner/tuning", qs(v.tuningName));
    if (!v.inputNode.empty() && !v.outputNode.empty())
        setCompensationFor(s, v.inputNode, v.outputNode, v.compensationMs);
}

double compensationFor(QSettings& s, const std::string& in, const std::string& out) {
    const QVariant v = s.value(pairKey(in, out));
    if (!v.isValid()) return 0.0;
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? validCompensation(d) : 0.0;
}

void setCompensationFor(QSettings& s, const std::string& in, const std::string& out, double ms) {
    s.setValue(pairKey(in, out), validCompensation(ms));
}
