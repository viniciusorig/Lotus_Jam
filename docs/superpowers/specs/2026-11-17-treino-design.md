# Treino — metrônomo, afinador e treinador de músicas para baixo/guitarra/violão (C++)

Data: 2026-11-17 · Estado: rascunho para aprovação

## 1. Intenção e porquê

O usuário (Vinicius) está aprendendo/treinando baixo (também toca ou quer tocar
violão e guitarra). Quer um programa que o ajude a **tocar no tempo certo e as
notas certas**, com retorno imediato de quem acertou. Já existe um protótipo em
Python (`~/metronomo/`: `treino.py`, `engine.py`, `gui.py`) que prova a ideia:
toca um clique, escuta a entrada de áudio, detecta o momento de cada nota e dá
uma pontuação ao vivo. Ele foi validado com um teste de loopback (clique tocado
numa saída virtual e capturado de volta: erro estável, desvio ~3 ms), mas ainda
**não foi testado com o baixo real**.

O pedido novo: reescrever tudo em **C++**, com **multithread de verdade** (a
pontuação e a análise nunca podem atrasar o áudio nem a interface), e
acrescentar:

1. **Importar uma "música"** — o usuário sobe um arquivo, o programa interpreta
   e, enquanto ele toca, diz se está acertando (nota **e** tempo).
   Formatos: **partitura** (MIDI, MusicXML, Guitar Pro) **e áudio**
   (MP3/WAV/FLAC/OGG, transcrito automaticamente).
2. **Afinador** para **violão, guitarra e baixo**.

Por quê: o treino atual só verifica tempo contra um clique. Para estudar uma
música de verdade é preciso saber *quais* notas tocar e *quando*.

## 2. Objetivos / não-objetivos / anti-objetivos

**Objetivos**
- Metrônomo contínuo com sinal de partida e contagem; depois, só o clique.
- Detecção ao vivo de cada nota tocada (instante + altura em Hz/cents).
- Afinador em tempo real: violão, guitarra, baixo (4/5/6 cordas), afinações
  alternativas, A4 configurável.
- Importar partitura (MIDI, MusicXML, Guitar Pro) e áudio (transcrição
  própria); escolher a trilha/instrumento; tocar junto e receber pontuação por
  nota (acertou / nota errada / adiantou / atrasou / perdeu).
- Interface Qt6 em pt-BR, fluida (60 fps) mesmo sob carga de análise.
- Arquitetura multithread: callback de áudio em tempo real sem travas nem
  alocação; análise, pontuação, carga de arquivos e UI em threads separadas.

**Não-objetivos**
- Separação de fontes por IA, transcrição polifônica de alta precisão, editor
  de partitura completo, sintetizador de alta qualidade, Windows/macOS.
- Substituir o REAPER ou gravar/mixar áudio.

**Anti-objetivos (seria falha mesmo "funcionando")**
- Prometer precisão que a transcrição de áudio não tem. A interface deve
  mostrar **confiança** e permitir ouvir/ajustar o que foi interpretado.
- Qualquer travada/xrun do áudio causada por UI, disco ou análise.
- Sons além do clique durante o treino (o usuário pediu "apenas o som da
  batida"): sem bipes de feedback por nota. Só o sinal de partida no início.

## 3. Restrições (ambiente real)

- Arch Linux, PipeWire 1.6 (com camada PulseAudio), g++, cmake, ninja, Qt 6.12,
  FFTW 3.3 e libpulse instalados. `aubio` e `rtmidi` **não** estão instalados.
- Entrada do baixo: **PCM2902** (USB, mono, 16 bit, 48 kHz), sinal medido como
  **fraco** (ruído de fundo ~ −39 dBFS). Saída: HDMI do monitor (padrão) ou a
  Fuxi-H3. Os dispositivos podem ser escolhidos na interface.
- O PipeWire já "segura" as placas; **não** usar ALSA direto.
- Latência do clique e da captura vem de clocks diferentes (placas USB
  distintas); a medição precisa de mapeamento amostra→tempo contínuo (como no
  protótipo) e de compensação manual/assistida.

## 4. Abordagens consideradas

| Parte | Opção | Prós | Contras | Decisão |
|---|---|---|---|---|
| Áudio de transcrição (A) | **Detecção própria em C++** | Sem dependências pesadas; ótimo p/ linha de baixo | Violão/guitarra com acordes: só nota principal ou acorde | **Escolhida (A)** |
| | Modelo de IA externo (Basic Pitch/Demucs) | Preciso em acordes e mixagens | Python + download grande, não é C++ | Adiado (possível extensão) |
| | Só importar MIDI externo | Simples | Passo manual, foge de "subir como música" | **Também incluído (C)**: já coberto pelo importador de partitura |
| Áudio nativo | **PipeWire nativo (libpipewire)** | Timestamps precisos, baixa latência | API mais complexa | **Escolhida** |
| | libpulse (como no protótipo) | Simples | Latência/timestamp menos confiáveis | Reserva |
| UI | **Qt6 Widgets/QPainter** | Já instalado, simples p/ gráficos custom | — | **Escolhida** |

Ordem de entrega (cada parte funciona sozinha): (1) núcleo + afinador + treino
de tempo; (2) partitura; (3) transcrição de áudio; (4) refinos.

## 5. Arquitetura (decidido pelo usuário/pelo design)

Threads (cada uma com um papel só; comunicação por filas sem trava —
SPSC ring buffers — e snapshots imutáveis):

1. **Thread de callback PipeWire (entrada)** — tempo real. Só copia amostras
   para um ring buffer e registra o carimbo de tempo. Sem malloc/locks/I/O.
2. **Thread de callback PipeWire (saída)** — tempo real. Lê um *agendador*
   pré-computado (clique, sinal de partida, notas-guia opcionais) e escreve o
   buffer; informa os instantes audíveis de cada batida.
3. **Thread de análise (DSP)** — consome o ring de entrada: detecção de ataque
   (envelope/fluxo espectral) e altura (YIN/MPM com suavização), afinador
   (janelas maiores, estimativa em cents). Emite eventos `NotaDetectada`.
4. **Thread de pontuação** — casa eventos com a linha do tempo (grade do
   metrônomo ou notas da música), aplica tolerâncias, emite `Resultado`.
5. **Thread(s) de carga** (pool) — decodifica arquivos, parseia partitura,
   transcreve áudio em segundo plano com barra de progresso e cancelamento.
6. **Thread de UI (Qt)** — só desenha; recebe snapshots a ~60 Hz. Nunca
   bloqueia as outras.

Relógio: `CLOCK_MONOTONIC` como referência; mapeamento amostra→tempo por
regressão linear sobre pontos recentes, separado para entrada e saída.

Modelo interno comum: `Song { tempo map, compassos, trilhas[] }`,
`Track { instrumento, notas[{início, duração, nota MIDI, cordas?}] }`.
Partitura e áudio transcrito produzem o **mesmo** `Song`.

## 6. Funcionalidades — comportamento

**Treino de tempo (herdado do protótipo)**: BPM, contagem, subdivisão
(1/2/4 por batida), compensação em ms. Sinal de partida (dois tons) → pausa →
contagem → clique uniforme **sem acento**. Roda até "Parar". Gráfico mostra as
últimas 32 notas (cor por faixa: ≤15 ms verde, ≤50 amarelo, resto vermelho; X =
batida sem nota); pontuação total e das últimas 16; média e desvio do erro;
notas extras (>200 ms de qualquer batida) contadas à parte.

**Afinador**: escolhe instrumento (Violão, Guitarra, Baixo 4/5/6 cordas) e
afinação (padrão, Drop D, meio-tom abaixo, DADGAD…); mostra a corda mais
próxima, desvio em cents (agulha/barra) e frequência; A4 ajustável (padrão
440 Hz); filtro adequado à faixa de cada instrumento (baixo chega a ~31 Hz com
5 cordas).

**Treino com música**: o usuário importa o arquivo, escolhe a trilha, o
programa mostra as notas (linha do tempo/pauta simples tipo "rolo de piano"
rolando), usa o andamento do arquivo (com controle de velocidade 50–120%),
toca contagem e acompanha. Por nota: **acertou** (altura dentro de ±50 cents e
tempo dentro da tolerância), **nota errada**, **adiantou/atrasou** (ms),
**perdeu**. Detalhes: oitava (baixo soa 1 oitava abaixo do escrito —
tratado), loop de trecho, pontuação por compasso.

**Importar partitura**: MIDI (SMF 0/1, mapa de tempo, seleção de trilha),
MusicXML (.musicxml/.xml e .mxl compactado), Guitar Pro (.gp3/.gp4/.gp5 e,
se viável, .gp/.gpx). Ordem de prioridade: MIDI → MusicXML → GP3–5 → GP7+.

**Importar áudio (A)**: decodifica (MP3/WAV/FLAC/OGG), converte para mono
48 kHz, **foca na linha grave** (passa-baixas/banda ~30–400 Hz), detecta
altura (pYIN/MPM + Viterbi para continuidade), segmenta em notas, estima
andamento e grade de batidas. Para guitarra/violão: melhor esforço — nota
principal ou acorde (cromagrama), sinalizado como baixa confiança. O resultado
vira um `Song` editável nos parâmetros globais (andamento, deslocamento,
transposição), com **pré-escuta sintetizada** e **indicador de confiança por
trecho**. O resultado fica em cache (`~/.local/share/treino/`).

## 7. Critérios de aceitação

- Afinador: sinal sintético em cada corda (E1 41,2 Hz … E4 329,6 Hz) estimado
  com erro < 1 cent; resposta visual < 100 ms; sem salto de oitava.
- Detecção de ataque em áudio sintético: erro de tempo < 3 ms; teste de
  **loopback** (saída virtual → entrada) reproduz o resultado do protótipo.
- Tempo real: zero alocação/locks no callback (verificado por teste/sanitizer);
  nenhuma xrun com a UI sob carga e uma transcrição rodando em paralelo.
- Importadores: arquivos de exemplo (MIDI, MusicXML, GP5) geram o `Song`
  esperado (testes automáticos).
- Transcrição de áudio: linha de baixo sintética em WAV com ≥ 90 % das notas
  corretas (altura e início ±50 ms); relatório honesto de limites com mixagens
  reais.
- Compila com cmake+ninja a partir de um clone limpo; testes automáticos
  rodam sem placa de áudio (usam sinais sintéticos).

## 8. Fora do documento / a cargo do construtor

Escolha de bibliotecas de decodificação (ex.: `miniaudio`/`dr_libs` vendorizadas
ou libsndfile/mpg123), parser de ZIP para `.mxl`/`.gp`, framework de testes,
estrutura exata de diretórios e nomes de classes, algoritmos precisos de
detecção (YIN vs MPM, parâmetros de limiar), formato do cache, ícone/tema,
política de recalibração automática de latência. Calibração: compensação
manual obrigatória; assistente de calibração é opcional (propostas aceitas).

O protótipo Python em `~/metronomo/` permanece como referência até a paridade
funcional da parte 1.

## 9. Decisões tomadas na revisão (chamadas do autor do design, não do usuário)

1. **Tolerâncias no modo música** (todas configuráveis, em ms absolutos, **não**
   escalam com a velocidade): *acertou* = |erro| ≤ 60 ms e altura ±50 cents;
   *adiantou/atrasou* = 60–150 ms (conta como acerto parcial); *perdeu* = sem
   nota detectada até 150 ms depois da nota (ou >150 ms de erro). A altura é
   comparada **com a oitava** (opção, desligada por padrão: aceitar qualquer
   oitava). Associação nota-detectada↔nota-da-música por janela, uma-para-uma.
2. **Alvo principal: o baixo real no PCM2902.** O DSP inclui ganho de entrada
   digital, limiar adaptativo (já no protótipo) e um botão **"gravar amostra"**
   (salva WAV da entrada) para criar testes de regressão com o baixo real.
   Testes sintéticos e de loopback continuam obrigatórios.
3. **Polifonia ao vivo:** entrada monofônica (YIN/MPM). Nota de acorde na
   partitura é pontuada pela **nota mais grave** (baixo) ou **qualquer nota do
   acorde** (guitarra/violão), regra documentada na interface. Verificação por
   cromagrama fica para a parte 4.
4. **Arquivos:** os de teste são gerados por nós (fixtures mínimas). Guitar Pro:
   **.gp3/.gp4/.gp5 na primeira versão; .gp (7/8, ZIP+XML) se couber; .gpx
   (BCFZ) fora.**
5. **Latência:** o pontuador usa os **instantes audíveis** carimbados pelo
   callback de saída (não só o agendado); a compensação em ms é salva por par
   (entrada, saída) nas configurações.
6. **Edição pós-importação de áudio:** só parâmetros globais (andamento,
   deslocamento, transposição); nada por nota. Confiança **por trecho** (compasso),
   baseada na probabilidade de altura e na proporção de quadros "com voz".
7. **Pré-escuta / notas-guia** ficam num modo de **prévia** separado, com
   oscilador simples; durante o treino só o clique (e o sinal de partida).
8. **Dependências:** bibliotecas **vendorizadas** (cabeçalho único) no repositório;
   **nenhum download na configuração do build**.
