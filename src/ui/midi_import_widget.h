#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QTextBrowser>
#include "dsp/midi_parser.h"

class MidiImportWidget : public QWidget {
    Q_OBJECT

public:
    explicit MidiImportWidget(QWidget *parent = nullptr);

    // Parse and display a MIDI file
    void loadMidiFile(const std::string& filePath);

    // Get the parsed song
    MidiSong getSong() const { return song_; }

Q_SIGNALS:
    void fileLoaded(const std::string& filePath);

private Q_SLOTS:
    void onLoadFile();
    void onClear();

private:
    MidiSong song_;
    QTextBrowser *logBrowser_;
    QLabel *statusLabel_;

    void displaySongInfo(const MidiSong& song);
};