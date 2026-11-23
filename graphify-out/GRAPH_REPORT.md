# Graph Report - treino  (2026-11-23)

## Corpus Check
- 80 files · ~69,450 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 5 file(s) not represented in the graph (top: (none) 2, .desktop 1, .qrc 1)

## Summary
- 2003 nodes · 3456 edges · 123 communities (93 shown, 30 thin omitted)
- Extraction: 90% EXTRACTED · 10% INFERRED · 0% AMBIGUOUS · INFERRED: 356 edges (avg confidence: 0.87)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `ca80bf82`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- MainWindow
- TunerDial
- TunerWidget
- GridScorer
- theme.h
- TEST
- TempoTrainingView
- TimelineWidget
- Summary
- Session
- PulseWidget
- TEST
- FakeFactory
- ClockMap
- UiEvent
- FakeBackend
- Tuner
- Conn
- .startSession
- main_window.cpp
- OnsetDetector
- tuner_widget.cpp
- ui_smoke_test.cpp
- impl_
- LevelMeter
- cmath
- Briefing de Design, UI/UX e Arquitetura Frontend — **Lotus Jam**
- session.cpp
- Instrument
- PipeWireBackend
- SpscRing
- StringStrip
- TEST
- IAudioBackend
- ClickScheduler
- settings.cpp
- Yin
- TEST
- onset_test.cpp
- TEST
- pipewire_backend.cpp
- MusicTrainingView
- level_meter.cpp
- TEST
- session_test.cpp
- TEST_P
- TunerReading
- fake_backend.cpp
- .paintEvent
- InputTimeline
- MainWindow::buildUi
- TEST
- Lotus Jam — Parte 1: núcleo, afinador e treino de tempo — Plano de implementação
- MidiImportWidget
- Treino — metrônomo, afinador e treinador de músicas para baixo/guitarra/violão (C++)
- main_window.h
- Options
- FloorParam
- PianoRollWidget
- timeline_widget.cpp
- Lotus Jam
- SessionConfig
- AudioDevice
- tempo_training_view.cpp
- pulse_widget.cpp
- Feed
- music_training_view.cpp
- Point
- HumParam
- Task 8: Backend abstrato, `FakeBackend` e `Session` (threads)
- listOutputDevices
- Rec
- Rng
- TakeEnv
- NoteScorer
- click_scheduler_rt_test.cpp
- TickWatch
- tuner.cpp
- waitTuner
- Led
- Rand
- TEST
- .startSession
- MidiSong
- midi_parser.cpp
- Tick
- TunerWidget::TunerWidget
- Summary
- DeviceLists
- TEST
- StreamResult
- LowParam
- TEST
- atomic
- note_scorer.cpp
- piano_roll_widget.cpp
- vector
- Obs
- MidiNote
- DropReq
- DropWin
- NoteResult
- Noise
- DisplayNote
- LowFloorPlaying
- Kind
- settings_test.cpp
- Failure
- .setRunning
- Segment
- DetectedNote
- selectInstrument

## God Nodes (most connected - your core abstractions)
1. `MainWindow` - 155 edges
2. `TunerDial` - 85 edges
3. `TunerWidget` - 81 edges
4. `Session` - 79 edges
5. `FakeBackend` - 72 edges
6. `TEST()` - 63 edges
7. `TempoTrainingView` - 57 edges
8. `MusicTrainingView` - 48 edges
9. `TimelineWidget` - 44 edges
10. `TEST()` - 42 edges

## Surprising Connections (you probably didn't know these)
- `Task 5: `OnsetDetector`` --references--> `OnsetDetector`  [INFERRED]
  docs/superpowers/plans/2026-11-17-parte1-nucleo-afinador-tempo.md → src/dsp/onset.h
- `7. Checklist de Aceitação e Testes Automatizados (`ui_smoke_test.cpp`)` --references--> `FakeBackend`  [INFERRED]
  docs/front/briefing_de_ui_ux_e_arquitetura_frontend_lotus_jam.md → src/audio/fake_backend.h
- `Arquitetura` --references--> `FakeBackend`  [INFERRED]
  README.md → src/audio/fake_backend.h
- `Testes` --references--> `FakeBackend`  [INFERRED]
  README.md → src/audio/fake_backend.h
- `Task 6: `GridScorer`` --references--> `GridScorer`  [INFERRED]
  docs/superpowers/plans/2026-11-17-parte1-nucleo-afinador-tempo.md → src/score/grid_scorer.h

## Import Cycles
- None detected.

## Communities (123 total, 30 thin omitted)

### Community 0 - "MainWindow"
Cohesion: 0.02
Nodes (69): main(), LevelMeter, MainWindow, applying_, backend_, badge_, comp_, countIn_ (+61 more)

### Community 1 - "TunerDial"
Cohesion: 0.03
Nodes (59): TunerDial, arcRect_, centre_, dashText_, flatFarPen_, flatRect_, flatText_, glyphFont_ (+51 more)

### Community 2 - "TunerWidget"
Cohesion: 0.05
Nodes (25): MainWindow::applySettings(), StringStrip, TunerDial, QComboBox, QDoubleSpinBox, QPushButton, TunerWidget, anim_ (+17 more)

### Community 3 - "GridScorer"
Cohesion: 0.05
Nodes (24): Session::refreshSummary(), GridScorer, comp_, done_, extras_, nextFinal_, pending_, pendingSlot_ (+16 more)

### Community 4 - "theme.h"
Cohesion: 0.21
Nodes (8): appIcon(), apply(), hex(), initLotusResources(), initResources(), monoFont(), QApplication, uiFont()

### Community 6 - "TempoTrainingView"
Cohesion: 0.07
Nodes (17): PulseWidget, QLabel, QPushButton, QSpinBox, ScoreSummaryCard, TempoTrainingView, calibLabel_, calibrate_ (+9 more)

### Community 7 - "TimelineWidget"
Cohesion: 0.05
Nodes (24): TimelineWidget, axisPen_, bandExact_, bandMiss_, bandWarn_, bg_, dotExact_, dotMiss_ (+16 more)

### Community 8 - "Summary"
Cohesion: 0.08
Nodes (21): Summary, extras, hits, lastN, meanErrMs, slots, stdErrMs, total (+13 more)

### Community 9 - "Session"
Cohesion: 0.05
Nodes (34): Session::summary(), Session, active_, anaEvents_, analysedFrames_, analysis_, analysisDone_, backend_ (+26 more)

### Community 10 - "PulseWidget"
Cohesion: 0.07
Nodes (17): PulseWidget::reset(), PulseWidget, anim_, countIn_, countPen_, first_, flashes_, generation_ (+9 more)

### Community 11 - "TEST"
Cohesion: 0.07
Nodes (5): instrument(), jitterMs(), runSyntheticPlayer(), TEST(), training()

### Community 12 - "FakeFactory"
Cohesion: 0.07
Nodes (25): FakeFactory, bpm, calls, countInBeats, failStart, in, last, notes (+17 more)

### Community 13 - "ClockMap"
Cohesion: 0.09
Nodes (15): Task 2: `ClockMap` (amostra → tempo), ClockMap, a_, b_, f0_, fitted_, haveOrigin_, kMinPoints (+7 more)

### Community 14 - "UiEvent"
Cohesion: 0.07
Nodes (25): UiEvent, a, b, n, noteKind, text, tuner, Type (+17 more)

### Community 15 - "FakeBackend"
Cohesion: 0.07
Nodes (24): FakeBackend, captured_, dropCount_, dropped_, in_, inTimes_, kBlock, kMaxDrops (+16 more)

### Community 16 - "Tuner"
Cohesion: 0.07
Nodes (26): Tuner, a4_, candidateHz_, chromatic_, confirmed_, filled_, history_, historyCount_ (+18 more)

### Community 17 - "Conn"
Cohesion: 0.08
Nodes (15): Conn, context, core, coreError, coreListener, failure, loop, nodes (+7 more)

### Community 18 - ".startSession"
Cohesion: 0.15
Nodes (8): MainWindow::onStartFinished(), MainWindow::poll(), MainWindow::recordSample(), Engine, Failed, Idle, Running, Starting

### Community 19 - "main_window.cpp"
Cohesion: 0.06
Nodes (19): BackendFactory, MainWindow::calibrate(), MainWindow::engineBadgeTexts(), MainWindow::flushSettings(), MainWindow::loadPairCompensation(), MainWindow::onTunerConfigChanged(), MainWindow::refreshDevices(), MainWindow::scheduleSave() (+11 more)

### Community 20 - "OnsetDetector"
Cohesion: 0.07
Nodes (28): OnsetDetector::updateThreshold(), OnsetDetector, anchor_, db_, dc_, dcAlpha_, dcInit_, env_ (+20 more)

### Community 21 - "tuner_widget.cpp"
Cohesion: 0.16
Nodes (9): TunerWidget::a4(), TunerWidget::clearReading(), TunerWidget::glowActive(), TunerWidget::highlightedString(), TunerWidget::rebuildTunings(), TunerWidget::refreshViews(), TunerWidget::stringLabels(), TunerWidget::tuning() (+1 more)

### Community 22 - "ui_smoke_test.cpp"
Cohesion: 0.13
Nodes (3): currentNode(), litPixels(), settle()

### Community 23 - "impl_"
Cohesion: 0.10
Nodes (14): impl_, active, busy, conn, delivered, inListener, inState, inStream (+6 more)

### Community 24 - "LevelMeter"
Cohesion: 0.09
Nodes (16): LevelMeter, db_, floorPen_, grad_, holdDb_, holdPen_, holdSince_, kMaxDb (+8 more)

### Community 26 - "Briefing de Design, UI/UX e Arquitetura Frontend — **Lotus Jam**"
Cohesion: 0.13
Nodes (14): 1. Visão Geral e Direção Criativa (*Nana* Punk-Rock Dark Mode), 2.1 Paleta de Cores, 2.2 Tipografia, 2. Design Tokens (Paleta de Cores, Tipografia e Estilo), 3.1 Especificação de Entrega, 3.2 Código Base Vetorial (SVG de Referência), 3. Ícone do App (`Lotus Jam`), 4.1 Regras Invioláveis de Performance e Threading (+6 more)

### Community 27 - "session.cpp"
Cohesion: 0.11
Nodes (6): Session::analysisLoop(), Session::finishRecording(), Session::Session(), drain(), validate(), writeWav16Mono()

### Community 28 - "Instrument"
Cohesion: 0.13
Nodes (18): Instrument, id, label, maxHz, minHz, tunings, instruments(), make() (+10 more)

### Community 29 - "PipeWireBackend"
Cohesion: 0.08
Nodes (15): RenderFn, PipeWireBackend, dropped_, failure_, inName_, inTimes_, kSampleRate, o_ (+7 more)

### Community 30 - "SpscRing"
Cohesion: 0.11
Nodes (7): TimePoint, frame, tSec, SpscRing, buf_, capacity_, mask_

### Community 31 - "StringStrip"
Cohesion: 0.11
Nodes (13): StringStrip, font_, haloPen_, numFont_, numPen_, offBrush_, offPen_, onBrush_ (+5 more)

### Community 32 - "TEST"
Cohesion: 0.11
Nodes (13): Task 4: Altura (YIN) e serviço de afinador, centsBetween(), hzToMidi(), midiToHz(), nearestString(), noteName(), StringMatch, cents (+5 more)

### Community 33 - "IAudioBackend"
Cohesion: 0.12
Nodes (10): Review Focus, Task 10: Lotus Jam — tema, ícone, janela e treino de tempo, Task 12: Configurações, lançador e paridade com o protótipo, Task 1: Esqueleto do projeto e `SpscRing`, Task 3: Notas, instrumentos e afinações, Task 5: `OnsetDetector`, Task 6: `GridScorer`, Task 9: `PipeWireBackend` (+2 more)

### Community 34 - "ClickScheduler"
Cohesion: 0.06
Nodes (25): Checklist de paridade, Parte 1 — paridade com o protótipo (`~/metronomo`) e validação pendente, Pontos a revisitar com o resultado da validação, Validação com o baixo real (PENDENTE — a ser feita pelo usuário), Task 7: `ClickScheduler`, Checklist de paridade, Parte 1 — paridade com o protótipo (não publicado) e validação pendente, Pontos a revisitar com o resultado da validação (+17 more)

### Community 35 - "settings.cpp"
Cohesion: 0.10
Nodes (24): 5.1 Barra Superior Global (Header & Controle de Áudio PipeWire), compensationFor(), loadSettings(), number(), oneOf(), pairKey(), qs(), saveSettings() (+16 more)

### Community 37 - "Yin"
Cohesion: 0.12
Nodes (11): Yin, cmndf_, diff_, kWindow, maxHz_, minHz_, sampleRate_, tauMax_ (+3 more)

### Community 38 - "TEST"
Cohesion: 0.07
Nodes (9): countEntries(), drainAll(), TEST(), TestSink, context_, core_, loop_, proxy_ (+1 more)

### Community 39 - "onset_test.cpp"
Cohesion: 0.16
Nodes (11): addNotes(), HumWithLowNotes, legacyNoise(), LongPlaying, LowNotes, lowParams(), maxErrorMs(), medianOf() (+3 more)

### Community 41 - "pipewire_backend.cpp"
Cohesion: 0.11
Nodes (12): PipeWireBackend::nowSec(), PipeWireBackend::PipeWireBackend(), PipeWireBackend::stop(), failureText(), isInputClass(), isOutputClass(), monoNow(), NodeInfo (+4 more)

### Community 42 - "MusicTrainingView"
Cohesion: 0.07
Nodes (17): MusicTrainingView, fileLabel_, importBtn_, public, roll_, running_, speedLabel_, speedSlider_ (+9 more)

### Community 43 - "level_meter.cpp"
Cohesion: 0.17
Nodes (3): LevelMeter::readoutText(), LevelMeter::reset(), LevelMeter::LevelMeter()

### Community 44 - "TEST"
Cohesion: 0.16
Nodes (3): run(), TEST(), tone()

### Community 45 - "session_test.cpp"
Cohesion: 0.18
Nodes (5): thread, Clock, timeoutSec(), waitUntil(), waitUntilQuick()

### Community 46 - "TEST_P"
Cohesion: 0.29
Nodes (8): addHum(), addPlucks(), addTouches(), applyBursts(), dbToAmp(), noise(), checkLongPlaying(), TEST_P()

### Community 47 - "TunerReading"
Cohesion: 0.22
Nodes (8): Task 11: Afinador (`TunerWidget`), TunerReading, cents, hz, midi, stringIndex, targetHz, voiced

### Community 48 - "fake_backend.cpp"
Cohesion: 0.16
Nodes (4): FakeBackend::gaussian(), FakeBackend::run(), FakeBackend::stop(), FakeBackend::FakeBackend()

### Community 49 - ".paintEvent"
Cohesion: 0.22
Nodes (3): angleOf(), polar(), qtAngle()

### Community 50 - "InputTimeline"
Cohesion: 0.13
Nodes (13): InputTimeline, accounted_, kJitterFrames, kKeepFrames, kMatchSec, kMaxObs, kMaxSegments, kMinGapFrames (+5 more)

### Community 51 - "MainWindow::buildUi"
Cohesion: 0.24
Nodes (4): MainWindow::buildUi(), MainWindow::onDevicesListed(), MainWindow::onTabChanged(), QComboBox

### Community 53 - "Lotus Jam — Parte 1: núcleo, afinador e treino de tempo — Plano de implementação"
Cohesion: 0.50
Nodes (3): Auto-revisão, Global Constraints, Lotus Jam — Parte 1: núcleo, afinador e treino de tempo — Plano de implementação

### Community 54 - "MidiImportWidget"
Cohesion: 0.10
Nodes (8): MidiImportWidget::onClear(), MidiImportWidget::onLoadFile(), MidiImportWidget, logBrowser_, MidiImportWidget::MidiImportWidget(), public, song_, statusLabel_

### Community 55 - "Treino — metrônomo, afinador e treinador de músicas para baixo/guitarra/violão (C++)"
Cohesion: 0.18
Nodes (10): 1. Intenção e porquê, 2. Objetivos / não-objetivos / anti-objetivos, 3. Restrições (ambiente real), 4. Abordagens consideradas, 5. Arquitetura (decidido pelo usuário/pelo design), 6. Funcionalidades — comportamento, 7. Critérios de aceitação, 8. Fora do documento / a cargo do construtor (+2 more)

### Community 57 - "Options"
Cohesion: 0.18
Nodes (11): Options, backpressure, captureOutputFrames, inputCapacity, inputLatencySec, noiseSigma, outputLatencySec, pauseAtSec (+3 more)

### Community 59 - "FloorParam"
Cohesion: 0.18
Nodes (11): FloorParam, humDbfs, humH2, medianMs, segPct, take, LongParam, harmonic (+3 more)

### Community 60 - "PianoRollWidget"
Cohesion: 0.10
Nodes (11): PianoRollWidget, midiHigh_, midiLow_, notes_, now_, nowLineX_, public, pxPerNote_ (+3 more)

### Community 61 - "timeline_widget.cpp"
Cohesion: 0.27
Nodes (3): alpha(), TimelineWidget::clear(), TimelineWidget::TimelineWidget()

### Community 62 - "Lotus Jam"
Cohesion: 0.20
Nodes (9): Arquitetura, As abas, Compensação de latência, Compilar, Configurações, Limitações conhecidas, Lotus Jam, Rodar (+1 more)

### Community 63 - "SessionConfig"
Cohesion: 0.15
Nodes (13): SessionConfig, a4, bpm, compensationMs, countIn, gainDb, instrument, mode (+5 more)

### Community 64 - "AudioDevice"
Cohesion: 0.36
Nodes (6): AudioDevice, description, name, defaultInputIndex(), defaultOutputIndex(), firstMatch()

### Community 65 - "tempo_training_view.cpp"
Cohesion: 0.21
Nodes (7): card(), cardTitle(), TempoTrainingView::bpm(), TempoTrainingView::countInBeats(), TempoTrainingView::subdivision(), field(), TempoTrainingView::TempoTrainingView()

### Community 68 - "Feed"
Cohesion: 0.14
Nodes (10): Feed, base, delivered, dropped, produced, Run, d0, d1 (+2 more)

### Community 69 - "music_training_view.cpp"
Cohesion: 0.17
Nodes (8): MusicTrainingView::filePath(), MusicTrainingView::onImport(), MusicTrainingView::selectedTrack(), MusicTrainingView::speedFactor(), MusicTrainingView::updateSpeedLabel(), MusicTrainingView::updateTrackCombo(), field(), MusicTrainingView::MusicTrainingView()

### Community 70 - "Point"
Cohesion: 0.20
Nodes (5): Point, errMs, miss, slot, rect

### Community 71 - "HumParam"
Cohesion: 0.25
Nodes (8): HumParam, h2, h3, hz, oldFalse, oldHits, peak, rmsDbfs

### Community 72 - "Task 8: Backend abstrato, `FakeBackend` e `Session` (threads)"
Cohesion: 0.14
Nodes (13): 5.2 Aba: O Afinador (`TunerWidget`) — *Estética Nana Osaki*, 5.3 Aba: Treino de Tempo (`TempoTrainingView`), 5.4 Aba: Leitor de MIDI, Faixas e Treino de Música (`PianoRollView`), 5. Especificação Detalhada das Telas e Widgets, Controles e Funcionalidades Específicas (Conforme Spec §6 e §9), Estética Visual do Piano Roll, Layout e Componentes, Task 8: Backend abstrato, `FakeBackend` e `Session` (threads) (+5 more)

### Community 73 - "listOutputDevices"
Cohesion: 0.70
Nodes (4): listDevices(), listInputDevices(), listOutputDevices(), listPipeWireDevices()

### Community 74 - "Rec"
Cohesion: 0.29
Nodes (6): Rec, Failed, Interrupted, None, Recording, Saved

### Community 76 - "TakeEnv"
Cohesion: 0.29
Nodes (7): TakeEnv, humDbfs, humH2, medianMs, noiseRms, segPctOverride, start

### Community 77 - "NoteScorer"
Cohesion: 0.14
Nodes (12): NoteScorer, comp_, done_, extras_, kPitchToleranceCents, kTimingOkMs, kTimingPerfectMs, nextFinal_ (+4 more)

### Community 79 - "TickWatch"
Cohesion: 0.33
Nodes (4): TickWatch, frames, have, ticks

### Community 80 - "tuner.cpp"
Cohesion: 0.31
Nodes (6): Tuner::analyse(), median3(), unvoiced(), PitchResult, hz, probability

### Community 81 - "waitTuner"
Cohesion: 0.29
Nodes (4): TunerWidget, pumpUntil(), waitRunning(), waitTuner()

### Community 82 - "Led"
Cohesion: 0.33
Nodes (6): TunerWidget::ensureAnimating(), Led, from, sign, span, start

### Community 85 - ".startSession"
Cohesion: 0.14
Nodes (7): Mode, Music, Training, Tuner, MainWindow::reconcile(), MainWindow::stopSession(), factory_

### Community 86 - "MidiSong"
Cohesion: 0.16
Nodes (9): getTrack(), MidiSong, division, tempoBpm, tracks, MidiTrack, instrument, name (+1 more)

### Community 87 - "midi_parser.cpp"
Cohesion: 0.21
Nodes (5): decodeVLQ(), parseMidiFile(), readExact(), readU16(), readU32()

### Community 88 - "Tick"
Cohesion: 0.40
Nodes (4): Tick, line, major, zero

### Community 89 - "TunerWidget::TunerWidget"
Cohesion: 0.13
Nodes (5): field(), Label, text, zero, TunerWidget::TunerWidget()

### Community 90 - "Summary"
Cohesion: 0.15
Nodes (12): Summary, early, extras, hits, lastN, late, meanErrMs, misses (+4 more)

### Community 91 - "DeviceLists"
Cohesion: 0.40
Nodes (4): DeviceLists, inputs, outputs, fakeDevices()

### Community 93 - "StreamResult"
Cohesion: 0.40
Nodes (5): realisticStream(), StreamResult, drops, splitDrops, worst

### Community 94 - "LowParam"
Cohesion: 0.40
Nodes (5): LowParam, harmonic, hz, spacing, tau

### Community 98 - "piano_roll_widget.cpp"
Cohesion: 0.18
Nodes (3): PianoRollWidget::clear(), PianoRollWidget::recalcGeometry(), PianoRollWidget::PianoRollWidget()

### Community 99 - "vector"
Cohesion: 0.29
Nodes (4): Recording, buf, filled, path

### Community 100 - "Obs"
Cohesion: 0.67
Nodes (3): Obs, tSec, value

### Community 101 - "MidiNote"
Cohesion: 0.22
Nodes (6): MidiNote, duration, midi, start, stringId, makeNotes()

### Community 102 - "DropReq"
Cohesion: 0.67
Nodes (3): DropReq, atSec, frames

### Community 103 - "DropWin"
Cohesion: 0.67
Nodes (3): DropWin, begin, end

### Community 105 - "NoteResult"
Cohesion: 0.22
Nodes (9): NoteResult, errorMs, expectedMidi, noteIndex, playedMidi, score, NoteState, matched (+1 more)

### Community 107 - "DisplayNote"
Cohesion: 0.22
Nodes (6): DisplayNote, duration, hasResult, midi, result, start

### Community 109 - "Kind"
Cohesion: 0.29
Nodes (7): Kind, Early, Extra, Hit, Late, Miss, WrongNote

### Community 111 - "Failure"
Cohesion: 0.40
Nodes (5): Failure, kDaemonGone, kDeviceGone, kNone, kStreamError

### Community 113 - "Segment"
Cohesion: 0.50
Nodes (4): Segment, first, map, start

### Community 114 - "DetectedNote"
Cohesion: 0.50
Nodes (4): DetectedNote, cents, midi, tSec

## Knowledge Gaps
- **684 isolated node(s):** `Training`, `Tuner`, `Music`, `mode`, `bpm` (+679 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1137 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **30 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Session` connect `Session` to `MainWindow`, `GridScorer`, `TEST`, `ClockMap`, `UiEvent`, `.startSession`, `OnsetDetector`, `session.cpp`, `SpscRing`, `IAudioBackend`, `ClickScheduler`, `session.h`, `session_test.cpp`, `InputTimeline`, `main_window.h`, `SessionConfig`, `NoteScorer`, `.startSession`, `vector`, `DetectedNote`?**
  _High betweenness centrality (0.214) - this node is a cross-community bridge._
- **Are the 5 inferred relationships involving `MainWindow` (e.g. with `4.1 Regras Invioláveis de Performance e Threading` and `5.1 Barra Superior Global (Header & Controle de Áudio PipeWire)`) actually correct?**
  _`MainWindow` has 5 INFERRED edges - model-reasoned connections that need verification._
- **What connects `Training`, `Tuner`, `Music` to the rest of the system?**
  _684 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `MainWindow` be split into smaller, more focused modules?**
  _Cohesion score 0.024021962937542895 - nodes in this community are weakly interconnected._
- **Why does `MainWindow` connect `MainWindow` to `IAudioBackend`, `TunerWidget`, `settings.cpp`, `TEST`, `Session`, `session_test.cpp`, `UiEvent`, `.setRunning`, `waitTuner`, `.startSession`, `main_window.cpp`, `MainWindow::buildUi`, `.startSession`, `Lotus Jam — Parte 1: núcleo, afinador e treino de tempo — Plano de implementação`, `main_window.h`, `Briefing de Design, UI/UX e Arquitetura Frontend — **Lotus Jam**`, `DeviceLists`?**
  _High betweenness centrality (0.146) - this node is a cross-community bridge._
- **Are the 4 inferred relationships involving `TunerDial` (e.g. with `TunerWidget::clearReading()` and `TunerWidget::refreshViews()`) actually correct?**
  _`TunerDial` has 4 INFERRED edges - model-reasoned connections that need verification._
- **Should `TunerDial` be split into smaller, more focused modules?**
  _Cohesion score 0.03277009728622632 - nodes in this community are weakly interconnected._