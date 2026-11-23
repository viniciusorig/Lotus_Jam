# Lotus Jam 🎸

Treinador musical em tempo real para **baixo**, **guitarra** e **violino**, para Linux (PipeWire).

**Funcionalidades principais:**
- 🎯 **Treino de tempo** — toque junto com o metrônomo e veja seu erro em milissegundos
- 🎵 **Treino com MIDI** — importe uma música MIDI, pratique e receba feedback de pitch e timing
- 🎤 **Transcrição** — grave sua performance e gere um arquivo MIDI automaticamente
- 🎼 **Afinador cromático** — afine seu instrumento com precisão de ±1 cent

## Screenshots

```
┌─────────────────────────────────────────────────────────────┐
│  🪷 Lotus Jam          [Entrada ▼] [Saída ▼] ↻  ▁▃▅▇ 0dB   │
├─────────────────────────────────────────────────────────────┤
│  AFINADOR │ TREINO DE TEMPO │ MÚSICAS & MIDI                │
├─────────────────────────────────────────────────────────────┤
│                         E2                                  │
│                      82,41 Hz                               │
│              ◀──────────│──────────▶                        │
│                      +2,3 cents                             │
│                                                             │
│  [1] [2] [3] [4]  ← cordas do baixo                        │
└─────────────────────────────────────────────────────────────┘
```

## Compilar

**Dependências:** CMake ≥ 3.20, Ninja, compilador C++20, Qt 6 (Widgets, Svg), PipeWire e GoogleTest.

```sh
# Arch Linux
sudo pacman -S cmake ninja qt6-base qt6-svg pipewire gtest

# Ubuntu/Debian
sudo apt install cmake ninja-build qt6-base-dev libqt6svg6-dev libpipewire-0.3-dev libgtest-dev

# Compilar
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

## Rodar

```sh
./build/lotus-jam
```

**Instalar** (atalho no menu e ícone):
```sh
cmake --install build --prefix ~/.local
```

## As abas

### 🎼 AFINADOR

Liga automaticamente ao entrar na aba. Mostra nota, frequência, desvio em cents e a corda mais próxima. O mostrador acende ("Nana Glow") quando está afinado (±5 cents).

**Instrumentos:** baixo (4, 5, 6 cordas), guitarra, violão  
**Afinações:** Padrão, Drop D, Meio-tom abaixo, DADGAD, **Cromático** (todas as notas)

**Transcrever:** clique em **Transcrever**, toque, clique em **Parar** → gera arquivo MIDI da sua performance (monofônico).

### 🥁 TREINO DE TEMPO

Toque junto com o metrônomo e veja seu timing nota a nota.

| Cor | Significado |
|-----|-------------|
| 🟢 Verde | ≤ 15 ms |
| 🟡 Amarelo | ≤ 50 ms |
| 🔴 Vermelho | > 50 ms ou nota perdida |

**Configurações:** BPM (30–240), contagem (0–8 tempos), subdivisão (1, 2, 4)  
**Calibrar:** toque ~30s com cuidado, clique em **Calibrar** → ajusta a compensação automaticamente

### 🎵 MÚSICAS & MIDI

Importe um arquivo MIDI, selecione a trilha e pratique junto com a música.

1. **Importar MIDI…** → escolha o arquivo .mid
2. Selecione a **trilha** com as notas que quer praticar
3. Ajuste a **velocidade** (50%–120%)
4. **Iniciar** → toque junto!

**Feedback em tempo real:**
- 🟢 Hit: nota certa no tempo certo (±60 ms, ±50 cents)
- 🟡 Early/Late: nota certa, timing fora
- 🔴 Miss: nota não tocada
- ⚫ Wrong: nota errada

**Backing track:** ondas senoidais + clique de metrônomo no tempo do MIDI.  
**Resumo:** ao final, mostra acertos, erros e pontuação.

## Compensação de latência

O sistema de áudio tem um atraso que varia conforme o hardware. A **compensação** (em ms) corrige isso.

**Calibração automática:**
1. Deixe compensação em 0 ms
2. Em TREINO DE TEMPO, toque 30s com cuidado
3. Clique em **Calibrar**

A compensação é salva **por par entrada/saída**.

## Configurações

`~/.config/lotus-jam/lotus-jam.conf` (formato INI):
- Dispositivos, BPM, contagem, subdivisão
- Ganho, compensação por par de dispositivos
- Instrumento, afinação, A4

## Arquitetura

```
src/
├── core/       # relógio, anel SPSC
├── dsp/        # YIN, afinador, onset detector, transcrição, MIDI parser/writer
├── score/      # pontuação (GridScorer, NoteScorer)
├── audio/      # backend PipeWire, FakeBackend, metrônomo, sintetizador
├── app/        # Session, settings
└── ui/         # Qt 6 Widgets
```

**Threads:**
- **PipeWire callbacks** (RT): só copia/gera áudio
- **Análise**: ganho, onset/pitch detection
- **Pontuação**: converte samples→tempo, scoring
- **Interface**: só desenha (nunca bloqueia)

Comunicação via filas SPSC lock-free.

## Testes

```sh
ctest --test-dir build --output-on-failure

# Pular testes do PipeWire (CI sem áudio)
ctest --test-dir build -E PipeWire

# Com ThreadSanitizer
cmake -S . -B build-tsan -G Ninja -DTREINO_TSAN=ON
cmake --build build-tsan && ctest --test-dir build-tsan
```

## Limitações

- **Transcrição:** apenas monofônico (uma nota por vez). Acordes não são suportados.
- **Backing track:** ondas senoidais simples, não samples reais. Para áudio realista, seria necessário FluidSynth + soundfonts.
- **Validação hardware:** medido com sinais sintéticos; validação com instrumento real pendente.

## Licença

MIT

## Créditos

Desenvolvido com auxílio do Claude (Anthropic).
