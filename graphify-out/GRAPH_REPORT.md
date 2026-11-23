# Graph Report - treino  (2026-11-18)

## Corpus Check
- 69 files · ~64,038 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 5 file(s) not represented in the graph (top: (none) 2, .desktop 1, .qrc 1)

## Summary
- 1746 nodes · 3037 edges · 109 communities (88 shown, 21 thin omitted)
- Extraction: 89% EXTRACTED · 11% INFERRED · 0% AMBIGUOUS · INFERRED: 326 edges (avg confidence: 0.87)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `893257da`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- MainWindow
- TunerDial
- TunerWidget
- GridScorer
- tempo_training_view.cpp
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
- Session::scoringLoop
- ClickScheduler
- settings.cpp
- session.h
- Yin
- TEST
- onset_test.cpp
- TEST
- pipewire_backend.cpp
- TestSink
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
- Review Focus
- Settings
- Treino — metrônomo, afinador e treinador de músicas para baixo/guitarra/violão (C++)
- main_window.h
- Options
- MainWindow::MainWindow
- FloorParam
- TEST
- timeline_widget.cpp
- Lotus Jam
- SessionConfig
- AudioDevice
- string
- pulse_widget.cpp
- Feed
- onset.cpp
- Point
- HumParam
- Task 8: Backend abstrato, `FakeBackend` e `Session` (threads)
- listOutputDevices
- Rec
- Rng
- TakeEnv
- TEST
- click_scheduler_rt_test.cpp
- TickWatch
- yin.cpp
- waitTuner
- Led
- Rand
- TEST
- Checklist de paridade
- Checklist de paridade
- Tick
- Label
- DeviceLists
- Engine
- StreamResult
- LowParam
- TEST
- atomic
- Options
- StringMatch
- Run
- Obs
- BeatMark
- DropReq
- DropWin
- Noise
- MainWindow::engineBadgeTexts
- LowFloorPlaying

## God Nodes (most connected - your core abstractions)
1. `MainWindow` - 153 edges
2. `TunerDial` - 85 edges
3. `TunerWidget` - 80 edges
4. `Session` - 74 edges
5. `FakeBackend` - 72 edges
6. `TEST()` - 63 edges
7. `TempoTrainingView` - 53 edges
8. `TimelineWidget` - 44 edges
9. `TEST()` - 42 edges
10. `Tuner` - 39 edges

## Surprising Connections (you probably didn't know these)
- `Arquitetura` --references--> `FakeBackend`  [INFERRED]
  README.md → src/audio/fake_backend.h
- `Testes` --references--> `FakeBackend`  [INFERRED]
  README.md → src/audio/fake_backend.h
- `Task 5: `OnsetDetector`` --references--> `OnsetDetector`  [INFERRED]
  docs/superpowers/plans/2026-11-17-parte1-nucleo-afinador-tempo.md → src/dsp/onset.h
- `7. Checklist de Aceitação e Testes Automatizados (`ui_smoke_test.cpp`)` --references--> `FakeBackend`  [INFERRED]
  docs/front/briefing_de_ui_ux_e_arquitetura_frontend_lotus_jam.md → src/audio/fake_backend.h
- `Task 6: `GridScorer`` --references--> `GridScorer`  [INFERRED]
  docs/superpowers/plans/2026-11-17-parte1-nucleo-afinador-tempo.md → src/score/grid_scorer.h

## Import Cycles
- None detected.

## Communities (109 total, 21 thin omitted)

### Community 0 - "MainWindow"
Cohesion: 0.02
Nodes (68): main(), LevelMeter, MainWindow, applying_, backend_, badge_, comp_, countIn_ (+60 more)

### Community 1 - "TunerDial"
Cohesion: 0.03
Nodes (59): TunerDial, arcRect_, centre_, dashText_, flatFarPen_, flatRect_, flatText_, glyphFont_ (+51 more)

### Community 2 - "TunerWidget"
Cohesion: 0.05
Nodes (25): StringStrip, TunerDial, QComboBox, QDoubleSpinBox, QPushButton, TunerWidget, anim_, dial_ (+17 more)

### Community 3 - "GridScorer"
Cohesion: 0.05
Nodes (23): GridScorer, comp_, done_, extras_, nextFinal_, pending_, pendingSlot_, Slot (+15 more)

### Community 4 - "tempo_training_view.cpp"
Cohesion: 0.08
Nodes (17): MainWindow::calibrate(), MainWindow::syncStartAllowed(), card(), cardTitle(), TempoTrainingView::bpm(), TempoTrainingView::countInBeats(), TempoTrainingView::subdivision(), field() (+9 more)

### Community 6 - "TempoTrainingView"
Cohesion: 0.08
Nodes (17): PulseWidget, QLabel, QPushButton, QSpinBox, ScoreSummaryCard, TempoTrainingView, calibLabel_, calibrate_ (+9 more)

### Community 7 - "TimelineWidget"
Cohesion: 0.06
Nodes (24): TimelineWidget, axisPen_, bandExact_, bandMiss_, bandWarn_, bg_, dotExact_, dotMiss_ (+16 more)

### Community 8 - "Summary"
Cohesion: 0.09
Nodes (21): Summary, extras, hits, lastN, meanErrMs, slots, stdErrMs, total (+13 more)

### Community 9 - "Session"
Cohesion: 0.06
Nodes (28): Session, active_, anaEvents_, analysedFrames_, analysis_, analysisDone_, backend_, carry_ (+20 more)

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
Cohesion: 0.08
Nodes (19): Task 2: `ClockMap` (amostra → tempo), Segment, first, map, start, ClockMap, a_, b_ (+11 more)

### Community 14 - "UiEvent"
Cohesion: 0.08
Nodes (22): UiEvent, a, b, n, text, tuner, Type, Beat (+14 more)

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
Cohesion: 0.11
Nodes (9): Mode, Training, Tuner, MainWindow::onStartFinished(), MainWindow::poll(), MainWindow::reconcile(), MainWindow::recordSample(), MainWindow::stopSession() (+1 more)

### Community 19 - "main_window.cpp"
Cohesion: 0.10
Nodes (12): MainWindow::loadPairCompensation(), MainWindow::onTunerConfigChanged(), MainWindow::refreshDevices(), MainWindow::scheduleSave(), MainWindow::shutdown(), ElidingComboBox, fillCombo(), labelled() (+4 more)

### Community 20 - "OnsetDetector"
Cohesion: 0.07
Nodes (27): OnsetDetector, anchor_, db_, dc_, dcAlpha_, dcInit_, env_, haveThr_ (+19 more)

### Community 21 - "tuner_widget.cpp"
Cohesion: 0.10
Nodes (10): TunerWidget::a4(), TunerWidget::glowActive(), TunerWidget::highlightedString(), TunerWidget::rebuildTunings(), TunerWidget::refreshViews(), TunerWidget::stringLabels(), TunerWidget::tuning(), field() (+2 more)

### Community 22 - "ui_smoke_test.cpp"
Cohesion: 0.10
Nodes (6): QSettings, TempIni, dir, currentNode(), litPixels(), settle()

### Community 23 - "impl_"
Cohesion: 0.10
Nodes (15): PipeWireBackend::stop(), impl_, active, busy, conn, delivered, inListener, inState (+7 more)

### Community 24 - "LevelMeter"
Cohesion: 0.09
Nodes (16): LevelMeter, db_, floorPen_, grad_, holdDb_, holdPen_, holdSince_, kMaxDb (+8 more)

### Community 26 - "Briefing de Design, UI/UX e Arquitetura Frontend — **Lotus Jam**"
Cohesion: 0.09
Nodes (21): 1. Visão Geral e Direção Criativa (*Nana* Punk-Rock Dark Mode), 2.1 Paleta de Cores, 2.2 Tipografia, 2. Design Tokens (Paleta de Cores, Tipografia e Estilo), 3.1 Especificação de Entrega, 3.2 Código Base Vetorial (SVG de Referência), 3. Ícone do App (`Lotus Jam`), 4.1 Regras Invioláveis de Performance e Threading (+13 more)

### Community 27 - "session.cpp"
Cohesion: 0.13
Nodes (8): Session::analysisLoop(), Session::finishRecording(), Session::refreshSummary(), Session::Session(), Session::summary(), drain(), validate(), writeWav16Mono()

### Community 28 - "Instrument"
Cohesion: 0.15
Nodes (18): Task 4: Altura (YIN) e serviço de afinador, Instrument, id, label, maxHz, minHz, tunings, instruments() (+10 more)

### Community 29 - "PipeWireBackend"
Cohesion: 0.10
Nodes (11): RenderFn, PipeWireBackend, dropped_, failure_, inName_, inTimes_, kSampleRate, o_ (+3 more)

### Community 30 - "SpscRing"
Cohesion: 0.11
Nodes (7): TimePoint, frame, tSec, SpscRing, buf_, capacity_, mask_

### Community 31 - "StringStrip"
Cohesion: 0.11
Nodes (13): StringStrip, TunerWidget::clearReading(), font_, haloPen_, numFont_, numPen_, offBrush_, offPen_ (+5 more)

### Community 32 - "TEST"
Cohesion: 0.13
Nodes (9): centsBetween(), hzToMidi(), midiToHz(), nearestString(), noteName(), TunerWidget::updateTexts(), TEST(), tuning() (+1 more)

### Community 33 - "Session::scoringLoop"
Cohesion: 0.14
Nodes (5): Session::scoringLoop(), Session::stop(), IAudioBackend, beatTime(), beatTime()

### Community 34 - "ClickScheduler"
Cohesion: 0.18
Nodes (9): Task 7: `ClickScheduler`, addTone(), ClickScheduler, click_, ClickScheduler::ClickScheduler(), cue_, firstBeat_, fpb_ (+1 more)

### Community 35 - "settings.cpp"
Cohesion: 0.25
Nodes (12): 5.1 Barra Superior Global (Header & Controle de Áudio PipeWire), compensationFor(), loadSettings(), number(), oneOf(), pairKey(), qs(), saveSettings() (+4 more)

### Community 36 - "session.h"
Cohesion: 0.29
Nodes (3): Writer, done, thread

### Community 37 - "Yin"
Cohesion: 0.12
Nodes (11): Yin, cmndf_, diff_, kWindow, maxHz_, minHz_, sampleRate_, tauMax_ (+3 more)

### Community 38 - "TEST"
Cohesion: 0.12
Nodes (4): countEntries(), drainAll(), TEST(), waitFor()

### Community 39 - "onset_test.cpp"
Cohesion: 0.16
Nodes (11): addNotes(), HumWithLowNotes, legacyNoise(), LongPlaying, LowNotes, lowParams(), maxErrorMs(), medianOf() (+3 more)

### Community 41 - "pipewire_backend.cpp"
Cohesion: 0.14
Nodes (8): PipeWireBackend::nowSec(), Failure, kDaemonGone, kDeviceGone, kNone, kStreamError, failureText(), monoNow()

### Community 42 - "TestSink"
Cohesion: 0.15
Nodes (5): TestSink, context_, core_, loop_, proxy_

### Community 43 - "level_meter.cpp"
Cohesion: 0.16
Nodes (3): LevelMeter::readoutText(), LevelMeter::reset(), LevelMeter::LevelMeter()

### Community 44 - "TEST"
Cohesion: 0.15
Nodes (3): run(), TEST(), tone()

### Community 45 - "session_test.cpp"
Cohesion: 0.19
Nodes (4): Clock, timeoutSec(), waitUntil(), waitUntilQuick()

### Community 46 - "TEST_P"
Cohesion: 0.29
Nodes (8): addHum(), addPlucks(), addTouches(), applyBursts(), dbToAmp(), noise(), checkLongPlaying(), TEST_P()

### Community 47 - "TunerReading"
Cohesion: 0.19
Nodes (11): Task 11: Afinador (`TunerWidget`), Tuner::analyse(), median3(), TunerReading, cents, hz, midi, stringIndex (+3 more)

### Community 48 - "fake_backend.cpp"
Cohesion: 0.16
Nodes (4): FakeBackend::gaussian(), FakeBackend::run(), FakeBackend::stop(), FakeBackend::FakeBackend()

### Community 49 - ".paintEvent"
Cohesion: 0.18
Nodes (4): angleOf(), polar(), qtAngle(), withAlpha()

### Community 50 - "InputTimeline"
Cohesion: 0.14
Nodes (12): InputTimeline, accounted_, kJitterFrames, kKeepFrames, kMatchSec, kMaxObs, kMaxSegments, kMinGapFrames (+4 more)

### Community 51 - "MainWindow::buildUi"
Cohesion: 0.21
Nodes (5): MainWindow::applySettings(), MainWindow::buildUi(), MainWindow::onDevicesListed(), MainWindow::onTabChanged(), QComboBox

### Community 53 - "Review Focus"
Cohesion: 0.17
Nodes (11): Auto-revisão, Global Constraints, Lotus Jam — Parte 1: núcleo, afinador e treino de tempo — Plano de implementação, Review Focus, Task 10: Lotus Jam — tema, ícone, janela e treino de tempo, Task 12: Configurações, lançador e paridade com o protótipo, Task 1: Esqueleto do projeto e `SpscRing`, Task 3: Notas, instrumentos e afinações (+3 more)

### Community 54 - "Settings"
Cohesion: 0.17
Nodes (11): Settings, a4, bpm, compensationMs, countInBeats, gainDb, inputNode, instrumentId (+3 more)

### Community 55 - "Treino — metrônomo, afinador e treinador de músicas para baixo/guitarra/violão (C++)"
Cohesion: 0.18
Nodes (10): 1. Intenção e porquê, 2. Objetivos / não-objetivos / anti-objetivos, 3. Restrições (ambiente real), 4. Abordagens consideradas, 5. Arquitetura (decidido pelo usuário/pelo design), 6. Funcionalidades — comportamento, 7. Critérios de aceitação, 8. Fora do documento / a cargo do construtor (+2 more)

### Community 56 - "main_window.h"
Cohesion: 0.29
Nodes (4): Recording, buf, filled, path

### Community 57 - "Options"
Cohesion: 0.18
Nodes (11): Options, backpressure, captureOutputFrames, inputCapacity, inputLatencySec, noiseSigma, outputLatencySec, pauseAtSec (+3 more)

### Community 58 - "MainWindow::MainWindow"
Cohesion: 0.18
Nodes (5): BackendFactory, MainWindow::flushSettings(), DeviceLister, MainWindow::MainWindow(), jitterMs()

### Community 59 - "FloorParam"
Cohesion: 0.18
Nodes (11): FloorParam, humDbfs, humH2, medianMs, segPct, take, LongParam, harmonic (+3 more)

### Community 60 - "TEST"
Cohesion: 0.22
Nodes (3): energy(), renderAll(), TEST()

### Community 61 - "timeline_widget.cpp"
Cohesion: 0.27
Nodes (3): alpha(), TimelineWidget::clear(), TimelineWidget::TimelineWidget()

### Community 62 - "Lotus Jam"
Cohesion: 0.20
Nodes (9): Arquitetura, As abas, Compensação de latência, Compilar, Configurações, Limitações conhecidas, Lotus Jam, Rodar (+1 more)

### Community 63 - "SessionConfig"
Cohesion: 0.20
Nodes (10): SessionConfig, a4, bpm, compensationMs, countIn, gainDb, instrument, mode (+2 more)

### Community 64 - "AudioDevice"
Cohesion: 0.36
Nodes (6): AudioDevice, description, name, defaultInputIndex(), defaultOutputIndex(), firstMatch()

### Community 65 - "string"
Cohesion: 0.22
Nodes (6): PipeWireBackend::PipeWireBackend(), NodeInfo, description, id, mediaClass, name

### Community 68 - "Feed"
Cohesion: 0.22
Nodes (6): Feed, base, delivered, dropped, produced, runs

### Community 70 - "Point"
Cohesion: 0.25
Nodes (5): Point, errMs, miss, slot, rect

### Community 71 - "HumParam"
Cohesion: 0.25
Nodes (8): HumParam, h2, h3, hz, oldFalse, oldHits, peak, rmsDbfs

### Community 72 - "Task 8: Backend abstrato, `FakeBackend` e `Session` (threads)"
Cohesion: 0.29
Nodes (6): Task 8: Backend abstrato, `FakeBackend` e `Session` (threads), Note, amp, end, hz, t

### Community 73 - "listOutputDevices"
Cohesion: 0.43
Nodes (6): isInputClass(), isOutputClass(), listDevices(), listInputDevices(), listOutputDevices(), listPipeWireDevices()

### Community 74 - "Rec"
Cohesion: 0.29
Nodes (6): Rec, Failed, Interrupted, None, Recording, Saved

### Community 76 - "TakeEnv"
Cohesion: 0.29
Nodes (7): TakeEnv, humDbfs, humH2, medianMs, noiseRms, segPctOverride, start

### Community 79 - "TickWatch"
Cohesion: 0.33
Nodes (4): TickWatch, frames, have, ticks

### Community 80 - "yin.cpp"
Cohesion: 0.33
Nodes (3): PitchResult, hz, probability

### Community 81 - "waitTuner"
Cohesion: 0.33
Nodes (3): TunerWidget, pumpUntil(), waitTuner()

### Community 82 - "Led"
Cohesion: 0.33
Nodes (6): TunerWidget::ensureAnimating(), Led, from, sign, span, start

### Community 85 - "Checklist de paridade"
Cohesion: 0.40
Nodes (4): Checklist de paridade, Parte 1 — paridade com o protótipo (`~/metronomo`) e validação pendente, Pontos a revisitar com o resultado da validação, Validação com o baixo real (PENDENTE — a ser feita pelo usuário)

### Community 86 - "Checklist de paridade"
Cohesion: 0.40
Nodes (4): Checklist de paridade, Parte 1 — paridade com o protótipo (não publicado) e validação pendente, Pontos a revisitar com o resultado da validação, Validação com o baixo real (PENDENTE — a ser feita pelo usuário)

### Community 88 - "Tick"
Cohesion: 0.40
Nodes (4): Tick, line, major, zero

### Community 89 - "Label"
Cohesion: 0.40
Nodes (3): Label, text, zero

### Community 91 - "DeviceLists"
Cohesion: 0.40
Nodes (4): DeviceLists, inputs, outputs, fakeDevices()

### Community 92 - "Engine"
Cohesion: 0.40
Nodes (5): Engine, Failed, Idle, Running, Starting

### Community 93 - "StreamResult"
Cohesion: 0.40
Nodes (5): realisticStream(), StreamResult, drops, splitDrops, worst

### Community 94 - "LowParam"
Cohesion: 0.40
Nodes (5): LowParam, harmonic, hz, spacing, tau

### Community 97 - "Options"
Cohesion: 0.50
Nodes (4): Options, connectTimeoutSec, inputCapacity, quantum

### Community 98 - "StringMatch"
Cohesion: 0.50
Nodes (4): StringMatch, cents, stringIndex, targetHz

### Community 99 - "Run"
Cohesion: 0.50
Nodes (4): Run, d0, d1, t0

### Community 100 - "Obs"
Cohesion: 0.67
Nodes (3): Obs, tSec, value

### Community 101 - "BeatMark"
Cohesion: 0.67
Nodes (3): BeatMark, beat, frame

### Community 102 - "DropReq"
Cohesion: 0.67
Nodes (3): DropReq, atSec, frames

### Community 103 - "DropWin"
Cohesion: 0.67
Nodes (3): DropWin, begin, end

## Knowledge Gaps
- **596 isolated node(s):** `applying_`, `backend_`, `badge_`, `comp_`, `countIn_` (+591 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 979 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **21 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Session` connect `Session` to `MainWindow`, `Session::scoringLoop`, `ClickScheduler`, `GridScorer`, `session.h`, `Summary`, `pipewire_backend.cpp`, `TEST`, `ClockMap`, `UiEvent`, `InputTimeline`, `.startSession`, `OnsetDetector`, `main_window.h`, `cmath`, `session.cpp`, `SpscRing`, `SessionConfig`?**
  _High betweenness centrality (0.178) - this node is a cross-community bridge._
- **Are the 5 inferred relationships involving `MainWindow` (e.g. with `4.1 Regras Invioláveis de Performance e Threading` and `5.1 Barra Superior Global (Header & Controle de Áudio PipeWire)`) actually correct?**
  _`MainWindow` has 5 INFERRED edges - model-reasoned connections that need verification._
- **What connects `applying_`, `backend_`, `badge_` to the rest of the system?**
  _596 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `MainWindow` be split into smaller, more focused modules?**
  _Cohesion score 0.024544179523141654 - nodes in this community are weakly interconnected._
- **Why does `MainWindow` connect `MainWindow` to `Session::scoringLoop`, `settings.cpp`, `session.h`, `MainWindow::MainWindow`, `tempo_training_view.cpp`, `TEST`, `Session`, `MainWindow::engineBadgeTexts`, `UiEvent`, `waitTuner`, `.startSession`, `MainWindow::buildUi`, `main_window.cpp`, `Review Focus`, `main_window.h`, `Briefing de Design, UI/UX e Arquitetura Frontend — **Lotus Jam**`, `DeviceLists`, `Engine`?**
  _High betweenness centrality (0.169) - this node is a cross-community bridge._
- **Are the 4 inferred relationships involving `TunerDial` (e.g. with `TunerWidget::clearReading()` and `TunerWidget::refreshViews()`) actually correct?**
  _`TunerDial` has 4 INFERRED edges - model-reasoned connections that need verification._
- **Should `TunerDial` be split into smaller, more focused modules?**
  _Cohesion score 0.03277009728622632 - nodes in this community are weakly interconnected._