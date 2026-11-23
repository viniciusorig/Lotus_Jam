#include "midi_import_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDebug>

MidiImportWidget::MidiImportWidget(QWidget *parent)
    : QWidget(parent)
    , statusLabel_(new QLabel("Nenhum arquivo carregado", this))
    , logBrowser_(new QTextBrowser(this))
{
    // Layout
    auto* mainLayout = new QVBoxLayout(this);

    // Button row
    auto* btnLayout = new QHBoxLayout();
    auto* loadBtn = new QPushButton("Importar MIDI", this);
    auto* clearBtn = new QPushButton("Limpar", this);

    btnLayout->addStretch(1);
    btnLayout->addWidget(loadBtn);
    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch(1);

    // Log area
    logBrowser_->setMaximumHeight(200);
    logBrowser_->setPlaceholderText("Log de importação aparecerá aqui após carregar um arquivo MIDI...");

    // Status
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setStyleSheet("color: #666; font-style: italic;");

    // Assemble
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(statusLabel_);
    mainLayout->addWidget(logBrowser_);

    // Connect signals
    connect(loadBtn, &QPushButton::clicked, this, &MidiImportWidget::onLoadFile);
    connect(clearBtn, &QPushButton::clicked, this, &MidiImportWidget::onClear);
}

void MidiImportWidget::loadMidiFile(const std::string& filePath) {
    // Parse the MIDI file
    song_ = parseMidiFile(filePath);

    // Display info
    displaySongInfo(song_);

    Q_EMIT fileLoaded(filePath);
}

void MidiImportWidget::onLoadFile() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Importar Arquivo MIDI",
        "",
        "Arquivos MIDI (*.mid *.midi);;Todos os arquivos (*)"
    );

    if (!filePath.isEmpty()) {
        loadMidiFile(filePath.toStdString());
    }
}

void MidiImportWidget::onClear() {
    song_ = MidiSong{};
    logBrowser_->clear();
    statusLabel_->setText("Nenhum arquivo carregado");
    update();  // Trigger repaint if needed
}

void MidiImportWidget::displaySongInfo(const MidiSong& song) {
    logBrowser_->clear();

    if (song.tracks.empty()) {
        logBrowser_->setPlainText("Nenhuma pista encontrada no arquivo MIDI.");
        statusLabel_->setText("Nenhuma pista encontrada");
        return;
    }

    QString info = QStringLiteral("Arquivo MIDI importado:\n");
    info += QStringLiteral("Division: %1 (ticks por quarto)\n").arg(song.division);
    info += QStringLiteral("Pistas: %1\n").arg(song.tracks.size());

    for (size_t t = 0; t < song.tracks.size(); ++t) {
        const auto& track = song.tracks[t];
        info += QStringLiteral("\nPista %1: %2 (%3)\n")
            .arg(t).arg(QString::fromStdString(track.name), QString::fromStdString(track.instrument));
        info += QStringLiteral("  Notas: %1\n").arg(track.notes.size());

        for (size_t i = 0; i < track.notes.size(); ++i) {
            const auto& note = track.notes[i];
            info += QStringLiteral("  Nota %1: MIDI=%2, start=%3s, dur=%4s\n")
                .arg(i).arg(note.midi).arg(note.start, 0, 'f', 3).arg(note.duration, 0, 'f', 3);
        }
    }

    logBrowser_->setPlainText(info);
    statusLabel_->setText("Arquivo carregado com sucesso");
}