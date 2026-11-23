# Parte 1 — paridade com o protótipo (`~/metronomo`) e validação pendente

Protótipo de referência: `~/metronomo` (`treino.py`, `engine.py`, `gui.py`, Python/PyQt).
Cada item abaixo está **verificado por teste** (nome do teste; os de interface rodam offscreen
com `FakeBackend`) ou **PENDENTE**.

## Checklist de paridade

| Item do protótipo | Lotus Jam | Estado |
|---|---|---|
| Sinal de partida (587,3 Hz + 880 Hz), silêncio, depois contagem | `ClickScheduler` | verificado: `ClickScheduler.StartCueThenSilenceThenCount` |
| Clique uniforme, **sem acento** (todas as batidas iguais) | `ClickScheduler` | verificado: `ClickScheduler.UniformClicks`, `ClickScheduler.BeatPositions`, `ClickScheduler.NonIntegerBeatLengthAndZeroCountIn` |
| Clique gerado sem alocação no callback de tempo real | `ClickScheduler::render` | verificado: `ClickSchedulerRT.RenderDoesNotAllocate` |
| Contagem (protótipo: 1–8 batidas, padrão 4) | 0, 4 ou 8 batidas (sem/1/2 compassos), padrão 4 | verificado: `UiSmoke.CountInZeroSupported`, `GridScorer.NegativeSlotIgnored` (notas na contagem não contam), `Settings.DefaultsMatchPrototype` |
| Subdivisão 1/2/4 por batida | igual | verificado: `Session.TrainingScoresSyntheticPlayer` (grade), `Settings.RoundTrip` |
| Fórmula da nota: `100·clip((100−|erro|)/85, 0, 1)`; janela = meio passo (≤ 200 ms); extras; faltas = 0 | `GridScorer` | verificado: `GridScorer.SummaryMatchesPrototype` (valores gerados com `pontuar()`/`relatorio()` do protótipo), `GridScorer.PerfectHitScoresHundred`, `GridScorer.MissAfterWindow`, `GridScorer.ExtraWhenSlotTaken`, `GridScorer.ExtraWhenFarFromGrid` |
| Resumo: total, últimas 16, média do erro (+ = atrasa), regularidade (desvio), acertos/extras | cartão de resumo | verificado: `GridScorer.SummaryMatchesPrototype`, `UiSmoke.TrainingSessionWithFakeBackend` |
| Zonas de cor (protótipo: faixas ±15/±50/±100 ms no gráfico; total verde ≥ 80, amarelo ≥ 50) | `theme::zone_exact/warn/miss` (≤ 15 / ≤ 50 / > 50 ms), linha do tempo | verificado (desenho): `UiSmoke.TrainingSessionWithFakeBackend` (captura `ui_smoke_running.png`); cores conferidas visualmente na Task 10 |
| Gráfico/linha do tempo com o erro de cada nota e as faltas | `TimelineWidget` | verificado: `UiSmoke.TrainingSessionWithFakeBackend` (16 notas, 0 faltas), `Session.SilenceAllMisses` |
| Compensação de latência subtraída do tempo tocado | `GridScorer`, campo COMPENSAÇÃO | verificado: `GridScorer.CompensationShiftsError`, `Session.CompensationApplied` |
| Compensação salva (protótipo: um valor global `atraso`) | por par entrada/saída | verificado: `Settings.CompensationPerDevicePair`, `UiSmoke.CompensationFollowsDevicePair` |
| Seletores de entrada/saída, ↻, padrão PCM2902 / HDMI | combos do cabeçalho, `device_select` | verificado: `UiSmoke.DeviceDefaults`, `UiSmoke.DeviceCombosReadable`, `UiSmoke.StartWaitsForDeviceScan`, `UiSmoke.DeviceScanDuringSessionKeepsIt`, `PipeWireDevices.ListsNullSinkAndExcludesMonitors` |
| Dispositivos e BPM lembrados entre execuções (protótipo: QSettings `entrada`, `saida`, `bpm`, `atraso`) | `~/.config/lotus-jam/lotus-jam.conf` (também contagem, subdivisão, ganho, afinador) | verificado: `Settings.RoundTrip`, `UiSmoke.SettingsAppliedOnStartup`, `UiSmoke.SettingsSavedOnStartAndClose` |
| Medidor de nível da entrada (protótipo: −60…0 dB, barra) | `LevelMeter` −45…0 dBFS com marca do piso da PCM2902 | verificado: `UiSmoke.NumbersFollowLocale` (leitura), `UiSmoke.UiThreadDoesNoHeavyWork` (eventos Level). Resposta com o baixo: **PENDENTE** (item 2 abaixo) |
| Gravar amostra da entrada (protótipo: `_gravar`) | “Gravar amostra” (10 s, WAV) | verificado: `Session.RecordSampleWritesWav`, `Session.RecordSampleFailureReportsError`, `UiSmoke.RecordingOutcomeShown` |
| Detector de ataques (RMS 5 ms, salto > 6 dB, refratário 120 ms) | `OnsetDetector` (+ piso adaptativo R8–R13) | verificado com sinais sintéticos: `OnsetDetector.*`, `LowNotes.*`, `LongPlaying.*`; com o baixo real: **PENDENTE** |
| Relógio: carimbos do áudio → mapa linear (protótipo: `Mapa`/`polyval`) | `ClockMap` por direção | verificado: `ClockMap.*`, `Session.BeatTimesMatchOutputGeometry`, `PipeWireLoopback.MeasuresStableOffset` (null sink). Viés absoluto em hardware real: **PENDENTE** |
| Erro de áudio para a sessão e avisa (protótipo: `vigia`) | evento Error/Stopped, selo FALHA | verificado: `Session.BackendFailureStops`, `UiSmoke.ErrorShownOnBackendFailure`, `UiSmoke.BadgeFailedOnlyOnDeviceFailure`, `PipeWireBackend.FailedAfterDeviceRemoved` |
| Afinador (não existia no protótipo) | aba AFINADOR | verificado com sinais sintéticos: `Tuner.*`, `Yin.*`, `UiSmoke.Tuner*`; com o baixo real: **PENDENTE** |

## Validação com o baixo real (PENDENTE — a ser feita pelo usuário)

Ruling R3: esta parte não pode ser automatizada (exige o baixo e a PCM2902 conectados). Rodar
`build/lotus-jam` e anotar o resultado de cada passo aqui.

1. **Entrada padrão.** Com a PCM2902 conectada e nada salvo, ela aparece selecionada em
   ENTRADA (e a saída HDMI em SAÍDA)? — resultado: _____
2. **Medidor de nível.** Em AFINADOR ou TREINO, com o baixo parado o medidor fica perto do piso
   (≈ −39 dBFS, marca no medidor)? Tocando, a barra sobe com clareza (quantos dB acima do
   piso)? Precisa de ganho? — resultado: _____
3. **Detecção de notas.** Em TREINO DE TEMPO, tocar E1, A1, D2 e G2 (cordas soltas) a 80 BPM e
   a 120 BPM, ~30 s cada: todas as notas aparecem na linha do tempo? Há faltas ou extras sem
   motivo? Testar também jogo contínuo > 15 s (R10/R13). — resultado: _____
4. **Compensação.** Com compensação 0 ms, tocar junto ~30 s com cuidado e anotar a média do
   erro; colocar esse valor em Compensação (mesmo sinal: +18 ms de média → +18), repetir e
   conferir que a média fica perto de 0. Valor que zera a média para PCM2902 → HDMI: _____ ms
   (fica salvo para esse par).
5. **Afinador.** Em cada uma das 4 cordas (E1, A1, D2, G2), com “Baixo 4 cordas” / Padrão: a
   nota e a oitava estão certas (principalmente E1 ≈ 41,2 Hz, sem saltar para E2)? O ponteiro
   fica estável (quantos cents oscila)? Responde em < 100 ms ao tocar? — resultado: _____
6. **“Sem sinal”.** Com o baixo abafado (só o ronco de 60 Hz/harmônicos) aparece “Sem sinal” ou
   uma nota falsa (A1 +150 cents)? Uma corda desafinada > 1 semitom aparece como “Sem sinal”
   (regra R7)? — resultado: _____
7. **Amostra.** Em TREINO DE TEMPO, clicar “Gravar amostra” e tocar por 10 s; guardar o WAV
   (`~/.local/share/lotus-jam/amostras/`) para testes de regressão (detector e afinador com
   sinal real). Arquivo: _____
8. **Desconectar o USB.** Com uma sessão rodando, desconectar a PCM2902: a sessão para com o
   selo FALHA e uma mensagem? Reconectar e clicar em ↻: ela volta a ser selecionada (é a
   salva)? O afinador reinicia sem travar? — resultado: _____

### Pontos a revisitar com o resultado da validação

- **R7 (afinador):** a regra “mais de 100 cents de todas as cordas = Sem sinal” separa o ronco de
  60 Hz (lido como A1 +150,6 cents) de uma nota, mas faz uma corda desafinada > 1 semitom
  aparecer como “Sem sinal”. Revisitar com o baixo real: possível gate de amplitude + rejeição de
  50/60 Hz em vez da regra de cents. Também: harmônicos do ronco (120/180 Hz) caem na janela de
  captura em “Meio-tom abaixo” (baixo6 B2 ≈ 123,5 Hz, violão F#3 ≈ 185 Hz); tolerância de oitava
  fixa (0,03): fundamental < ~0,1 do 2º harmônico ainda é lida uma oitava acima.
- **R13 (detector de ataques, limites em `src/dsp/onset.h`):** re-toques sobre a cauda sem subida
  > 6 dB não são detectados (e os detectados chegam 2–4 ms atrasados); notas < ~14 dB acima do
  piso não são detectadas; após queda de ~20 dB no volume há ponto cego de até ~10 s; ventilador
  ou ronco que sobem durante o jogo podem gerar ataques falsos (41–66 por 120 s no pior caso
  sintético); sessão que começa no meio de jogo contínuo sem silêncio colapsa (a contagem dá o
  silêncio). Ideia adiada: medir o piso nos intervalos entre ataques.
- **Task 9 (backend):** viés absoluto do carimbo de tempo do PipeWire em hardware (delay/buffered
  = 0 no null sink; até 1–2 quanta, 5–11 ms a 256 frames) — constante, absorvido pela compensação
  do passo 4; anotar o valor encontrado.
