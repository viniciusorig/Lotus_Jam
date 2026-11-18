#pragma once
#include <string>

class QSettings;

// Persisted user settings (Qt-dependent: built into lotus_ui, not treino_core).
//
// The app stores them in QSettings("lotus-jam", "lotus-jam"), i.e.
// ~/.config/lotus-jam/lotus-jam.conf on Linux; every function takes the QSettings so tests use
// a temporary INI file. Keys (stable; changing them loses the users' settings):
//   devices/input, devices/output                  PipeWire node names chosen by the user
//                                                  (empty = automatic: device_select's default)
//   training/bpm, training/countInBeats, training/subdivision
//   audio/gainDb
//   tuner/a4, tuner/instrument, tuner/tuning       instrument id, tuning name
//   compensation/<in>|<out>                        latency compensation (ms) of that device pair,
//                                                  node names percent-encoded (RFC 3986)
// Missing, unreadable, mistyped or out-of-range values load as the defaults below (the
// prototype's: 80 BPM, count-in 4, 1 note per beat, gain 0 dB, compensation 0 ms, A4 440 Hz,
// 4-string bass, standard tuning). Saved device nodes are kept as they are, even when the device
// is not present right now (a USB interface unplugged for a while is still remembered);
// the window falls back to device_select's defaults while it is absent.
struct Settings {
    std::string inputNode, outputNode;
    double bpm = 80, compensationMs = 0, gainDb = 0, a4 = 440;
    std::string instrumentId = "baixo4", tuningName = "Padrão";
    int countInBeats = 4;  // 0, 4 or 8
    int subdivision = 1;   // 1, 2 or 4
};

// compensationMs is the compensation of the (inputNode, outputNode) pair.
Settings loadSettings(QSettings& s);
// Writes every field (compensationMs under its device pair, only when both nodes are set).
// Does not sync(): QSettings writes to disk later on its own (or when destroyed); call
// QSettings::sync() for an immediate write.
void saveSettings(QSettings& s, const Settings& v);

// Latency compensation of a device pair; an unknown pair (or an invalid stored value) returns
// the default 0 ms, never another pair's value: the latency depends on both devices.
double compensationFor(QSettings& s, const std::string& in, const std::string& out);
// Stores the pair's compensation (non-finite or |ms| > 1000 is stored as 0).
void setCompensationFor(QSettings& s, const std::string& in, const std::string& out, double ms);
