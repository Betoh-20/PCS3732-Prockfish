# ♚ Tabuleiro de Xadrez Eletrônico — Camada Python

Aplicação em Python para o tabuleiro de xadrez eletrônico com Raspberry Pi.
Permite jogar contra o Stockfish (engine de xadrez) ou contra oponentes
online via Lichess Board API.

## Arquitetura

O sistema é dividido em duas camadas:

| Camada | Linguagem | Responsabilidade |
|--------|-----------|-----------------|
| **Hardware/Low-Level** | C (ou mock Python) | Varredura da matriz 8×8 de reed switches, debouncing, detecção de mudanças |
| **Aplicação** | Python | Lógica do jogo, validação, interface com engines, GUI |

A comunicação entre as camadas é feita via IPC (subprocess stdout/stdin, Named Pipe ou stdin).

O processo C tem **duas camadas de entrada intercambiáveis**, escolhidas por
linha de comando: a matriz de reed switches (`--input reed`) e um teclado
matricial 4×4 em que os lances são digitados (`--input keypad`). As duas
emitem os mesmos eventos, então a camada Python é a mesma nos dois casos —
ver [Processo C](#processo-c--camadas-de-entrada).

### Módulos Python

```
app/
├── config.py            # Configurações e constantes
├── ipc_reader.py        # Leitor de eventos IPC (pipe/stdin/subprocess)
├── move_interpreter.py  # Interpreta eventos de sensor → jogadas de xadrez
├── game_state.py        # Motor de estado do jogo (python-chess)
├── stockfish_engine.py  # Interface UCI com Stockfish
├── lichess_client.py    # Cliente da Lichess Board API
├── gui.py               # Interface gráfica com pygame
├── launcher.py          # Configuração de uma partida (modo, entrada, engine)
├── menu.py              # Menu que escolhe essa configuração (janela ou terminal)
└── main.py              # Ponto de entrada
```

```
mock/
├── hardware_mock.py     # Simulação do processo C para testes
└── gui_mock.py          # GUI do mock: matriz 8×8 de botões (reed switches)
```

### Módulos C

```
src/
├── main.c               # CLI: escolhe a camada de entrada
├── reed_layer.c         # Camada 1: matriz 8×8 de reed switches + debouncing
├── keypad_layer.c       # Camada 2: teclado matricial 4×4 (lances digitados)
├── board.c              # Casas do tabuleiro e espelho de ocupação
├── ipc.c                # Serialização dos eventos (stdout ou Named Pipe)
├── lcd.c                # Display 16x2 por I2C (opcional, só com --lcd)
├── gpio.c               # Wrapper da wiringPi (compila também sem ela)
└── runstate.c           # Encerramento limpo em SIGINT/SIGTERM
```

```
tests/
├── fake_lichess.py      # Servidor falso da Board API (testes sem token/rede)
├── test_lichess.py      # Modo Lichess: cliente e aplicação
├── test_challenge.py    # Desafios enviados e recebidos
├── test_stockfish_loop.py  # Regressão do loop principal
└── run_all.py           # Roda todas as suítes
```

```bash
python tests/run_all.py
```

## Instalação

### Pré-requisitos

- Python 3.10+
- Stockfish (para modo offline)

### Instalar dependências

```bash
pip install -r requirements.txt     # ou: make deps
```

Com Nix, nada disso é preciso: `nix develop` (na raiz do repositório) já traz
Python com as dependências, o Stockfish e as fontes das peças — veja
[Com o Nix](#com-o-nix).

### Instalar Stockfish

**Linux (Raspberry Pi / Ubuntu):**
```bash
sudo apt install stockfish
```

**Windows:**
Baixe de https://stockfishchess.org/download/ e configure o caminho:
```bash
set CHESS_STOCKFISH_PATH=C:\caminho\para\stockfish.exe
```

## Uso

### Menu de configuração

Tudo o que os alvos do `Makefile` escolhem por variáveis — modo de jogo,
oponente, cor das peças, camada de entrada, tempo da engine, controle de tempo
do Lichess — também é escolhido na própria interface. Sem argumentos, a
aplicação abre o menu:

```bash
python -m app.main        # ou: make menu
```

```
Tabuleiro de Xadrez Eletrônico
Stockfish (1s por lance) · peças brancas · entrada: mock (gui)
──────────────────────────────────────────────────────────────
  Modo de jogo                          Stockfish local
  Tempo por lance (s)              <       1.0       >
  Cor das peças físicas                          brancas
  Entrada do tabuleiro               mock (sem hardware)
  ...
  INICIAR PARTIDA
  Compilar processo C (make board-input)
  Sair
──────────────────────────────────────────────────────────────
Equivalente no terminal: make stockfish COLOR=white
Setas: escolher/alterar · Enter: editar ou iniciar · F5: compilar · Esc: sair
```

| Tecla | Ação |
|-------|------|
| `↑` `↓` | Escolhe a linha |
| `←` `→` | Altera o valor da linha |
| `Enter` | Edita um campo de texto, alterna um sim/não ou aciona a linha |
| `F5` | Compila o processo C (o mesmo que `make board-input`) |
| `Esc` / `Q` | Sai da aplicação |

O mouse faz o mesmo: um clique escolhe a linha, o clique seguinte altera o
valor (botão direito volta) e a roda rola a lista.

Detalhes que o menu resolve sozinho:

- **as linhas mudam com o que já foi escolhido** — o nível da IA só aparece no
  modo Lichess contra a IA, as opções do teclado 4×4 só com essa camada
  selecionada;
- **avisa antes de começar** — token do Lichess ausente, controle de tempo que
  a Board API recusa, processo C ainda não compilado e Stockfish fora do
  `PATH` aparecem no rodapé, e a partida só começa quando não há impedimento;
- **compila o processo C** sem sair da interface (por `make`, ou chamando o
  compilador direto quando não há `make` na máquina);
- **mostra o comando equivalente** (`make ...` ou `python -m app.main ...`),
  para repetir a mesma partida pelo terminal;
- **volta quando a partida acaba** — nas [opções da
  partida](#opções-durante-a-partida), "Voltar ao menu" traz o jogador de
  volta para escolher a próxima sem reabrir o programa; "Sair" (ou fechar a
  janela) encerra a aplicação.

Sem display (por SSH, por exemplo) o mesmo menu aparece como uma lista
numerada no terminal.

Opções passadas na linha de comando entram no menu como valores iniciais, o
que também vale para os alvos do `Makefile`:

```bash
make menu COLOR=black                    # abre o menu já com as pretas
python -m app.main --menu --mode lichess --lichess-ai 6
python -m app.main --no-menu --mode stockfish   # começa direto, sem menu
```

### Atalhos do Makefile

Todos os modos de execução têm um alvo no `Makefile` da raiz do repositório.
`make` sozinho lista os alvos com os valores correntes das variáveis:

```bash
make
```

| Alvo | O que faz |
|------|-----------|
| `make menu` | Abre o [menu de configuração](#menu-de-configuração) e joga a partir dele |
| `make stockfish` | Partida offline contra o Stockfish (não precisa de rede nem de token) |
| `make lichess-ai` | Partida contra a IA do Lichess (nível `LICHESS_LEVEL`, padrão 3) |
| `make random-sir` | Desafia a conta `random-sir` no Lichess |
| `make lichess-user OPPONENT=fulano` | Desafia a conta informada |
| `make lichess-seek` | Publica um *seek* e espera um oponente humano qualquer |
| `make lichess-game GAME=AbCdEfGh` | Retoma uma partida já em andamento na conta |
| `make mock` | Roda só o mock do hardware (sem a aplicação) |
| `make test` | Roda `tests/run_all.py` |
| `make deps` | `pip install -r requirements.txt` |
| `make shell` | Abre o devShell do Nix (`make shell-classic` para `nix-shell`) |
| `make clean` | Remove `__pycache__` e caches de ferramentas |

Os alvos do Lichess verificam antes se há um token acessível e falham com a
instrução de como criá-lo — em vez de abrir a janela e só então tomar um 401.

#### Variáveis

Qualquer alvo aceita variáveis na linha de comando, que é como o `make` passa
argumentos:

```bash
make stockfish COLOR=black STOCKFISH_TIME=2.0
make lichess-ai LICHESS_LEVEL=6 LICHESS_TIME=15
make lichess-user OPPONENT=fulano COLOR=black
make mock MOCK_MODE=interactive
```

| Variável | Padrão | Para quê |
|----------|--------|----------|
| `COLOR` | `white` | Cor das peças físicas (`white`/`black`) |
| `LOG_LEVEL` | `INFO` | Nível de log |
| `ARGS` | — | Opções extras repassadas direto ao `app.main` |
| `STOCKFISH_TIME` | `1.0` | Segundos de cálculo por lance |
| `STOCKFISH_PATH` | — | Binário do Stockfish (vazio: `$CHESS_STOCKFISH_PATH` ou o do `PATH`) |
| `LICHESS_LEVEL` | `3` | Nível da IA do Lichess (1–8) |
| `LICHESS_TIME` | `10` | Minutos iniciais |
| `LICHESS_INC` | `0` | Incremento por lance, em segundos |
| `LICHESS_TIMEOUT` | `180` | Espera máxima por um oponente, em segundos |
| `OPPONENT` | — | Conta a desafiar em `make lichess-user` |
| `GAME` | — | Id da partida em `make lichess-game` |
| `MOCK_MODE` | `gui` | Modo do mock em `make mock` |
| `PYTHON` | `python3` | Interpretador usado |
| `USE_NIX` | — | `USE_NIX=1` roda o alvo dentro do devShell do flake |

O `ARGS` cobre o que não tem variável própria — as opções da
[lista completa](#opções-de-linha-de-comando) continuam todas disponíveis:

```bash
make stockfish ARGS="--no-gui --ipc stdin"
```

#### Com o Nix

O repositório tem um `flake.nix` com Python (mais `python-chess`, `pygame` e
`requests`), Stockfish, `make` e a configuração de fontes que a GUI precisa
para desenhar as peças. Duas formas de usar:

```bash
# Entrar no ambiente uma vez e trabalhar dentro dele
nix develop        # ou: nix-shell, sem os experimental-features de flakes
make stockfish

# Ou rodar um alvo isolado dentro do ambiente
make stockfish USE_NIX=1
```

### Jogar contra Stockfish (com mock do hardware)

```bash
python -m app.main --mode stockfish     # ou: make stockfish
```

O mock do hardware é aberto automaticamente numa segunda janela: uma matriz
8×8 de botões, um por casa. Cada botão representa um reed switch — clique
para alternar entre pressionado (peça detectada) e solto (casa vazia).

Para simular a jogada `e2e4`, clique em `e2` (a peça sai) e depois em `e4`
(a peça chega) — a mesma sequência de dois eventos que o hardware real
produziria. Se não houver display disponível, o mock cai automaticamente
para o modo interativo por terminal.

### Opções de linha de comando

```
python -m app.main --help

Opções:
  --menu / --no-menu           Abre (ou dispensa) o menu de configuração.
                               Sem argumento nenhum, o menu é o padrão
  --mode {stockfish,lichess}   Modo de jogo (padrão: stockfish)
  --color {white,black}        Cor do jogador (padrão: white)
  --ipc {subprocess,stdin,pipe} Modo de IPC (padrão: subprocess)
  --input {mock,reed,keypad}   Camada de entrada (as duas últimas usam o
                               processo C de build/board_input)
  --board-input-args "..."     Opções extras do processo C
  --mock-mode {gui,interactive,auto}  Como o mock do hardware é operado
  --stockfish-path PATH        Caminho do Stockfish
  --stockfish-time SECONDS     Tempo de cálculo (padrão: 1.0)
  --token TOKEN                Token Lichess (evite: fica visível em `ps`)
  --token-file ARQUIVO         Lê o token de um arquivo
  --lichess-ai {1-8}           Joga contra a IA do Lichess nesse nível
  --lichess-challenge USUARIO  Desafia uma conta específica
  --lichess-game ID            Acompanha uma partida já em andamento
  --lichess-rated              Procura partida ranqueada (padrão: casual)
  --lichess-time MINUTOS       Tempo inicial (padrão: 10)
  --lichess-increment SEGUNDOS Incremento por jogada (padrão: 0)
  --lichess-timeout SEGUNDOS   Espera máxima por um oponente (padrão: 180)
  --flip                       Inverte o tabuleiro
  --no-gui                     Sem interface gráfica
  --log-level LEVEL            Nível de log
```

O tabuleiro é desenhado da perspectiva do jogador físico: com `--color black`
as pretas já ficam embaixo, e `--flip` inverte essa orientação padrão.

### Opções durante a partida

Com a partida em andamento, `Esc` (ou um toque na barra de status) abre o menu
de ações sobre o tabuleiro:

| Ação | Quando aparece | O que faz |
|------|----------------|-----------|
| **Reiniciar partida** | modo Stockfish | Volta à posição inicial e recomeça, mantendo cor e camada de entrada |
| **Abortar partida** | Lichess, antes de os dois lados jogarem | `abort` na Board API — ninguém perde |
| **Oferecer / Aceitar empate** | Lichess | Propõe empate, ou aceita o que o oponente propôs |
| **Desistir** | partida em andamento | Encerra a partida; no Lichess, manda `resign` |
| **Voltar ao menu** | sempre | Encerra a partida e volta ao menu de configuração |
| **Sair** | sempre | Fecha a aplicação |

Enquanto a partida está viva, essas ações pedem confirmação — num tabuleiro
operado por toque, um encostão não pode custar a partida. Depois do fim da
partida o menu abre sozinho, sem confirmação, com o que ainda faz sentido:
jogar de novo, voltar ao menu ou sair.

Uma proposta de empate feita pelo oponente aparece na barra de status e vira
"Aceitar empate" no menu; antes era preciso responder pelo site do Lichess.

Reiniciar não mexe no que os sensores estão lendo — a instrução da barra de
status passa a pedir as peças que faltam para montar a posição inicial.

### Jogar contra Lichess (online)

#### 1. Token de API

Crie um token em https://lichess.org/account/oauth/token/create com os escopos:

| Escopo | Para quê |
|--------|----------|
| `board:play` | **Obrigatório** — jogar pela Board API |
| `challenge:write` | Só para `--lichess-ai` (criar o desafio) |

#### Onde guardar o token

O token é uma credencial da sua conta: passá-lo em `--token` o deixa no
histórico do shell e visível para qualquer processo via `ps`. Prefira um
arquivo:

```bash
# Na raiz do repositório — já está no .gitignore
echo 'lip_seu_token' > .lichess_token
chmod 600 .lichess_token

python -m app.main --mode lichess --lichess-ai 3   # acha o token sozinho
```

O arquivo aceita comentários, o que ajuda quando há mais de uma conta:

```
# conta de testes do grupo W
lip_seu_token
```

A busca acontece nesta ordem — a primeira fonte que tiver um token vence:

| Ordem | Fonte |
|-------|-------|
| 1 | `--token TOKEN` |
| 2 | `--token-file ARQUIVO` |
| 3 | `$CHESS_LICHESS_TOKEN` |
| 4 | `$CHESS_LICHESS_TOKEN_FILE` (caminho de um arquivo) |
| 5 | `.lichess_token` (na raiz do repositório) |
| 6 | `~/.config/chess-board/lichess_token` |

Um `--token-file` que não puder ser lido é erro, não uma volta silenciosa
para as outras fontes — senão a partida poderia acabar na conta errada. A
aplicação também avisa se o arquivo estiver legível por outros usuários.

> A Board API é para **contas humanas**. Não use uma conta marcada como BOT, e
> não jogue partidas ranqueadas enquanto estiver testando.

#### 2. Jogar

```bash
# Contra a IA do Lichess (não precisa de segundo jogador — melhor para testar)
python -m app.main --mode lichess --lichess-ai 3
# make lichess-ai

# Desafiando uma conta específica (jogar contra alguém combinado)
python -m app.main --mode lichess --lichess-challenge nome_do_usuario
# make lichess-user OPPONENT=nome_do_usuario   (ou: make random-sir)

# Procurando um oponente humano qualquer (partida casual 10+0)
python -m app.main --mode lichess --lichess-time 10 --lichess-increment 0
# make lichess-seek

# Acompanhando uma partida que já está em andamento na conta
python -m app.main --mode lichess --lichess-game AbCdEfGh
# make lichess-game GAME=AbCdEfGh
```

#### Controles de tempo aceitos

A Board API **só aceita rapid ou mais lento**. O Lichess estima a duração de
uma partida em `limite_em_segundos + 40 × incremento` (40 lances) e recusa
qualquer coisa abaixo de **480 s**, respondendo
`{"global":["Invalid time control"]}`. Faz sentido: não dá para operar um
tabuleiro físico em ritmo de blitz.

| Controle | Estimativa | |
|----------|-----------|---|
| `10+0` | 600 s | aceito (padrão da aplicação) |
| `8+0` | 480 s | aceito (limite exato) |
| `5+5` | 500 s | aceito |
| `6+3` | 480 s | aceito |
| `5+3` | 420 s | **recusado** |
| `7+0` | 420 s | **recusado** |
| `3+0` | 180 s | **recusado** |

A aplicação verifica isso antes de conectar e explica o que usar no lugar,
em vez de deixar o 400 do servidor aparecer sem contexto.

Sem nenhuma dessas opções, a aplicação primeiro procura uma partida já em
aberto na conta (dá para começar a partida no site e continuar no tabuleiro
físico) e, se não houver nenhuma, publica um *seek* e espera um oponente até
`--lichess-timeout`.

#### Jogar contra uma segunda conta sua

Duas direções, as duas funcionam:

**Do tabuleiro para o navegador** — a aplicação cria o desafio:

```bash
python -m app.main --mode lichess --lichess-challenge sua_outra_conta
```

O log imprime a URL do desafio; aceite-o logado na outra conta e a partida
começa. Se você fechar a aplicação antes de o desafio ser aceito, ele é
cancelado automaticamente — nada fica pendurado na conta.

**Do navegador para o tabuleiro** — desafie a conta do tabuleiro pelo site
com a aplicação já rodando (`python -m app.main --mode lichess`). Enquanto
espera uma partida, ela **aceita automaticamente** os desafios recebidos e
começa a jogar. Desafios que a própria conta enviou são ignorados.

> Com `--lichess-challenge` dá para escolher a cor (`--color`), o que o seek
> não permite. Use contas diferentes: o Lichess não deixa uma conta desafiar
> a si mesma.

#### 3. Durante a partida

- **A cor quem decide é o Lichess.** `--color` só é respeitado com
  `--lichess-ai`; procurando um humano, o pareamento sorteia a cor (o
  endpoint de seek nem aceita escolha). Se vier a cor oposta, a aplicação se
  reconfigura sozinha — tabuleiro, orientação da GUI e mock — e avisa no log,
  mas o tabuleiro **físico** precisa ser remontado com as peças dessa cor.
- As jogadas do oponente chegam pelo stream e viram instruções físicas: uma
  captura vira "remova a peça de d5".
- Se o Lichess recusar uma jogada, ela não entra no tabuleiro virtual e a
  aplicação pede para desfazê-la no tabuleiro físico.
- Desistência, tempo esgotado e empate são reportados pelo servidor e
  encerram a partida na tela.
- Ofertas de empate e desistência **não** são feitas pelo tabuleiro: use o
  site do Lichess (a oferta recebida aparece no log).

### Usar o Mock diretamente

O mock pode ser executado standalone para testes:

```bash
# Modo GUI — matriz de botões na tela (padrão)
python -m mock.hardware_mock

# Modo GUI com o tabuleiro invertido (útil jogando de pretas)
python -m mock.hardware_mock --color black --flip

# Modo interativo (comandos no terminal)
python -m mock.hardware_mock --mode interactive

# Modo automático (jogadas aleatórias)
python -m mock.hardware_mock --mode auto --auto-events 30

# Modo scripted (sequência pré-definida)
python -m mock.hardware_mock --mode scripted --moves e2e4 e7e5 g1f3 b8c6
```

#### Mock em modo GUI

Cada casa é um botão que reflete o estado do seu reed switch:

| Aparência | Significado |
|-----------|-------------|
| Afundado, com LED verde | Sensor ativo — ímã/peça detectada |
| Em relevo, sem LED | Sensor inativo — casa vazia |

| Interação | Ação |
|-----------|------|
| Clique numa casa | Alterna o sensor e envia o evento IPC |
| Arrastar com o botão pressionado | Aplica o mesmo estado às casas percorridas |
| `Reset` / `R` | Volta os sensores ao estado inicial |
| `Limpar` / `C` | Desliga todos os sensores |
| `Inverter` / `F` | Inverte a orientação do tabuleiro |
| `Sair` / `Esc` / `Q` | Encerra o mock |

A barra inferior mostra o último evento enviado por stdout e a contagem de
sensores ativos. O tamanho do tabuleiro é ajustável por
`CHESS_MOCK_BOARD_SIZE`.

#### Comandos do mock interativo

| Comando | Descrição |
|---------|-----------|
| `e2e4` | Simula movimento (origem→destino, gera evento IPC) |
| `on e4` | Ativa o sensor em e4 (coloca peça) |
| `off e4` | Desativa o sensor em e4 (remove peça) |
| `board` | Exibe estado dos sensores |
| `reset` | Volta ao estado inicial |
| `help` | Lista os comandos |
| `quit` | Encerrar |

## Processo C — camadas de entrada

O processo C é o que fala com o hardware. Ele tem duas camadas de entrada, e
**as duas emitem exatamente os mesmos eventos IPC** (`e2:0,e4:1`) — a camada
Python não sabe qual delas está do outro lado, o que permite trocar de uma
para a outra sem mexer em nada do jogo.

| Camada | Opção | Como o lance chega |
|--------|-------|--------------------|
| Reed switches | `--input reed` | O jogador move a peça no tabuleiro físico |
| Teclado matricial | `--input keypad` | O jogador digita o lance no teclado 4×4 |

A camada de teclado é o **plano B**: se a matriz de reed switches não ficar
pronta, o jogo inteiro (Stockfish, Lichess, GUI, roque, capturas) continua
funcionando com um teclado de R$ 10 no lugar do tabuleiro instrumentado.

### Compilar

```bash
make board-input
```

A `wiringPi` é detectada automaticamente. Sem ela o binário compila do mesmo
jeito — o que permite testar o teclado com `make keypad-stdin` em qualquer
máquina — mas as camadas que tocam o GPIO recusam a rodar.

### Jogar com o processo C

```bash
# Contra o Stockfish, lendo a matriz de reed switches
make stockfish-hw

# Contra o Stockfish, digitando os lances no teclado 4×4
make keypad

# Online, contra a IA do Lichess (INPUT_LAYER escolhe a camada)
make lichess-ai-hw INPUT_LAYER=keypad
```

A mesma escolha está no [menu](#menu-de-configuração), em "Entrada do
tabuleiro" — e, com o binário ainda não compilado, a linha "Compilar processo
C" resolve isso ali mesmo.

Por baixo, os alvos apenas apontam a aplicação para o binário em vez do mock,
o que também se faz direto pela linha de comando:

```bash
# Pelas opções da aplicação
python -m app.main --no-menu --mode stockfish --input keypad \
                   --board-input-args "--keys stdin"

# Ou pelas variáveis de ambiente que os alvos do Makefile usam
CHESS_C_PROCESS=./build/board_input \
CHESS_C_PROCESS_ARGS='--input keypad' \
python -m app.main --no-menu --mode stockfish
```

### Teclado 4×4 — como digitar um lance

```
1  2  3  A          Colunas do tabuleiro:  A → a     AA → e
4  5  6  B                                 B → b     BB → f
7  8  9  C                                 C → c     CC → g
*  0  #  D                                 D → d     DD → h
```

O teclado só tem A-D e o tabuleiro precisa de a-h: **a tecla repetida vale
pela letra seguinte do bloco**. Não há tempo de espera envolvido — o lance
alterna coluna e fileira, então duas letras seguidas só podem ser a mesma
casa sendo redigitada. Um terceiro toque volta para a letra simples
(`A → a → e → a`), o que permite corrigir sem apagar.

| Lance | Teclas |
|-------|--------|
| `a2a4` | `A` `2` `A` `4` `#` |
| `e2e4` | `AA` `2` `AA` `4` `#` |
| `g1f3` | `CC` `1` `BB` `3` `#` |
| Roque curto (brancas) | `AA` `1` `CC` `1` `#`, depois `DD` `1` `BB` `1` `#` |

| Tecla | Ação |
|-------|------|
| `#` | Confirma e envia o lance |
| `*` | Apaga a última tecla |

O roque é digitado em dois lances (rei e depois torre), igual ao que se faz
no tabuleiro físico. A promoção vira dama automaticamente, como na camada de
reed switches.

#### Comandos

O prefixo `0` (só com a entrada vazia) abre os comandos que correspondem a
mexer numa peça sem fazer um lance:

| Comando | Efeito | Quando usar |
|---------|--------|-------------|
| `0` `1` *casa* `#` | Retira a peça da casa | A aplicação pediu "remova a peça de e5" (o oponente capturou) |
| `0` `2` *casa* `#` | Coloca uma peça na casa | A aplicação pediu "coloque uma peça em e5" |
| `0` `9` `#` | Reenvia o estado das 64 casas | O tabuleiro virtual e o espelho do teclado divergiram |
| `0` `0` `#` | Volta o espelho à posição inicial e o reenvia | Recomeçar do zero |

> [!NOTE]
> O processo C mantém um espelho de onde estão as peças **do jogador** e
> recusa na origem o que o tabuleiro físico também recusaria: mover de uma
> casa vazia ou para uma casa que já tem peça sua. A legalidade do lance
> continua sendo decidida pelo Python — um lance ilegal é recusado lá e a
> aplicação pede para desfazê-lo, o que no teclado é digitar o lance ao
> contrário.

### Testar sem hardware

```bash
# Teclas pelo terminal; os eventos IPC saem em stdout
make keypad-stdin
```

Digitando `AA2AA4#` a saída é `e2:0,e4:1` — exatamente o que a matriz de reed
switches emitiria para o mesmo lance.

### Opções do processo C

```
--input reed|keypad   Camada de entrada (padrão: reed)
--color white|black   Cor das peças do jogador (posição inicial do espelho)
--output CAMINHO      Escreve num Named Pipe em vez de stdout (modo IPC 'pipe')
--poll-ms N           Intervalo entre varreduras
--debounce N          Leituras estáveis exigidas por mudança
--active-low          Inverte a polaridade da matriz escolhida
--reed-flip           Gira o mapeamento 180° (a1 no canto oposto)
--no-initial          Reed: não envia o estado completo na primeira leitura
--keys gpio|stdin     Teclado: origem das teclas (stdin dispensa hardware)
--auto-enter          Teclado: envia o lance na quarta tecla, sem '#'
--raw                 Teclado: conferência de bancada (ver abaixo)
--lcd                 Teclado: usa um display 16x2 no I2C (desligado por padrão)
--i2c-bus / --lcd-addr   Barramento e endereço do display
```

O que está sendo digitado é ecoado em stderr (`[teclado] Digitando... | Lance:
E2_`), que aparece no terminal de onde a aplicação foi iniciada. Quem tiver um
display 16x2 no I2C pode passar `--lcd` para ver a mesma coisa nele.

> [!NOTE]
> O `--lcd` fica desligado por padrão de propósito. Um `open()` do barramento
> e o `ioctl(I2C_SLAVE)` funcionam mesmo sem display ligado — o ioctl só
> registra o endereço, não confere quem está lá —, então a ausência do display
> só aparece na primeira escrita. Quando isso acontece, o processo imprime uma
> mensagem, desliga o display e segue com o eco em stderr.

Pinos padrão (numeração BCM): a matriz de reed usa linhas `4,5,6,12,13,16,19,20`
e colunas `21,22,23,24,25,26,27,17`; o teclado usa linhas `16,20,21,26` e
colunas `19,13,6,5` — a fiação já montada na bancada (a mesma do experimento 6).
Para mudar, edite `keypad_config_default()` em [src/keypad_layer.c](src/keypad_layer.c).

### Conferir a fiação do teclado

Antes de jogar, vale confirmar que cada tecla chega onde se espera:

```bash
./build/board_input --input keypad --raw
```

Cada tecla pressionada imprime em que interseção da matriz ela foi lida:

```
[bancada] tecla 'A'  (linha 0 = pino 16, coluna 3 = pino 19)
```

Nenhum evento é enviado nesse modo. O que conferir:

| Sintoma | Causa provável |
|---------|----------------|
| Nenhuma tecla aparece | Polaridade invertida — tente `--active-low` |
| A tecla sai trocada por outra da mesma coluna | Ordem dos pinos de linha |
| A tecla sai trocada por outra da mesma linha | Ordem dos pinos de coluna |
| Linhas e colunas trocadas entre si | `row_pins` e `column_pins` invertidos |

Confira também a **serigrafia**: o código assume o teclado 4×4 padrão, com
`A B C D` na quarta coluna e `* 0 # D` na última linha. O experimento 6 usava
as mesmas teclas com rótulos de calculadora (`+ - * /` e `! 0 = /`) — se o
teclado da bancada for esse, a tecla `A` é a marcada `+`, a `B` é a `-`, o
`#` (confirmar) é o `=` e o `*` (apagar) é o `!`.

## Destaques no tabuleiro

Enquanto o jogador está com uma peça na mão — o sensor da casa desligou e
nenhum outro ligou — a GUI mostra para onde essa peça pode ir:

| Marca | Significado |
|-------|-------------|
| Casa verde | Casa de onde a peça foi levantada |
| Ponto no centro | Destino legal, casa livre |
| Anel vermelho | Destino legal que captura uma peça (inclui *en passant*) |
| Casa amarela | Origem e destino do último lance |

Os destinos saem dos lances legais do tabuleiro virtual, então já consideram
xeque e peças cravadas: uma peça sem lance legal não recebe marca nenhuma.
Nada é destacado fora do turno do jogador, nem quando a peça foi levantada
para desfazer um movimento ilegal — aí o que vale é a instrução da barra de
status.

## Roque em duas etapas

No tabuleiro físico ninguém move rei e torre ao mesmo tempo, então o roque é
feito **em duas etapas, o rei primeiro**:

1. **Mova o rei duas casas** (e1→g1 ou e1→c1). Sozinho, esse lance não existe
   nas regras — aqui ele é lido como o começo de um roque. O tabuleiro
   virtual **não** é atualizado ainda; a barra de status passa a pedir a
   torre e a GUI destaca a casa dela e o destino.
2. **Mova a torre** para o outro lado do rei (h1→f1 ou a1→d1). Só agora o
   roque é aplicado ao tabuleiro virtual, como um lance só.

| Estado | Mensagem exibida |
|--------|------------------|
| Rei no lugar, torre ainda na casa dela | `Roque — agora mova a torre de h1 para f1` |
| Torre na mão | `Roque — coloque a torre em f1` |
| Rei levantado de novo | `Roque — coloque o rei em g1` |

Enquanto o roque está pela metade:

- **Devolver o rei à casa dele cancela o roque.** Nada é aplicado e o jogo
  volta ao estado anterior.
- **Nenhum outro lance é aceito.** Mexer noutra peça faz a barra de status
  pedir a correção; o roque só se completa com o resto do tabuleiro na
  posição, como qualquer outro lance.

O evento único com as quatro mudanças (`e1:0,g1:1,h1:0,f1:1`) continua
valendo: se a torre já estiver no lugar quando o rei for reconhecido, o roque
é aplicado na hora, sem espera.

## Instruções na barra de status

A barra inferior da GUI diz o que fazer **no tabuleiro físico** para que ele
volte à posição que o jogo espera. A instrução tem prioridade sobre qualquer
outra mensagem enquanto o tabuleiro estiver diferente do esperado.

Uma instrução por vez, na ordem do que precisa ser feito: a peça que está na
mão, as peças deslocadas (que bloqueiam o jogo) e depois a diferença nos
sensores.

| Situação nos sensores | Mensagem exibida |
|-----------------------|------------------|
| Uma peça foi levantada | `Peça de e2 na mão — solte no destino` |
| Peça capturada pelo oponente ainda no tabuleiro | `Remova a peça de d5` |
| Movimento ilegal (registrado no histórico) | `Desfaça o movimento ilegal — mova a peça de e5 para e2` |
| Lance tentado com o tabuleiro fora da posição | `Arrume o tabuleiro antes de jogar (2 pendentes) — mova a peça de f3 para g1` |
| Peça deslocada na mão | `Desfaça o movimento ilegal — coloque a peça em e2` |
| Casas erradas sem par conhecido | `Tabuleiro fora de sincronia — remova de f1, g1 e coloque em e1, h1` |
| Tudo no lugar novamente | `Tabuleiro na posição certa — sua vez` |

Avisos do jogo entram como prefixo (`Xeque! Remova a peça de e2`). As mesmas
instruções vão para o log, o que as torna visíveis também com `--no-gui`.

### Histórico de peças deslocadas

Quando o tabuleiro virtual recusa um lance, o par origem→destino é guardado
num histórico de peças deslocadas. Isso tem duas consequências:

- **O jogo fica bloqueado até o tabuleiro voltar à posição.** Enquanto houver
  peça deslocada, nenhum lance novo é aplicado: a peça movida também entra no
  histórico (como `bloqueado`) e recebe sua própria instrução de devolução. As
  devoluções são pedidas da mais recente para a mais antiga — a peça mais nova
  pode estar justamente na casa de origem de uma anterior.
- **A instrução nunca inventa emparelhamento.** `mova a peça de X para Y` só
  é dito quando X e Y foram registrados juntos, no momento do lance ilegal.
  Para uma diferença qualquer nos sensores, a instrução é `remova de ... e
  coloque em ...`: os reed switches dizem *onde* há ímã, não *qual* peça é, e
  um palpite errado mandaria pôr a peça numa casa que, no tabuleiro virtual, é
  de outra — criando a dessincronia que a instrução deveria corrigir.

O registro é descartado quando não há mais para onde voltar — o oponente
capturou a peça deslocada, ou o jogador já pôs outra peça na casa de origem.
Nos dois casos a instrução passa a ser só `remova a peça de e5`.

## Protocolo IPC

A comunicação entre o processo C (ou mock) e o Python usa um protocolo
simples baseado em texto:

```
casa:estado,casa:estado\n
```

- **casa**: Notação algébrica (a1–h8)
- **estado**: `0` (desocupada) ou `1` (ocupada)
- Exemplo: `e2:0,e4:1\n` — peça saiu de e2, chegou em e4

### Exemplos de eventos

| Jogada | Evento IPC |
|--------|-----------|
| e2→e4 (peão) | `e2:0,e4:1` |
| O-O (roque curto brancas) | `e1:0,g1:1` e depois `h1:0,f1:1` |
| O-O-O (roque longo) | `e1:0,c1:1` e depois `a1:0,d1:1` |
| Captura (Bxf7) | `c4:0,f7:1` |

O roque também é aceito num evento só (`e1:0,g1:1,h1:0,f1:1`), mas na mão do
jogador ele chega em duas etapas — veja [Roque em duas
etapas](#roque-em-duas-etapas).

## Configuração via variáveis de ambiente

| Variável | Descrição | Padrão |
|----------|-----------|--------|
| `CHESS_IPC_MODE` | Modo IPC | `subprocess` |
| `CHESS_PIPE_PATH` | Caminho do Named Pipe | `/tmp/chess_board_pipe` |
| `CHESS_STOCKFISH_PATH` | Binário do Stockfish | `stockfish` |
| `CHESS_STOCKFISH_TIME` | Tempo de cálculo (s) | `1.0` |
| `CHESS_STOCKFISH_DEPTH` | Profundidade de busca | (sem limite) |
| `CHESS_STOCKFISH_SKILL` | Nível de habilidade (0-20) | (não configurado) |
| `CHESS_LICHESS_TOKEN` | Token OAuth2 Lichess | (vazio) |
| `CHESS_LICHESS_TOKEN_FILE` | Arquivo de onde ler o token | `.lichess_token` |
| `CHESS_LICHESS_API_URL` | URL base da API do Lichess | `https://lichess.org` |
| `CHESS_LICHESS_TIME` | Tempo inicial do seek (min) | `10` |
| `CHESS_LICHESS_INCREMENT` | Incremento do seek (s) | `0` |
| `CHESS_C_PROCESS` | Executável do hardware (ou mock) | `mock/hardware_mock.py` |
| `CHESS_BOARD_SIZE` | Tamanho do tabuleiro (px) | `640` |
| `CHESS_MOCK_BOARD_SIZE` | Tamanho da matriz de botões do mock (px) | `560` |

## Teclas de Atalho

### Menu de configuração

| Tecla | Ação |
|-------|------|
| `↑` `↓` | Escolher a linha |
| `←` `→` | Alterar o valor |
| `Enter` | Editar o campo, alternar sim/não ou acionar a linha |
| `F5` | Compilar o processo C |
| `ESC` / `Q` | Sair |

### GUI da aplicação

| Tecla | Ação |
|-------|------|
| `ESC` | Abre as [opções da partida](#opções-durante-a-partida) (fecha a janela durante as esperas) |
| `F` | Inverter tabuleiro |
| `Q/R/B/N` | Selecionar peça de promoção |

### Opções da partida (menu aberto)

| Tecla | Ação |
|-------|------|
| `↑` `↓` | Escolher a ação |
| `Enter` | Acionar (ações sem volta pedem confirmação) |
| `S` / `N` | Responder ao "tem certeza?" |
| `ESC` | Fechar o menu |

### GUI do mock (matriz de botões)

| Tecla | Ação |
|-------|------|
| `R` | Reset dos sensores |
| `C` | Limpar (desliga todos) |
| `F` | Inverter tabuleiro |
| `ESC` / `Q` | Encerrar o mock |

## Compatibilidade

| Plataforma | IPC subprocess | IPC stdin | IPC pipe (FIFO) | GUI |
|------------|:-:|:-:|:-:|:-:|
| Linux / Raspberry Pi | ✅ | ✅ | ✅ | ✅ |
| Windows | ✅ | ✅ | ❌ | ✅ |
| macOS | ✅ | ✅ | ✅ | ✅ |
