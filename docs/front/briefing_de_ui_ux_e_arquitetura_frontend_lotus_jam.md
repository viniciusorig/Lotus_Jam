# Briefing de Design, UI/UX e Arquitetura Frontend — **Lotus Jam**

> **Destinatário:** Agente de Engenharia de Software / Frontend C++ (Qt 6.12)  
> **Projeto:** **Lotus Jam** — Software de Treino Musical, Afinador de Alta Precisão e Leitor de Partituras/MIDI/Áudio para Baixo, Guitarra e Violão  
> **Stack Obrigatória:** C++20, CMake + Ninja, **Qt 6.12 Widgets (`QPainter` + `QSS`)**, Linux (PipeWire nativo via `treino_core`).  
> **Idioma da Interface:** 100% em Português do Brasil (`pt-BR`).

---

## 1. Visão Geral e Direção Criativa (*Nana* Punk-Rock Dark Mode)

O **Lotus Jam** une engenharia de áudio de baixíssima latência (C++ multithread) a uma interface visceral, moderna e contrastante inspirada na estética visual do anime ***Nana*** (especificamente a tatuagem de flor de lótus de **Nana Osaki** e a atmosfera punk-rock de palco/estúdio noturno).

### Pilares Visuais
1. **Preto Fosco Profundo (Matte Obsidian):** Superfícies escuras sem reflexos cansativos para longas sessões de estudo noturno (`#09090B` e `#111115`), fazendo os elementos ativos saltarem na tela.
2. **Rosa Vibrante & Magenta Elétrico (Nana Lotus Palette):** O rosa intenso da flor de lótus de Nana Osaki (`#FF2A85` / `#FF007F`) atua como cor primária de energia, afinação exata, notas MIDI e foco ativo.
3. **Precisão Cirúrgica em 60 FPS:** Números sempre alinhados com fontes monoespaçadas tabulares (`tabular-nums`) para que leituras de **BPM**, **ms**, **Hz** e **cents** jamais tremam durante a execução.
4. **Zero Poluição Sonora/Visual:** Durante o treino ativo, a interface entrega feedback visual instantâneo e silencioso (nenhum bipe extra além do clique do metrônomo e do sinal de partida).

---

## 2. Design Tokens (Paleta de Cores, Tipografia e Estilo)

Implementar um arquivo central de tema (`src/ui/theme.h` + `src/ui/lotus_jam.qss`) com os seguintes tokens:

### 2.1 Paleta de Cores

| Token | Hex | Uso na Interface |
| :--- | :--- | :--- |
| `bg_canvas` | `#09090B` | Fundo principal da janela, Piano Roll e dial do afinador (preto fosco) |
| `bg_surface` | `#111115` | Cards, painéis laterais e barra superior de dispositivos |
| `bg_elevated` | `#191920` | Inputs, dropdowns, botões secundários e hover states |
| `border_subtle` | `#262630` | Divisórias finas e grades de fundo |
| `border_punk` | `#3F1D32` | Bordas de destaque com subtom magenta |
| `text_primary` | `#F4F4F6` | Títulos, valores principais (nota, BPM, pontuação) |
| `text_secondary` | `#9E9EA9` | Rótulos, legendas, frequências secundárias |
| `text_muted` | `#5C5C68` | Estado `"Sem sinal"`, marcadores inativos |
| `nana_pink` | `#FF2A85` | **Cor assinatura:** ponteiro do afinador, notas MIDI principais, botões primários |
| `nana_glow` | `#FF6EB0` | Brilho/halo (`radialGradient`) quando a nota crava na afinação exata |
| `magenta_deep` | `#B80D57` | Notas MIDI secundárias, barras de velocidade baixa, gradientes |
| `magenta_neon` | `#E11D74` | Playhead (agulha do Piano Roll) e seleção de trilha ativa |
| `zone_exact` | `#00F5A0` | Zona de precisão máxima no treino de tempo (`≤ 15 ms`) |
| `zone_warn` | `#FFB800` | Zona de atenção/desvio moderado (`≤ 50 ms`) |
| `zone_miss` | `#FF1744` | Erro grave (`> 50 ms`), faltas (`X`) e alertas de falha de backend |

### 2.2 Tipografia
- **Interface & Títulos:** `"Inter"`, `"Noto Sans"`, sans-serif (pesos `500`, `600`, `700`).
- **Leituras Numéricas & Notas (BPM, ms, Hz, cents, Nota):** `"JetBrains Mono"`, `"Fira Code"`, monospace com `QFont::Monospace` para evitar variação de largura a 60 Hz.

---

## 3. Ícone do App (`Lotus Jam`)

O ícone oficial do aplicativo deve ser uma **flor de lótus rosa estilizada com traços geométricos e ondas sonoras integradas às pétalas**, sobre um emblema escuro arredondado (`squircle`).

### 3.1 Especificação de Entrega
- Criar `resources/icons/lotus_jam.svg` (vetorial escalável) e renderizar variantes PNG (`16x16`, `32x32`, `64x64`, `128x128`, `256x256`) empacotadas via `resources.qrc` e referenciadas no arquivo `.desktop` (`packaging/lotus-jam.desktop`).
- Aplicar como ícone da janela principal (`QApplication::setWindowIcon(QIcon(":/icons/lotus_jam.svg"))`).

### 3.2 Código Base Vetorial (SVG de Referência)

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <defs>
    <linearGradient id="bgGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#141419"/>
      <stop offset="100%" stop-color="#08080A"/>
    </linearGradient>
    <linearGradient id="lotusPink" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="#FF5CA8"/>
      <stop offset="55%" stop-color="#FF2A85"/>
      <stop offset="100%" stop-color="#9D0B48"/>
    </linearGradient>
    <linearGradient id="petalSide" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#FF2A85" stop-opacity="0.9"/>
      <stop offset="100%" stop-color="#6E0733" stop-opacity="0.95"/>
    </linearGradient>
    <filter id="neonGlow" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur stdDeviation="4" result="blur"/>
      <feMerge>
        <feMergeNode in="blur"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>

  <!-- Base Preto Fosco -->
  <rect x="12" y="12" width="232" height="232" rx="52" fill="url(#bgGrad)" stroke="#2E1524" stroke-width="4"/>

  <!-- Anel de Onda Sonora Sutil -->
  <circle cx="128" cy="132" r="86" fill="none" stroke="#FF2A85" stroke-opacity="0.18" stroke-width="2" stroke-dasharray="6 6"/>

  <!-- Pétalas Externas Geométricas -->
  <path d="M128 178 C84 176, 44 148, 38 106 C68 112, 96 134, 114 166 Z" fill="url(#petalSide)"/>
  <path d="M128 178 C172 176, 212 148, 218 106 C188 112, 160 134, 142 166 Z" fill="url(#petalSide)"/>

  <!-- Pétalas Intermediárias -->
  <path d="M128 176 C92 162, 70 124, 76 78 C102 96, 118 128, 124 168 Z" fill="url(#lotusPink)" opacity="0.92"/>
  <path d="M128 176 C164 162, 186 124, 180 78 C154 96, 138 128, 132 168 Z" fill="url(#lotusPink)" opacity="0.92"/>

  <!-- Pétala Central com Barras de Onda Sonora Integradas -->
  <path d="M128 44 C104 82, 102 132, 128 176 C154 132, 152 82, 128 44 Z" fill="url(#lotusPink)" filter="url(#neonGlow)"/>

  <!-- Ondas Sonoras / Equalizador Recortado no Centro da Lótus -->
  <g stroke="#09090B" stroke-width="4.5" stroke-linecap="round">
    <line x1="128" y1="78" x2="128" y2="146"/>
    <line x1="118" y1="96" x2="118" y2="134"/>
    <line x1="138" y1="96" x2="138" y2="134"/>
    <line x1="108" y1="110" x2="108" y2="124"/>
    <line x1="148" y1="110" x2="148" y2="124"/>
  </g>

  <!-- Base Geométrica da Lótus -->
  <path d="M88 186 L168 186 L154 196 L102 196 Z" fill="#FF2A85" opacity="0.85"/>
</svg>
```

---

## 4. Arquitetura Frontend e Contrato com o Backend (`treino_core`)

### 4.1 Regras Invioláveis de Performance e Threading
- **Thread de UI (Qt) nunca executa DSP ou I/O bloqueante.**
- A `MainWindow` mantém um `QTimer` de **16 ms (~60 Hz)** que chama `session_->pollEvents(events)` e despacha os `UiEvent` para o widget ativo.
- Todos os widgets customizados (`TunerWidget`, `PulseWidget`, `TimelineWidget`, `LevelMeter`, `PianoRollWidget`) devem implementar `paintEvent` com `QPainter::Antialiasing` usando objetos pré-alocados (pens/brushes/gradients cacheados) para garantir **0 xruns** no PipeWire mesmo sob carga gráfica.
- **Injeção de Dependência para Testes:** O construtor da `MainWindow` **deve aceitar** uma factory `std::function<std::unique_ptr<IAudioBackend>(const std::string& inNode, const std::string& outNode)>` para permitir injeção do `FakeBackend` nos testes automatizados (`ui_smoke_test.cpp` rodando sob `QT_QPA_PLATFORM=offscreen`).

### 4.2 Estrutura de Arquivos Recomendada (`src/ui/`)
```text
src/ui/
├── theme.h                  # Paleta de cores, fontes e constantes visuais
├── lotus_jam.qss            # Folha de estilo global Qt (Dark Mode Punk)
├── main_window.h/.cpp       # Janela principal, header global de áudio e navegação por abas
├── level_meter.h/.cpp       # Medidor de entrada dBFS em tempo real (otimizado p/ sinal fraco PCM2902)
├── tuner_widget.h/.cpp      # Aba 1/2: Afinador com mostrador escuro e brilho rosa Nana Osaki
├── pulse_widget.h/.cpp      # Indicador visual da batida sincronizado com tempo audível
├── timeline_widget.h/.cpp   # Gráfico das últimas 32 notas no treino de tempo
└── piano_roll_widget.h/.cpp # Aba 3: Leitor de MIDI, Faixas e Treino de Música (Song/Track)
```

---

## 5. Especificação Detalhada das Telas e Widgets

### 5.1 Barra Superior Global (Header & Controle de Áudio PipeWire)
Presente no topo da `MainWindow` em todas as abas:
1. **Identidade:** Ícone SVG da flor de lótus + logotipo **LOTUS JAM** (com badge discreto de status do motor).
2. **Navegação Principal (Abas Estilizadas):**
   - `AFINADOR` (Tuner)
   - `TREINO DE TEMPO` (Metrônomo & Precisão Rítmica)
   - `MÚSICAS & MIDI` (Piano Roll / Partitura / Áudio Transcrito)
3. **Painel de Dispositivos & Calibração:**
   - **Entrada:** `QComboBox` populado por `listInputDevices()` (padrão: `PCM2902`, excluindo `.monitor`).
   - **Saída:** `QComboBox` populado por `listOutputDevices()` (padrão: `HDMI` / `Fuxi-H3`).
   - **Botão Atualizar (`↻`):** Recarrega dispositivos PipeWire sem reiniciar o app.
   - **Medidor de Nível (`LevelMeter`):** Barra horizontal/vertical em `dBFS` atualizada pelos eventos `UiEvent::Level` (~20 Hz). Deve ter escala visual clara na região de sinal fraco (`-45 dBFS` a `0 dBFS`), pois o baixo no `PCM2902` opera com ruído de fundo em torno de `-39 dBFS`.
   - **Ganho Digital (`dB`):** `QDoubleSpinBox` / slider compacto (`0.0` a `+24.0 dB`).
   - **Compensação de Latência (`ms`):** `QDoubleSpinBox` carregado e salvo automaticamente por par `(entrada, saída)` via `compensationFor` / `setCompensationFor` em `QSettings`.
4. **Banner de Erro (`UiEvent::Error`):** Faixa discreta em vermelho carmesim (`#FF1744`) que aparece no topo caso o dispositivo USB desconecte ou o PipeWire reporte falha, exibindo a mensagem em `pt-BR` sem travar a interface.

---

### 5.2 Aba: O Afinador (`TunerWidget`) — *Estética Nana Osaki*

O Afinador é a peça central da identidade visual do **Lotus Jam**.

#### Layout e Componentes
1. **Barra de Configuração do Instrumento:**
   - **Instrumento:** `Violão`, `Guitarra`, `Baixo 4 cordas`, `Baixo 5 cordas`, `Baixo 6 cordas` (consome `instruments()`).
   - **Afinação:** `Padrão`, `Drop D`, `Meio-tom abaixo`, `DADGAD` (atualizado dinamicamente conforme o instrumento).
   - **Referência A4:** `QDoubleSpinBox` ajustável (padrão `440.0 Hz`, passo `0.5 Hz`).
2. **Mostrador Principal (`QPainter` Custom Dial):**
   - **Fundo do Mostrador:** Círculo/arco escuro em preto fosco (`#0B0B0E`) com borda metálica sutil (`#262630`) e marcações radiais de `-50` a `+50 cents` (traços maiores a cada `10 cents`, traços finos a cada `5 cents`, e marcação central `0 cents` destacada).
   - **Ponteiro & Arco de Luzes em Rosa Vibrante (`#FF2A85`):**
     - O ponteiro físico/digital desliza suavemente com interpolação visual (`< 100 ms` de resposta) indicando o desvio em `cents` (`TunerReading::cents`).
     - Abaixo do arco, uma coroa de **LEDs segmentados** acompanha o desvio: quando está longe (`|cents| > 15`), acendem segmentos em magenta escuro/âmbar indicando se deve apertar (`♭` à esquerda) ou afrouxar (`♯` à direita) a tarraxa.
   - **Estado de Afinação Exata (Efeito *Nana Glow*):**
     - Quando `voiced == true` e `|cents| <= 5.0` (com precisão interna `< 1 cent`), o mostrador entra em **modo de ressonância**:
       - O ponteiro, o arco central e o ícone de flor de lótus no centro do dial acendem em **rosa vibrante (`#FF2A85`) com halo luminoso (`#FF6EB0`)**.
       - Um anel externo sutil pulsa suavemente confirmando que a corda cravou na afinação exata.
   - **Leitura Central:**
     - **Nota Gigante:** Ex.: `E2`, `B0`, `A2` (fonte `48pt` bold).
     - **Frequência & Desvio:** Ex.: `82.41 Hz  •  +0.4 cents`.
     - **Estado Sem Sinal (`voiced == false`):** Quando há silêncio, ruído de 60 Hz ou frequência fora da faixa do instrumento (`[minHz, maxHz]`), o ponteiro repousa no centro apagado e exibe claramente o texto `"Sem sinal"` (requisito obrigatório dos testes automatizados).
3. **Seletor Visual de Cordas (Bottom Strip):**
   - Exibe botões/indicadores individuais para cada corda da afinação atual (ex. para `Baixo 5`: `B0`, `E1`, `A1`, `D2`, `G2`; para `Violão`: `E2`, `A2`, `D3`, `G3`, `B3`, `E4`).
   - A corda correspondente a `TunerReading::stringIndex` acende com borda e brilho rosa `#FF2A85` enquanto tocada.

---

### 5.3 Aba: Treino de Tempo (`TempoTrainingView`)

Mantém 100% de compatibilidade funcional e de teste com a Parte 1 do plano, mas vestido na estética **Lotus Jam**.

1. **Controles de Sessão:**
   - `BPM` (`30` a `240`), `Contagem` (`0`, `1`, `2` compassos), `Subdivisão` (`1`, `2`, `4` por batida).
   - Botão Principal **`Iniciar` / `Parar`** (estilo pílula rosa vibrante `#FF2A85` quando pronto para iniciar; contorno carmesim/escuro quando rodando).
   - Botão **`Gravar amostra (10 s)`**: dispara `session_->recordSample(...)` e exibe indicador de gravação WAV para diagnóstico do baixo.
2. **Indicador de Batida (`PulseWidget`):**
   - Círculo geométrico no centro superior que pisca **exatamente no instante audível** da batida (agendado via `QTimer::singleShot` usando o timestamp `a` do `UiEvent::Beat`).
   - **Clique uniforme sem acento:** Todas as batidas do metrônomo têm exatamente a mesma intensidade visual rosa-magenta (após os dois tons do sinal de partida e a contagem).
3. **Gráfico das Últimas 32 Notas (`TimelineWidget`):**
   - Fundo preto fosco `#0B0B0E` com eixo central `0 ms` e escala vertical de `-120 ms` (adiantou) a `+120 ms` (atrasou).
   - Faixas horizontais translúcidas respeitando exatamente as regras do domínio:
     - **Faixa Central (`≤ 15 ms`):** Verde-neon (`#00F5A0` com 12% de opacidade) — acerto cirúrgico.
     - **Faixa Intermediária (`15–50 ms`):** Âmbar (`#FFB800` com 10% de opacidade) — aceitável.
     - **Faixa Externa (`> 50 ms`):** Vermelho punk (`#FF1744` com 10% de opacidade) — fora do tempo.
   - Cada nota detectada (`UiEvent::Note`) é desenhada como um ponto luminoso com linha de conexão; faltas (`UiEvent::Miss`) são desenhadas como um **`X`** vermelho na linha do tempo.
4. **Painel de Pontuação (`ScoreSummaryCard`):**
   - Exibe em cards de alto contraste:
     - **Pontuação Total** (`0–100%`)
     - **Últimas 16 Notas** (`0–100%`)
     - **Média de Erro** (`ms`) & **Desvio Padrão** (`ms`)
     - **Notas Extras** (`> 200 ms` de qualquer batida, contadas separadamente)

---

### 5.4 Aba: Leitor de MIDI, Faixas e Treino de Música (`PianoRollView`)

Esta tela atende às **Partes 2 e 3** do design (importação de partituras MIDI/MusicXML/Guitar Pro e áudio transcrito) e deve ser construída sobre o modelo interno `Song` e `Track`.

#### Estética Visual do Piano Roll
- **Fundo Preto Fosco (`#09090B`):** Grade rítmica minimalista com linhas de compasso em `#2A2A35` e subdivisões em `#16161C`.
- **Régua Lateral de Notas (Teclado Vertical):** Teclas escuras (`#141418`) e teclas acidentais (`#09090B`) com rótulos de oitava (`E1`, `A1`, `D2`, `G2` destacadas para baixistas/guitarristas).
- **Notas no Piano Roll em Tons de Rosa e Magenta:**
  - As notas da trilha ativa são renderizadas como barras horizontais com cantos levemente chanfrados/arredondados em degradê de **Rosa Nana (`#FF2A85`) a Magenta Elétrico (`#B80D57`)**, com borda superior iluminada (`#FF70B8`).
  - Se a `Track` vier de Guitar Pro / MusicXML com informação de corda/casa (`cordas?`), exibir o número da casa dentro ou acima da barra em fonte monoespaçada limpa.
  - **Playhead (Agulha de Reprodução):** Linha vertical magenta-neon (`#FF2A85`, largura `2px`) com rastro luminoso suave à esquerda.

#### Controles e Funcionalidades Específicas (Conforme Spec §6 e §9)
1. **Importação e Seleção de Trilha:**
   - Botão **`Importar Música`** (aceita `.mid`, `.midi`, `.musicxml`, `.xml`, `.mxl`, `.gp3`, `.gp4`, `.gp5`, `.gp`, `.mp3`, `.wav`, `.flac`, `.ogg`).
   - Barra de progresso não bloqueante (rodando na pool de threads de carga) com botão `Cancelar`.
   - **Seletor de Trilha (`Track`):** Dropdown listando as faixas do `Song` (ex.: `Trilha 1: Electric Bass (Finger)`, `Trilha 2: Rhythm Guitar`).
2. **Controles de Estudo:**
   - **Velocidade:** `50%` a `120%` (lembrando que as tolerâncias de tempo permanecem em ms absolutos conforme Spec §9.1).
   - **Loop de Trecho:** Marcadores `A` e `B` arrastáveis na régua de compassos para repetir passagens difíceis.
   - **Modo Prévia vs. Modo Treino:** Toggle explícito — no *Modo Prévia* o usuário ouve a síntese de guia; no *Modo Treino* toca apenas o clique do metrônomo (Spec §9.7).
3. **Feedback em Tempo Real nas Notas:**
   - Durante a execução, cada nota muda de cor dinamicamente conforme o resultado:
     - **Acertou (`|erro| ≤ 60 ms` e altura `±50 cents`):** Brilho rosa-esmeralda vibrante com contorno iluminado.
     - **Adiantou / Atrasou (`60–150 ms`):** Âmbar quente (`#FFB800`) com seta direcional (`←` ou `→`).
     - **Nota Errada / Perdeu (`> 150 ms`):** Carmesim escuro (`#FF1744`) semitransparente.
4. **Indicador de Confiança (Áudio Transcrito) & Ajustes Globais:**
   - Quando a música vier de transcrição de áudio, exibir uma **barra de confiança por compasso** abaixo da régua superior (rosa sólido = alta confiança; tracejado/opaco = baixa confiança) e controles globais de **Andamento**, **Deslocamento (ms)** e **Transposição (semitons)**.

---

## 6. Template Base de QSS (`src/ui/lotus_jam.qss`)

O agente deve aplicar uma folha de estilo base consistente com os tokens acima:

```css
QMainWindow, QWidget {
    background-color: #09090B;
    color: #F4F4F6;
    font-family: "Inter", "Noto Sans", sans-serif;
    font-size: 13px;
}

QFrame#CardPanel {
    background-color: #111115;
    border: 1px solid #262630;
    border-radius: 10px;
}

QPushButton {
    background-color: #191920;
    color: #F4F4F6;
    border: 1px solid #2E2E3A;
    border-radius: 6px;
    padding: 6px 14px;
    font-weight: 600;
}

QPushButton:hover {
    border-color: #FF2A85;
    background-color: #20161E;
}

QPushButton#PrimaryAction {
    background-color: #FF2A85;
    color: #FFFFFF;
    border: none;
    font-weight: 700;
}

QPushButton#PrimaryAction:hover {
    background-color: #FF4797;
}

QPushButton#PrimaryAction[running="true"] {
    background-color: #1F1218;
    color: #FF2A85;
    border: 1px solid #FF2A85;
}

QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: #191920;
    color: #F4F4F6;
    border: 1px solid #2A2A35;
    border-radius: 6px;
    padding: 4px 8px;
}

QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #FF2A85;
}

QTabBar::tab {
    background: transparent;
    color: #9E9EA9;
    padding: 10px 18px;
    font-weight: 700;
    letter-spacing: 0.5px;
    border-bottom: 2px solid transparent;
}

QTabBar::tab:selected {
    color: #FF2A85;
    border-bottom: 2px solid #FF2A85;
}
```

---

## 7. Checklist de Aceitação e Testes Automatizados (`ui_smoke_test.cpp`)

Ao concluir a implementação do frontend, o agente deve garantir que todos os requisitos funcionais e de teste passem sem erros:

- [ ] **Compilação Limpa:** Compila via `cmake -S . -B build -G Ninja && cmake --build build` sem downloads externos e com `AUTOMOC` ativo.
- [ ] **Smoke Tests Offscreen (`QT_QPA_PLATFORM=offscreen`):**
  - [ ] `UiSmoke.TrainingSessionWithFakeBackend`: injeta `FakeBackend` com 16 notas, inicia treino, verifica pontuação `> 90`, `timeline.noteCount() == 16`, botão retorna para `"Iniciar"` ao parar e salva screenshot em `build/ui_smoke.png`.
  - [ ] `UiSmoke.ErrorShownOnBackendFailure`: exibe mensagem clara em `pt-BR` no banner de status quando o backend falha.
  - [ ] `UiSmoke.TunerShowsStringAndCents`: com sinal de `82.4 Hz + 15 cents`, exibe nota `"E2"`, corda `1` destacada e desvio entre `+13` e `+17 cents`.
  - [ ] `UiSmoke.TunerNoSignalMessage`: em silêncio ou fora de faixa, exibe exatamente `"Sem sinal"` (case-insensitive) e apaga o brilho rosa do dial.
  - [ ] `UiSmoke.TunerSwitchesInstrument`: ao trocar para `baixo4`, atualiza a lista visual para 4 cordas (`E1`, `A1`, `D2`, `G2`).
- [ ] **Fidelidade Visual Lotus Jam:**
  - [ ] Dial do afinador em preto fosco com ponteiro e brilho rosa vibrante (`#FF2A85`) ao cravar na afinação (`|cents| <= 5`).
  - [ ] Piano Roll em fundo preto fosco (`#09090B`) com notas em tons de rosa e magenta e playhead neon.
  - [ ] Ícone SVG da flor de lótus com ondas sonoras configurado na janela principal e no `.desktop`.