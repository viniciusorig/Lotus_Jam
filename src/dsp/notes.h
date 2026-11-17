#pragma once
#include <string>
#include <vector>

double midiToHz(double midi, double a4 = 440.0);
double hzToMidi(double hz, double a4 = 440.0);
std::string noteName(int midi);  // e.g. 40 -> "E2" (C4 = 60)
double centsBetween(double hz, double refHz);

struct Tuning {
    std::string name;
    std::vector<int> strings;  // MIDI, lowest to highest
};

struct Instrument {
    std::string id, label;
    double minHz, maxHz;  // accepted tuner pitch range (covers every string of every tuning)
    std::vector<Tuning> tunings;
};

const std::vector<Instrument>& instruments();

struct StringMatch {
    int stringIndex;
    double targetHz;
    double cents;
};

StringMatch nearestString(const Tuning& tuning, double hz, double a4 = 440.0);
