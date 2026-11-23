# Lotus Jam — Parte 1: núcleo, afinador e treino de tempo — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reescrever em C++ o treino de tempo do protótipo Python (`~/metronomo/`) com áudio PipeWire nativo, multithread real, e acrescentar o afinador (violão, guitarra, baixo).

**Architecture:** Biblioteca `treino_core` sem Qt (ring buffers SPSC, DSP, pontuação, backend de áudio abstrato, `Session` com threads de análise e pontuação) + executável Qt6 Widgets que só desenha, lendo eventos a ~60 Hz. Callbacks PipeWire fazem apenas cópia/geração em memória pré-alocada. `FakeBackend` permite testar tudo sem placa de áudio.

**Tech Stack:** C++20, CMake + Ninja, Qt 6.12 Widgets, libpipewire-0.3, GoogleTest (já instalado no sistema), sem FFTW nesta parte (YIN no domínio do tempo).

**Spec:** `docs/superpowers/specs/2026-11-17-treino-design.md` (seções 3, 5, 6, 7, 9). **Spec de UI (autoridade para as Tasks 10–12):** `docs/front/briefing_de_ui_ux_e_arquitetura_frontend_lotus_jam.md` (produto **Lotus Jam**; biblioteca interna continua `treino_core`). Protótipo de referência: `~/metronomo/engine.py` (Detector, Motor, Mapa) e `~/metronomo/treino.py` (pontuação).

## Global Constraints

- C++ com cmake+ninja; Qt 6.12; **PipeWire nativo (libpipewire)**; nunca ALSA direto.
- Dispositivo do baixo: PCM2902, mono, 16 bit, 48 kHz; sinal fraco (~ −39 dBFS de ruído de fundo).
- Callbacks de áudio em tempo real: **sem malloc, sem locks, sem I/O**.
- Threads: callbacks PipeWire (entrada e saída), análise (DSP), pontuação, carga (pool, partes futuras), UI. Comunicação por ring buffers SPSC sem trava.
- Clique uniforme **sem acento**; depois do sinal de partida (dois tons) e da contagem, **só o clique**.
- Faixas do gráfico: ≤15 ms verde, ≤50 ms amarelo, resto vermelho; nota extra = mais de 200 ms de qualquer batida; janela de associação = `min(0.5·passo, 0.2 s)`.
- Nota por batida: `100·clip((100−|erro_ms|)/85, 0, 1)`; falta vale 0.
- Detecção de ataque: erro de tempo < 3 ms em sinal sintético. Afinador: erro < 1 cent; resposta visual < 100 ms; A4 = 440 Hz ajustável; baixo de 5 cordas chega a ~31 Hz (B0).
- Zero xruns com a UI sob carga. Interface em pt-BR.
- Dependências vendorizadas; **nenhum download na configuração do build**.
- Compensação de latência em ms salva por par (entrada, saída).
- Produto: **Lotus Jam**; executável `lotus-jam`; interface 100% pt-BR; paleta, tipografia e QSS do briefing (`#09090B` fundo, `#FF2A85` rosa assinatura, `#00F5A0/#FFB800/#FF1744` zonas); fontes com fallback (Inter→Noto Sans; JetBrains Mono→Fira Code→monospace).
- Thread de UI nunca faz DSP nem I/O bloqueante; `QTimer` de 16 ms chama `pollEvents`; widgets customizados com `QPainter::Antialiasing` e pens/brushes/gradients pré-alocados.
- `MainWindow` aceita `std::function<std::unique_ptr<IAudioBackend>(const std::string& inNode, const std::string& outNode)>` para injetar `FakeBackend`.
- Botão "gravar amostra" (WAV da entrada) e ganho digital de entrada (spec §9, decisão 2).

## Review Focus

1. **Silêncio ou só ruído estacionário na entrada** → nenhuma nota inventada depois do aquecimento; todas as batidas viram falta; sem travar (testes: `OnsetDetector.NoiseOnlyNoOnsets`, `Session.SilenceAllMisses`).
2. **Dispositivo some / PipeWire parado no meio da sessão** → erro claro na UI, sessão encerra sem deadlock (`Session.BackendFailureStops`, `PipeWireBackend.ConnectFailureReported`).
3. **BPM extremos (30 e 240) com subdivisão 4** → janela de associação e passos corretos, sem divisão por zero (`GridScorer.ExtremeTempos`).
4. **Frequência fora da faixa / sinal que não é de corda** (silêncio, 440 Hz no baixo, ronco de 60 Hz) → afinador mostra "sem sinal", nunca salta de oitava (`Tuner.UnvoicedAndOutOfRange`).
5. **Iniciar/parar repetidamente e fechar a janela durante a sessão** → sem deadlock, vazamento ou thread pendurada (`Session.StartStopStress` sob ThreadSanitizer).

---

### Task 1: Esqueleto do projeto e `SpscRing`

**Files:**
- Create: `CMakeLists.txt`, `src/core/ring_buffer.h`, `tests/CMakeLists.txt`, `tests/ring_buffer_test.cpp`, `.gitignore`
- Modo: `git init` na raiz `~/treino` (docs já existem), branch `main`.

**Interfaces:**
- Produces: alvo CMake `treino_core` (STATIC, C++20, `src/` no include path), alvo `treino_tests` (GTest, `ctest`), opção `-DTREINO_TSAN=ON`.
  `template <class T> class SpscRing { explicit SpscRing(size_t capacityPow2); size_t write(const T*, size_t n) noexcept; size_t read(T*, size_t n) noexcept; size_t readable() const noexcept; size_t writable() const noexcept; };` (capacidade potência de 2; um produtor, um consumidor; acquire/release).

- [ ] **Step 1:** Escrever `ring_buffer_test.cpp`: `SpscRing.WriteReadRoundTrip` (escreve 10, lê 10, valores iguais), `SpscRing.WrapAround`, `SpscRing.FullRejectsExtra` (write retorna só o que cabe), `SpscRing.ConcurrentStress` (produtor e consumidor em threads, 10 milhões de `uint32` crescentes, consumidor confere sequência sem perda).
- [ ] **Step 2:** `cmake -S . -B build -G Ninja && cmake --build build` → falha (header inexistente).
- [ ] **Step 3:** Implementar `SpscRing` header-only e o `CMakeLists.txt` raiz (C++20, `-Wall -Wextra`, `find_package(GTest)`, subdiretório de testes, opção TSAN que adiciona `-fsanitize=thread`).
- [ ] **Step 4:** `ctest --test-dir build --output-on-failure` → PASS; repetir com `-DTREINO_TSAN=ON` em `build-tsan` → PASS sem avisos.
- [ ] **Step 5:** `git add -A && git commit -m "feat: project skeleton and SPSC ring buffer"`.

### Task 2: `ClockMap` (amostra → tempo)

**Files:** Create `src/core/clock_map.h`, `src/core/clock_map.cpp`, `tests/clock_map_test.cpp`

**Interfaces:**
- Produces: `struct TimePoint { int64_t frame; double tSec; };` e
  `class ClockMap { explicit ClockMap(size_t maxPoints = 300); void add(TimePoint); bool ready() const; double timeOf(double frame) const; double frameAt(double tSec) const; };`
  Regressão linear por mínimos quadrados sobre os últimos `maxPoints`; refaz o ajuste a cada 25 pontos; `ready()` = ≥ 20 pontos. Usar pontos relativos ao primeiro para evitar perda de precisão.

- [ ] **Step 1:** Testes: `ClockMap.RecoversLinearMapping` (pontos `t = 1000 + frame/48000`, `timeOf(480000)` = 1010 ± 1e-9), `ClockMap.AveragesOutJitter` (ruído gaussiano σ=2 ms nos tempos → erro do ajuste < 0.5 ms), `ClockMap.DetectsClockDrift` (taxa 48000.5 Hz → inclinação recuperada com erro < 1e-6), `ClockMap.NotReadyBeforeTwentyPoints`.
- [ ] **Step 2:** Rodar → FAIL.
- [ ] **Step 3:** Implementar (contrato acima).
- [ ] **Step 4:** Rodar → PASS.
- [ ] **Step 5:** Commit `feat: clock map`.

### Task 3: Notas, instrumentos e afinações

**Files:** Create `src/dsp/notes.h`, `src/dsp/notes.cpp`, `tests/notes_test.cpp`

**Interfaces:**
- Produces:
  `double midiToHz(double midi, double a4 = 440.0); double hzToMidi(double hz, double a4 = 440.0); std::string noteName(int midi); double centsBetween(double hz, double refHz);`
  `struct Tuning { std::string name; std::vector<int> strings; };` (MIDI, grave→agudo)
  `struct Instrument { std::string id, label; double minHz, maxHz; std::vector<Tuning> tunings; };`
  `const std::vector<Instrument>& instruments();` com ids `violao`, `guitarra`, `baixo4`, `baixo5`, `baixo6`; `Instrument` `violao`/`guitarra`: E2 A2 D3 G3 B3 E4 = 40,45,50,55,59,64; `baixo4`: 28,33,38,43; `baixo5`: 23,28,33,38,43; `baixo6`: 23,28,33,38,43,48. Afinações: "Padrão", "Drop D" (corda mais grave −2; baixo: D1), "Meio-tom abaixo" (todas −1), e "DADGAD" (só violão/guitarra: 38,45,50,55,57,62). `minHz` = ~0,9 × corda mais grave, `maxHz` = ~1,2 × corda mais aguda + margem de harmônicos (valores decididos pelo construtor e testados).
  `struct StringMatch { int stringIndex; double targetHz; double cents; };`
  `StringMatch nearestString(const Tuning&, double hz, double a4 = 440.0);` (menor |cents|).

- [ ] **Step 1:** Testes: `Notes.A4Is440` (`midiToHz(69)==440`), `Notes.HzMidiRoundTrip`, `Notes.NoteNames` (`noteName(40)=="E2"`, `noteName(23)=="B0"`), `Notes.CentsOfSemitone` (`centsBetween(midiToHz(61), midiToHz(60))` = 100 ± 1e-6), `Notes.InstrumentsCatalog` (5 instrumentos, tuning "Padrão" com as cordas acima, DADGAD só em violão/guitarra), `Notes.NearestStringPicksClosest` (82 Hz em violão padrão → índice 0, |cents| < 20; 110 Hz → índice 1).
- [ ] **Step 2:** Rodar → FAIL. **Step 3:** Implementar. **Step 4:** PASS. **Step 5:** Commit `feat: notes, instruments and tunings`.

### Task 4: Altura (YIN) e serviço de afinador

**Files:** Create `src/dsp/yin.h`, `src/dsp/yin.cpp`, `src/dsp/tuner.h`, `src/dsp/tuner.cpp`, `tests/yin_test.cpp`, `tests/tuner_test.cpp`

**Interfaces:**
- Consumes: `Instrument`, `Tuning`, `nearestString` (Task 3).
- Produces:
  `struct PitchResult { double hz; double probability; };` (`hz == 0` = sem altura)
  `class Yin { Yin(double sampleRate, double minHz, double maxHz, double threshold = 0.12); size_t requiredSamples() const; PitchResult process(const float* x) const; };` (`requiredSamples = W + tauMax` com `W = 2048`, `tauMax = sampleRate/minHz`; diferença acumulada normalizada, limiar absoluto, interpolação parabólica).
  `struct TunerReading { bool voiced; int stringIndex; double hz, targetHz, cents; };`
  `class Tuner { Tuner(double sampleRate, const Instrument&, const Tuning&, double a4); void feed(const float* x, size_t n); bool poll(TunerReading&); };` (mantém janela deslizante interna; emite leitura ≥ 20 vezes por segundo; suaviza por mediana de 3 leituras; **descarta** resultados fora de `[minHz, maxHz]` do instrumento e probabilidade < 0.8 → `voiced=false`).

- [ ] **Step 1:** `yin_test.cpp`: `Yin.EachStringWithinOneCent` — para cada corda de todos os instrumentos (E1 41.2 Hz … E4 329.6 Hz e B0 30.87 Hz), senoide + 3 harmônicos (amplitudes 1, 0.5, 0.3, 0.2) e ruído de −50 dBFS → `|centsBetween(hz, alvo)| < 1`; `Yin.NoOctaveJumpOnWeakFundamental` (fundamental com amplitude 0.2 e 2º harmônico 1.0 em 41.2 Hz → resultado 41.2 Hz, não 82.4); `Yin.SilenceIsUnvoiced`. `tuner_test.cpp`: `Tuner.ReadingWithin100ms` (alimentar 100 ms de sinal em blocos de 480 → `poll` retorna leitura da corda certa), `Tuner.DetuneShowsCents` (E2 +20 cents → cents ≈ +20 ± 1), `Tuner.UnvoicedAndOutOfRange` (silêncio → `voiced=false`; 440 Hz com instrumento `baixo4` → `voiced=false`; 60 Hz puro com `baixo4` → não afirma oitava errada: `voiced=false` ou corda 0 com |cents| < 50).
- [ ] **Step 2:** Rodar → FAIL. **Step 3:** Implementar `Yin` e `Tuner`. **Step 4:** PASS. **Step 5:** Commit `feat: YIN pitch detector and tuner`.

### Task 5: `OnsetDetector`

**Files:** Create `src/dsp/onset.h`, `src/dsp/onset.cpp`, `tests/onset_test.cpp`

**Interfaces:**
- Produces: `class OnsetDetector { explicit OnsetDetector(double sampleRate = 48000); void feed(const float* x, size_t n, std::vector<int64_t>& onsetsOut); };` — onsets em **índice absoluto de amostra** desde a criação. Porta da classe `Detector` de `~/metronomo/engine.py` (janela RMS 5 ms, passo 1 ms, remoção de DC por média móvel inicializada no 1º bloco, limiar `max(5·p20(últimos 5 s), 0.12·p99(últimos 10 s))` recalculado a cada 500 passos, subida > 6 dB em 15 ms, refratário 120 ms, recuo até o início da subida). **Aquecimento:** sem ataques nos primeiros 1000 passos (1 s).

- [ ] **Step 1:** Testes: `OnsetDetector.SyntheticBassTimingWithin3ms` (9 notas de 55 Hz com ataque de 4 ms, decaimento exp, ruído σ=20/32768 em escala flutuante; posições conhecidas; todas detectadas, erro máx < 3 ms — alimentar em blocos de 480), `OnsetDetector.NoiseOnlyNoOnsets` (10 s de ruído branco estacionário → 0 ataques), `OnsetDetector.WeakSignalStillDetected` (mesma nota a −39 dBFS de pico sobre ruído de −55 dBFS → detectada), `OnsetDetector.ChunkSizeInvariant` (blocos de 1, 480 e 4096 amostras → mesmos ataques).
- [ ] **Step 2:** FAIL. **Step 3:** Implementar. **Step 4:** PASS. **Step 5:** Commit `feat: onset detector`.

### Task 6: `GridScorer`

**Files:** Create `src/score/grid_scorer.h`, `src/score/grid_scorer.cpp`, `tests/grid_scorer_test.cpp`

**Interfaces:**
- Produces:
  `struct ScoreEvent { enum Kind { Hit, Miss, Extra } kind; int64_t slot; double errorMs; double score; };`
  `using SlotTimeFn = std::function<double(int64_t slot)>;` (tempo audível, em s, do slot `k` ≥ 0 da grade)
  `class GridScorer { GridScorer(double stepSec, double compensationSec, SlotTimeFn); std::optional<ScoreEvent> onNote(double tSec); void advance(double nowSec, std::vector<ScoreEvent>& missesOut); struct Summary { double total, lastN; int hits, slots, extras; double meanErrMs, stdErrMs; }; Summary summary(size_t lastN = 16) const; };`
  Regras: `k = round((t−comp−t0)/step)`; `k<0` → ignorado (retorna `nullopt`); janela `min(0.5·step, 0.2)`; fora da janela, slot já ocupado ou já finalizado → `Extra`; `advance` finaliza slots em ordem cujo `slotTime+janela+0.1 < now−comp` como `Miss` (score 0). Nota do hit: fórmula das Global Constraints.

- [ ] **Step 1:** Testes: `GridScorer.PerfectHitScoresHundred` (erro 0 → 100; erro +10 ms → ≈ 100·(90/85 clip)=100; erro 50 ms → ≈ 58.8; erro ≥ 100 ms → 0), `GridScorer.MissAfterWindow`, `GridScorer.ExtraWhenSlotTaken`, `GridScorer.ExtraWhenFarFromGrid` (nota a 0.3 s de qualquer slot, passo 0.75 → Extra), `GridScorer.NegativeSlotIgnored`, `GridScorer.CompensationShiftsError`, `GridScorer.SummaryMatchesPrototype` (mesmos números do relatório de `treino.py` para 8 erros fixos), `GridScorer.ExtremeTempos` (BPM 30 e 240 com subdivisão 4: sem divisão por zero, janela ≤ 0.2 s).
- [ ] **Step 2:** FAIL. **Step 3:** Implementar. **Step 4:** PASS. **Step 5:** Commit `feat: grid scorer`.

### Task 7: `ClickScheduler`

**Files:** Create `src/audio/click_scheduler.h`, `src/audio/click_scheduler.cpp`, `tests/click_scheduler_test.cpp`

**Interfaces:**
- Produces: `struct BeatMark { int64_t beat; int64_t frame; };` e
  `class ClickScheduler { ClickScheduler(double sampleRate, double bpm, int countIn); void render(float* stereoInterleaved, size_t frames, int64_t firstFrame) noexcept; size_t beatsIn(int64_t firstFrame, size_t frames, BeatMark* out, size_t maxOut) const noexcept; int64_t firstBeatFrame() const noexcept; double framesPerBeat() const noexcept; };`
  Sinal de partida: tons 587.3 Hz (0.14 s) + 880 Hz (0.24 s), ataque 4 ms, decaimento `exp(−9t)`, amplitude 0.6; primeira batida da contagem em `len(sinal)+0.7 s`; clique 1200 Hz, 30 ms, `exp(−120t)`, amplitude 0.8, **uniforme**. Formas de onda pré-renderizadas no construtor; `render` não aloca.

- [ ] **Step 1:** Testes: `ClickScheduler.BeatPositions` (a batida `n` começa em `round(firstBeatFrame + n·framesPerBeat)` e `beatsIn` as reporta uma única vez em blocos de tamanhos variados), `ClickScheduler.UniformClicks` (a energia de todos os cliques é igual, incluindo o primeiro de cada "compasso"), `ClickScheduler.StartCueThenSilenceThenCount` (energia do sinal de partida na faixa esperada, silêncio de 0.7 s antes da primeira batida), `ClickScheduler.RenderDoesNotAllocate` (sobrescrever `operator new` no teste com contador; chamar `render` 1000 vezes → 0 alocações), `ClickScheduler.BlockBoundaryClicks` (clique atravessando fronteira de bloco sai contínuo: renderizar em blocos de 1, 480 e 960 frames produz saídas idênticas).
- [ ] **Step 2:** FAIL. **Step 3:** Implementar. **Step 4:** PASS. **Step 5:** Commit `feat: click scheduler`.

### Task 8: Backend abstrato, `FakeBackend` e `Session` (threads)

**Files:** Create `src/audio/backend.h`, `src/audio/fake_backend.h`, `src/audio/fake_backend.cpp`, `src/app/session.h`, `src/app/session.cpp`, `src/app/wav_writer.h`, `tests/session_test.cpp`

**Interfaces:**
- Consumes: `SpscRing`, `ClockMap`, `OnsetDetector`, `Tuner`, `GridScorer`, `ClickScheduler`.
- Produces:
  `using RenderFn = std::function<void(float* stereo, size_t frames, int64_t firstFrame)>;`
  `class IAudioBackend { virtual ~IAudioBackend(); virtual bool start(std::string& err) = 0; virtual void stop() = 0; virtual void setRenderer(RenderFn) = 0; virtual SpscRing<float>& input() = 0; virtual SpscRing<TimePoint>& inputTimes() = 0; virtual SpscRing<TimePoint>& outputTimes() = 0; virtual uint32_t xruns() const = 0; virtual bool failed(std::string& err) const = 0; };` (`input()` = mono float 48 kHz; os pontos de tempo são `{frame, tempo audível/capturado em s CLOCK_MONOTONIC}`).
  `class FakeBackend : public IAudioBackend` — gera entrada sintética programável (`scheduleNote(tSec, hz, amp)`, ruído de fundo, `simulateFailure()`), consome a saída chamando o renderizador em tempo real acelerável.
  `enum class Mode { Training, Tuner };`
  `struct SessionConfig { Mode mode; double bpm; int countIn; int subdivision; double compensationMs; double gainDb; const Instrument* instrument; Tuning tuning; double a4; };`
  `struct UiEvent { enum Type { Beat, Note, Miss, Extra, Tuner, Level, Error, Stopped } type; int64_t n; double a, b; TunerReading tuner; std::string text; };`
  `class Session { explicit Session(std::unique_ptr<IAudioBackend>); ~Session(); bool start(const SessionConfig&, std::string& err); void stop(); bool pollEvents(std::vector<UiEvent>& out); GridScorer::Summary summary() const; void recordSample(const std::string& wavPath, double seconds); };`
  Threads: análise (lê `input()`, aplica ganho, alimenta `OnsetDetector` ou `Tuner`, emite pico de nível ~20 Hz), pontuação (consome onsets e `TimePoint`s, mantém dois `ClockMap`, emite `Beat` com tempo audível, `Note/Miss/Extra`). Eventos para a UI por `SpscRing<UiEvent>`-like fila limitada sem bloquear produtores (descarta o mais antigo de `Level`). `recordSample` grava WAV 16 bit mono 48 kHz da entrada.

- [ ] **Step 1:** `session_test.cpp` com `FakeBackend`: `Session.TrainingScoresSyntheticPlayer` (BPM 120, 16 batidas tocadas com erro conhecido +12 ms ±3 → `Note` com erro dentro de ±4 ms do conhecido, média de pontuação ≥ 95, 0 faltas), `Session.SilenceAllMisses` (Review Focus 1), `Session.CompensationApplied` (compensação 12 ms zera o erro médio ±3 ms), `Session.TunerModeEmitsReadings`, `Session.BackendFailureStops` (`simulateFailure()` → evento `Error` e `Stopped`, `stop()` retorna), `Session.StartStopStress` (20 ciclos start/stop, sem deadlock, rodando também sob `-DTREINO_TSAN=ON`), `Session.RecordSampleWritesWav` (arquivo com cabeçalho válido e duração ± 50 ms), `Session.AudioThreadsNeverBlockOnUi` (UI não consome eventos por 5 s simulados → nenhuma xrun/ressincronização, fila descarta `Level`).
- [ ] **Step 2:** FAIL. **Step 3:** Implementar interfaces, `FakeBackend` e `Session`. **Step 4:** PASS (também com TSAN). **Step 5:** Commit `feat: session with analysis and scoring threads`.

### Task 9: `PipeWireBackend`

**Files:** Create `src/audio/pipewire_backend.h`, `src/audio/pipewire_backend.cpp`, `tests/pipewire_loopback_test.cpp`; Modify `CMakeLists.txt` (pkg-config `libpipewire-0.3`).

**Interfaces:**
- Consumes: `IAudioBackend`.
- Produces: `struct AudioDevice { std::string name, description; };`
  `std::vector<AudioDevice> listInputDevices(); std::vector<AudioDevice> listOutputDevices();` (nomes de nó PipeWire, ex. `alsa_input.usb-C-Media_Electronics_Inc._USB_PnP_Sound_Device-00.analog-mono`; **exclui** `.monitor` de saídas na lista de entrada principal)
  `class PipeWireBackend : public IAudioBackend { PipeWireBackend(std::string inputNode, std::string outputNode); ... };` — dois `pw_stream` (captura mono F32 48 kHz; reprodução estéreo F32 48 kHz) em loop próprio (`pw_thread_loop`); `target.object` com o nome do nó; no `process` de entrada: copia para o ring e empurra `{frame, tempo}` (de `pw_stream_get_time_n` convertido para `CLOCK_MONOTONIC`); no de saída: chama o renderizador sobre buffer do PipeWire e empurra `{frame, tempo audível}` (tempo do buffer + `delay`); `xruns` conta eventos de `pw_stream` `xrun`; falhas de conexão/estado erro ficam em `failed()`.

- [ ] **Step 1:** `pipewire_loopback_test.cpp` (pula com `GTEST_SKIP` se `pw-cli info` não responde): cria sink nulo via `pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=treino_test media.class=Audio/Sink object.linger=false }'` (ou `pactl load-module module-null-sink`), roda `Session` em modo treino com entrada `treino_test.monitor`/saída `treino_test`, BPM 120, 20 batidas, e verifica `PipeWireLoopback.MeasuresStableOffset` (todas as batidas detectadas, |média| < 25 ms, desvio < 5 ms, 0 xruns) e `PipeWireBackend.ConnectFailureReported` (nó inexistente → `start` retorna `false` com mensagem em pt-BR ou `failed()` verdadeiro em < 3 s).
- [ ] **Step 2:** FAIL (backend ausente). **Step 3:** Implementar `PipeWireBackend` e a enumeração via `pw_registry` (sincronizada com `pw_core_sync`), sem alocar nos callbacks. **Step 4:** PASS; confirmar manualmente `pw-link -l` durante a execução mostra as ligações esperadas. **Step 5:** Commit `feat: PipeWire backend and loopback test`.

### Task 10: Lotus Jam — tema, ícone, janela e treino de tempo

**Files:**
- Create: `src/main.cpp`, `src/ui/theme.h`, `src/ui/lotus_jam.qss`, `resources/icons/lotus_jam.svg` (SVG do briefing §3.2) + `resources/icons/lotus_jam_{16,32,64,128,256}.png` (gerados com `rsvg-convert`, versionados), `resources.qrc`, `src/ui/main_window.{h,cpp}`, `src/ui/level_meter.{h,cpp}`, `src/ui/pulse_widget.{h,cpp}`, `src/ui/timeline_widget.{h,cpp}`, `src/ui/score_summary_card.{h,cpp}`, `src/ui/tempo_training_view.{h,cpp}`, `tests/ui_smoke_test.cpp`
- Modify: `CMakeLists.txt` (alvo `lotus-jam`; `find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)`; `AUTOMOC` e `AUTORCC`).

**Interfaces:**
- Consumes: `Session`, `SessionConfig`, `UiEvent`, `IAudioBackend`, `FakeBackend`, `listInputDevices/OutputDevices`, `PipeWireBackend`.
- Produces: `namespace theme` em `theme.h` com todos os tokens do briefing §2 (`bg_canvas` … `zone_miss`) como `QColor`/`QString` constantes e `QFont uiFont(int px, int weight)` / `QFont monoFont(int px, bool bold)` com fallbacks;
  `using BackendFactory = std::function<std::unique_ptr<IAudioBackend>(const std::string& inNode, const std::string& outNode)>;`
  `class MainWindow : public QMainWindow { MainWindow(BackendFactory, QWidget* = nullptr); TempoTrainingView* tempoView(); TunerWidget* tunerView(); QTabWidget* tabs(); QWidget* errorBanner(); };`
  Barra superior (briefing §5.1): ícone + "LOTUS JAM" + badge do motor; abas **AFINADOR / TREINO DE TEMPO / MÚSICAS & MIDI** (a terceira mostra apenas "Em breve — Partes 2 e 3"; o Piano Roll §5.4 pertence aos planos seguintes); entrada/saída (padrão PCM2902 / HDMI), botão ↻, `LevelMeter` com escala −45…0 dBFS, ganho 0…+24 dB, compensação (ms), banner de erro `#FF1744` em pt-BR.
  `class TempoTrainingView : public QWidget` com BPM (30–240), **Contagem** (combo: 0, 1, 2 compassos = 0/4/8 batidas; padrão 1), Subdivisão (1/2/4), botão pílula `Iniciar/Parar` (`objectName "PrimaryAction"`, propriedade `running`), `Gravar amostra (10 s)`, `PulseWidget` (mesma cor em todas as batidas; agenda com `QTimer::singleShot(max(0,(a − backend.nowSec())·1000))`, Ruling R1), `TimelineWidget` (fundo `#0B0B0E`, ±120 ms, faixas 12%/10%/10% de opacidade para ≤15/15–50/>50 ms, ponto luminoso com linha de conexão, `X` para faltas, `int noteCount() const`), `ScoreSummaryCard` (Total, últimas 16, média, desvio, extras).
  `ClickScheduler` precisa aceitar `countIn == 0` (ajuste mínimo em Task 7 se faltar).

- [ ] **Step 1:** `ui_smoke_test.cpp` (`QT_QPA_PLATFORM=offscreen`, `FakeBackend` injetado): `UiSmoke.TrainingSessionWithFakeBackend` (16 notas, iniciar, ~6 s acelerados → pontuação total > 90, `timeline->noteCount() == 16`, botão volta a "Iniciar" ao parar, `grab()` salva `build/ui_smoke.png`), `UiSmoke.ErrorShownOnBackendFailure` (mensagem em pt-BR no banner, UI continua responsiva), `UiSmoke.IconTabsAndTheme` (`windowIcon()` não nulo; títulos das abas exatamente "AFINADOR", "TREINO DE TEMPO", "MÚSICAS & MIDI"; fundo da janela `#09090B`), `UiSmoke.CountInZeroSupported`.
- [ ] **Step 2:** Rodar → FAIL. **Step 3:** Implementar (QSS do briefing §6 em `lotus_jam.qss` carregado via recurso; ícone aplicado com `QApplication::setWindowIcon`). **Step 4:** PASS; abrir `ui_smoke.png` e conferir o visual. **Step 5:** Commit `feat: Lotus Jam UI shell and tempo training view`.

### Task 11: Afinador (`TunerWidget`)

**Files:** Create `src/ui/tuner_widget.h`, `src/ui/tuner_widget.cpp`; Modify `src/ui/main_window.cpp` (aba AFINADOR é a primeira); Test: `tests/ui_smoke_test.cpp`

**Interfaces:**
- Consumes: `Session` em `Mode::Tuner`, `instruments()`, `UiEvent::Tuner`, `TunerReading`, `theme`.
- Produces: `class TunerWidget : public QWidget` conforme briefing §5.2: barra de instrumento (Violão, Guitarra, Baixo 4/5/6), afinação dinâmica, A4 (`QDoubleSpinBox`, padrão 440.0, passo 0.5); mostrador `QPainter` (fundo `#0B0B0E`, borda `#262630`, marcas −50…+50 cents: grandes a cada 10, finas a cada 5, 0 destacado), ponteiro com interpolação (resposta < 100 ms), coroa de LEDs segmentados (♭ à esquerda, ♯ à direita), **Nana Glow** (ponteiro, arco e flor de lótus central em `#FF2A85` com halo `#FF6EB0` e anel pulsante) quando `voiced && |cents| ≤ 5`; nota gigante (48 pt, mono bold), `"82.41 Hz  •  +0.4 cents"`, `"Sem sinal"` quando `voiced == false` (ponteiro apagado no centro); faixa de cordas com a corda atual em borda/brilho rosa.
  Acessores de teste: `QString noteText() const; int highlightedString() const;` (**1-based; 0 = nenhuma**, Ruling R5) `double centsShown() const; bool glowActive() const; QString statusText() const; QStringList stringLabels() const;`.

- [ ] **Step 1:** Testes em `ui_smoke_test.cpp`: `UiSmoke.TunerShowsStringAndCents` (violão padrão, `FakeBackend` tocando 82.4 Hz + 15 cents → `noteText()=="E2"`, `highlightedString()==1`, `centsShown()` entre +13 e +17), `UiSmoke.TunerNoSignalMessage` (silêncio → `statusText()` igual a "Sem sinal" sem diferenciar caixa e `glowActive()==false`; também 60 Hz e 440 Hz com `baixo4`), `UiSmoke.TunerGlowWhenInTune` (82.41 Hz exato → `glowActive()==true`), `UiSmoke.TunerSwitchesInstrument` (`baixo4` → `stringLabels() == {"E1","A1","D2","G2"}`; `baixo5` → `{"B0","E1","A1","D2","G2"}`), `UiSmoke.TunerRespondsWithin100ms`.
- [ ] **Step 2:** FAIL. **Step 3:** Implementar. **Step 4:** PASS; salvar e conferir `build/ui_tuner.png` (dial em sintonia e em "Sem sinal"). **Step 5:** Commit `feat: Lotus Jam tuner`.

### Task 12: Configurações, lançador e paridade com o protótipo

**Files:** Create `src/app/settings.h`, `src/app/settings.cpp`, `packaging/lotus-jam.desktop` (Name=Lotus Jam, Icon=lotus-jam, Exec=lotus-jam), `README.md` (título Lotus Jam), `docs/superpowers/notes/parte1-paridade.md`, `tests/settings_test.cpp`; Modify `src/ui/main_window.cpp`

**Interfaces:**
- Produces: `struct Settings { std::string inputNode, outputNode; double bpm, compensationMs, gainDb, a4; std::string instrumentId, tuningName; };` e `Settings loadSettings(QSettings&); void saveSettings(QSettings&, const Settings&); double compensationFor(QSettings&, in, out); void setCompensationFor(QSettings&, in, out, double);` (compensação guardada por par entrada|saída).

- [ ] **Step 1:** Testes: `Settings.RoundTrip`, `Settings.CompensationPerDevicePair` (valores diferentes para dois pares), `Settings.DefaultsMatchPrototype` (entrada PCM2902, saída HDMI, BPM 80).
- [ ] **Step 2:** FAIL. **Step 3:** Implementar; ligar à janela (carrega ao abrir, salva ao iniciar/fechar; trocar de dispositivo recarrega a compensação do par). Escrever o `README.md` (como compilar, rodar, e como usar a compensação) e a lista de paridade com `~/metronomo` (sinal de partida, clique uniforme, pontuação, gráfico, seletores, medidor) marcando cada item como verificado.
- [ ] **Step 4:** `ctest --test-dir build` completo (+ TSAN) → tudo PASS; rodar `build/lotus-jam` na máquina com o baixo real (a verificação com o baixo real fica **PENDENTE**, Ruling R3: o usuário a executa) e deixar registrado em `docs/superpowers/notes/parte1-paridade.md` o resultado: notas detectadas? nível do sinal? compensação medida.
- [ ] **Step 5:** Commit `feat: settings, launcher and parity notes`.

---

## Auto-revisão

- **Cobertura do spec (parte 1):** treino de tempo (Tasks 5–7, 10), afinador (3–4, 11), multithread/RT (1, 7–9), clock/latência (2, 8, 12), "gravar amostra" e ganho (8, 10), seletores de dispositivo (9–10), configurações por par (12), critérios de aceitação de tempo/cents/xrun (4, 5, 9, 8). Partes 2–4 (partitura, áudio, refinos) ficam para planos próprios.
- **Consistência de tipos:** `TimePoint` (Task 2) usado em 8–9; `ScoreEvent`/`GridScorer::Summary` (6) usados em 8 e 10; `StringMatch/Tuning/Instrument` (3) usados em 4, 8, 11.
- **Proporção:** passos trazem assinaturas, nomes de teste e valores do spec; corpos de função ficam para quem implementa.
