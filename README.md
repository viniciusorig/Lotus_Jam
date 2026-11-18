# Lotus Jam

Treinador de tempo e afinador em tempo real para baixo, guitarra e violão, para Linux
(PipeWire). É a versão em C++20/Qt 6 do protótipo em Python (não publicado): toca um
metrônomo, escuta o instrumento pela interface de áudio (por padrão a PCM2902 USB) e mostra,
nota a nota, se você está adiantado ou atrasado em relação ao clique.

## Compilar

Dependências: CMake ≥ 3.20, Ninja, um compilador C++20, Qt 6 (Widgets, Svg), PipeWire
(`libpipewire-0.3`) e GoogleTest (para os testes).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Sem `-DCMAKE_BUILD_TYPE`, a compilação é otimizada (`RelWithDebInfo`): o DSP (YIN, detector de
ataques) é lento demais em `-O0`. Para depurar: `-DCMAKE_BUILD_TYPE=Debug`. Para o
ThreadSanitizer: `-DTREINO_TSAN=ON` (num diretório de build separado).

## Rodar

```sh
build/lotus-jam
```

Para instalar o lançador (atalho no menu de aplicativos e ícone):

```sh
cmake --install build --prefix ~/.local
```

Isso copia `bin/lotus-jam`, `share/applications/lotus-jam.desktop` e os ícones em
`share/icons/hicolor/` (16 a 256 px e SVG).

## As abas

No cabeçalho ficam os seletores de **entrada** e **saída** (dispositivos do PipeWire; ↻ refaz
a lista), o **medidor de nível** da entrada (dBFS, −45…0; a marca indica o piso de ruído da
PCM2902, ≈ −39 dBFS), o **ganho** digital aplicado à análise e a **compensação** de latência.

- **AFINADOR** — liga sozinho ao entrar na aba e desliga ao sair (o botão Parar/Iniciar pausa).
  Escolha o instrumento (violão, guitarra, baixo de 4, 5 ou 6 cordas), a afinação (Padrão,
  Drop D, Meio-tom abaixo, DADGAD no violão/guitarra) e a referência do Lá (A4). Mostra a nota,
  a corda mais próxima, a frequência e o desvio em cents; o mostrador acende (“Nana Glow”)
  dentro de ±5 cents. Sem nota reconhecível aparece “Sem sinal”.
- **TREINO DE TEMPO** — escolha o andamento (BPM), a contagem (sem contagem, 1 ou 2
  compassos) e as notas por batida (1, 2 ou 4). Iniciar toca um sinal de partida, a contagem e
  depois o clique contínuo (todos iguais, sem acento). Toque junto: cada nota aparece no
  gráfico com o erro em ms (verde ≤ 15 ms, amarelo ≤ 50 ms, vermelho acima ou nota faltando) e
  o cartão de resumo mostra a pontuação (0–100, a mesma fórmula do protótipo), a média do erro
  (positivo = atrasado), a regularidade, acertos e notas extras. “Gravar amostra” salva 10 s da
  entrada em WAV (`~/.local/share/lotus-jam/amostras/`).
- **MÚSICAS & MIDI** — reservada para as próximas partes (partitura, áudio e MIDI).

## Compensação de latência

O tempo de cada nota é medido no relógio do áudio; mesmo assim a interface, os buffers e o
caminho até o alto-falante somam um atraso constante que o programa não enxerga por completo.
A compensação (em ms) é **subtraída** do tempo de cada nota tocada antes da pontuação, igual
ao protótipo.

Como calibrar com o baixo de verdade:

1. Escolha a entrada e a saída que você vai usar e deixe a compensação em 0 ms.
2. Em TREINO DE TEMPO, toque junto com o clique por uns 30 s, com o máximo de cuidado.
3. Pare e clique em **Calibrar**: ele usa **todas** as notas que você tocou (inclusive as que caíram
   fora da janela de 200 ms, que aparecem como “extras”) e ajusta a Compensação sozinho. Isso
   funciona mesmo quando o atraso do áudio (por exemplo, o do monitor HDMI) é maior que a janela.
   Se preferir ajustar à mão, leia a **média do erro** no cartão de resumo. Se ela der, por exemplo, +18 ms (você
   “atrasou” mesmo tocando certo), coloque **+18** em Compensação; se der −7 ms, coloque −7.
   (Se já havia uma compensação, some a média a ela.)
4. Inicie de novo e confira que a média fica perto de 0.

A compensação é guardada **por par entrada/saída**: ao trocar de dispositivo, o campo mostra o
valor salvo para o novo par (0 se o par nunca foi calibrado).

## Configurações

Ficam em `~/.config/lotus-jam/lotus-jam.conf` (formato INI do Qt): dispositivos escolhidos,
BPM, contagem, subdivisão, ganho, instrumento, afinação, A4 e a compensação de cada par de
dispositivos. São carregadas ao abrir, salvas ao iniciar uma sessão, um segundo depois de cada
mudança e ao fechar a janela. Valores ausentes ou inválidos voltam ao padrão (80 BPM, contagem
de 4 tempos, 1 nota por batida, ganho 0 dB, compensação 0 ms, A4 440 Hz, baixo de 4 cordas,
afinação padrão). Se o dispositivo salvo não estiver conectado, o programa usa o padrão (a
PCM2902 na entrada, HDMI na saída, ou o primeiro da lista) sem esquecer o salvo: quando ele
voltar e você clicar em ↻, volta a ser o selecionado.

## Arquitetura

- `src/core` (relógio e anel SPSC), `src/dsp` (YIN, afinador, detector de ataques, notas),
  `src/score` (pontuação na grade), `src/audio` (backend PipeWire, `FakeBackend`, metrônomo) e
  `src/app` (sessão) formam a biblioteca `treino_core`, sem Qt.
- `src/ui` e `src/app/settings.*` formam `lotus_ui` (Qt 6 Widgets); `src/main.cpp` é o executável.
- Threads: os callbacks de tempo real do PipeWire só copiam/geram áudio e carimbam o tempo;
  a thread de **análise** aplica o ganho e roda o detector de ataques (treino) ou o afinador; a
  thread de **pontuação** converte posições de amostra em tempo, casa as notas com a grade e
  gera os eventos; a thread da **interface** só desenha.
- Entre elas há filas SPSC sem bloqueio (a interface nunca segura o áudio; eventos de nível e
  de afinador são descartados se a fila encher).
- O relógio é o do backend: entrada e saída têm cada uma um mapa amostra → tempo (regressão
  sobre carimbos do PipeWire), então o erro de cada nota não depende de quando a interface
  recebe os eventos. Perdas de entrada (xruns) partem a linha do tempo em segmentos, sem
  deslocar as notas seguintes.
- A janela abre o PipeWire, lista dispositivos e inicia sessões em threads auxiliares; a
  thread da interface nunca faz DSP nem E/S de áudio bloqueante (só grava as
  configurações, alguns bytes, ao iniciar, ao fechar e com atraso de 1 s).

## Limitações conhecidas

A validação com o baixo real na PCM2902 **ainda não foi feita** (veja
`docs/validacao-parte1.md`); tudo abaixo foi medido com sinais sintéticos.

- Detector de ataques (detalhes em `src/dsp/onset.h`): re-toques da mesma nota por cima da
  cauda que não sobem mais de 6 dB podem não ser detectados; notas menos de ~14 dB acima do
  piso de ruído não são detectadas; depois de uma queda de ~20 dB no volume do jogo há um ponto
  cego de até ~10 s; ronco de rede ou ventilador que sobe durante o jogo pode gerar ataques
  falsos; uma sessão que começa no meio de um jogo contínuo, sem silêncio, perde a referência do
  piso (por isso a contagem).
- Afinador: sinal com RMS abaixo de −38 dBFS (silêncio, ruído, ronco) aparece como “Sem sinal”;
  uma corda bem desafinada é mostrada com a nota mais próxima e o desvio em cents. A tolerância
  de oitava é fixa; uma fundamental muito fraca em relação ao 2º harmônico ainda pode ser lida
  uma oitava acima.
- O viés absoluto do carimbo de tempo do PipeWire em hardware real (até 1–2 quanta) não foi
  medido; é constante e é absorvido pela compensação.

## Testes

```sh
ctest --test-dir build --output-on-failure
```

Os testes não tocam no áudio real nem nas suas configurações: os de interface rodam com
`QT_QPA_PLATFORM=offscreen`, um `FakeBackend` (relógio virtual, sinais sintéticos) e arquivos
INI temporários; os testes do backend PipeWire criam só um null sink privado e temporário
(nunca o dispositivo padrão) e são pulados se não houver PipeWire. Para pular esses:
`ctest --test-dir build -E PipeWire`. Com ThreadSanitizer:

```sh
cmake -S . -B build-tsan -G Ninja -DTREINO_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tsan
ctest --test-dir build-tsan
```
