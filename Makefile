# Makefile — atalhos para os modos de execução do projeto.
#
#   make                  lista os alvos disponíveis
#   make stockfish        joga contra o Stockfish local
#   make lichess-ai       joga contra a IA do Lichess
#   make random-sir       desafia a conta "random-sir" no Lichess
#   make lichess-user OPPONENT=fulano    desafia a conta informada
#
# Todo alvo aceita variáveis na linha de comando (COLOR, LICHESS_LEVEL, ...)
# e um ARGS livre repassado ao `app.main`:
#
#   make stockfish COLOR=black STOCKFISH_TIME=2.0
#   make lichess-ai LICHESS_LEVEL=6 ARGS="--log-level DEBUG"

# ---------------------------------------------------------------------------
#  Variáveis configuráveis
# ---------------------------------------------------------------------------

# Atenção ao editar: em Make, um comentário no fim da linha de atribuição
# entraria no valor da variável (com os espaços antes dele) — daí cada
# comentário ficar em linha própria.
PYTHON          ?= python3

# Cor das peças físicas: white|black
COLOR           ?= white
# Nível de log: DEBUG|INFO|WARNING|ERROR
LOG_LEVEL       ?= INFO
# Opções extras repassadas ao app.main
ARGS            ?=

# Segundos de cálculo por lance
STOCKFISH_TIME  ?= 1.0
# Vazio = usa $CHESS_STOCKFISH_PATH ou o stockfish do PATH
STOCKFISH_PATH  ?=

# Nível da IA do Lichess (1-8)
LICHESS_LEVEL   ?= 3
# Minutos iniciais (a Board API exige o equivalente a 8+0 ou mais lento)
LICHESS_TIME    ?= 10
# Incremento por lance, em segundos
LICHESS_INC     ?= 0
# Espera máxima por um oponente, em segundos
LICHESS_TIMEOUT ?= 180
# Conta a desafiar em `make lichess-user`
OPPONENT        ?=
# Id de partida em `make lichess-game`
GAME            ?=

# Conta usada por `make random-sir`.
RANDOM_SIR      := random-sir

# Mock do hardware executado standalone (`make mock`): gui|interactive|auto|scripted
MOCK_MODE       ?= gui
MOCK_ARGS       ?=

# Camada de entrada do processo C: reed (matriz de reed switches) ou
# keypad (teclado matricial 4x4, lances digitados — plano B)
INPUT_LAYER     ?= reed
# Opções extras repassadas ao processo C (ex: --auto-enter, --lcd)
BOARD_INPUT_ARGS ?=

# USE_NIX=1 roda cada alvo dentro do devShell do flake (traz python com as
# dependências, o Stockfish e a config de fontes). Sem isso, assume-se que o
# ambiente atual já tem tudo — inclusive quando já se está dentro do shell.
ifeq ($(USE_NIX),1)
RUN := nix develop $(CURDIR) --command
else
RUN :=
endif

# `python -m app.main` precisa rodar com a raiz do repositório como diretório
# atual; `make -C <repo>` e `make` de dentro dele já garantem isso.
APP  = $(RUN) $(PYTHON) -m app.main --color $(COLOR) --log-level $(LOG_LEVEL)

STOCKFISH_OPTS = --mode stockfish --stockfish-time $(STOCKFISH_TIME) \
                 $(if $(STOCKFISH_PATH),--stockfish-path $(STOCKFISH_PATH))

LICHESS_OPTS   = --mode lichess --lichess-time $(LICHESS_TIME) \
                 --lichess-increment $(LICHESS_INC) \
                 --lichess-timeout $(LICHESS_TIMEOUT)

# ---------------------------------------------------------------------------
#  Processo C — camadas de entrada (reed switches / teclado matricial)
# ---------------------------------------------------------------------------

CC        ?= gcc
CFLAGS    ?= -O2 -Wall -Wextra -std=gnu11
C_SRC     := $(wildcard src/*.c)
C_HDR     := $(wildcard src/*.h)
C_OBJ     := $(C_SRC:src/%.c=build/%.o)
C_BIN     := build/board_input

# A wiringPi só existe no Raspberry Pi. Sem ela o binário compila do mesmo
# jeito — o que permite testar o parser do teclado com `make keypad-stdin`
# em qualquer máquina — mas as camadas que tocam GPIO recusam a rodar.
HAVE_WIRINGPI ?= $(shell test -f /usr/include/wiringPi.h -o -f /usr/local/include/wiringPi.h && echo 1)
ifeq ($(HAVE_WIRINGPI),1)
C_DEFS := -DHAVE_WIRINGPI
C_LIBS := -lwiringPi
else
C_DEFS :=
C_LIBS :=
endif

# Ambiente que faz a aplicação usar o processo C no lugar do mock.
HW_ENV = CHESS_C_PROCESS='$(CURDIR)/$(C_BIN)' \
         CHESS_C_PROCESS_ARGS='--input $(INPUT_LAYER) $(BOARD_INPUT_ARGS)'

.PHONY: help menu stockfish lichess-ai random-sir lichess-user lichess-seek \
        lichess-game mock test deps shell shell-classic check-token clean pdf \
        pdf3 diagramas board-input stockfish-hw lichess-ai-hw keypad keypad-stdin

# ---------------------------------------------------------------------------
#  Ajuda (alvo padrão)
# ---------------------------------------------------------------------------

help:
	@echo 'Tabuleiro de Xadrez Eletrônico — alvos disponíveis:'
	@echo ''
	@echo '  make menu                          escolhe tudo na interface (modo,'
	@echo '                                     oponente, cor, entrada) e joga'
	@echo ''
	@echo '  make stockfish                     joga contra o Stockfish local'
	@echo '  make lichess-ai                    joga contra a IA do Lichess (nível $(LICHESS_LEVEL))'
	@echo '  make random-sir                    desafia a conta "$(RANDOM_SIR)" no Lichess'
	@echo '  make lichess-user OPPONENT=fulano  desafia a conta informada'
	@echo '  make lichess-seek                  procura um oponente humano qualquer'
	@echo '  make lichess-game GAME=AbCdEfGh    retoma uma partida já em andamento'
	@echo ''
	@echo '  make board-input                   compila o processo C (build/board_input)'
	@echo '  make stockfish-hw                  joga usando o processo C (INPUT_LAYER=$(INPUT_LAYER))'
	@echo '  make keypad                        idem, com o teclado matricial 4x4'
	@echo '  make keypad-stdin                  só o processo C, teclas pelo terminal'
	@echo ''
	@echo '  make mock                          roda só o mock do hardware'
	@echo '  make test                          roda a suíte de testes'
	@echo '  make deps                          instala as dependências Python'
	@echo '  make shell                         abre o devShell do Nix (nix-shell: make shell-classic)'
	@echo '  make clean                         remove __pycache__ e caches'
	@echo '  make pdf                           gera docs/Relatorio-1.pdf (requer pandoc)'
	@echo '  make pdf3                          gera docs/Relatorio-3.pdf (LaTeX/xelatex)'
	@echo '  make diagramas                     renderiza docs/diagramas/*.mmd em .png'
	@echo ''
	@echo 'Variáveis (make <alvo> VAR=valor):'
	@echo '  COLOR=$(COLOR)  LOG_LEVEL=$(LOG_LEVEL)  ARGS=...'
	@echo '  STOCKFISH_TIME=$(STOCKFISH_TIME)  STOCKFISH_PATH=$(STOCKFISH_PATH)'
	@echo '  LICHESS_LEVEL=$(LICHESS_LEVEL)  LICHESS_TIME=$(LICHESS_TIME)  LICHESS_INC=$(LICHESS_INC)  LICHESS_TIMEOUT=$(LICHESS_TIMEOUT)'
	@echo '  INPUT_LAYER=$(INPUT_LAYER)  BOARD_INPUT_ARGS=...    (alvos com o processo C)'
	@echo '  USE_NIX=1                          roda o alvo dentro do devShell do flake'

# ---------------------------------------------------------------------------
#  Modos de jogo
# ---------------------------------------------------------------------------

# Menu na própria aplicação: as escolhas que os alvos abaixo fazem por
# variáveis (modo, oponente, cor, camada de entrada, tempo da engine) são
# feitas na tela, e o menu volta ao fim de cada partida. As variáveis daqui
# continuam valendo — elas entram como os valores iniciais do menu.
menu:
	$(APP) --menu $(ARGS)

# Partida offline contra o Stockfish. Não precisa de rede nem de token.
stockfish:
	$(APP) $(STOCKFISH_OPTS) $(ARGS)

# Partida contra a IA do Lichess: não depende de um segundo jogador, é o
# caminho mais rápido para testar o modo online de ponta a ponta.
lichess-ai: check-token
	$(APP) $(LICHESS_OPTS) --lichess-ai $(LICHESS_LEVEL) $(ARGS)

# Desafio direto à conta random-sir. A URL do desafio sai no log; ele precisa
# ser aceito do outro lado (e é cancelado se a aplicação for fechada antes).
random-sir:
	@$(MAKE) --no-print-directory lichess-user OPPONENT=$(RANDOM_SIR)

# Desafio direto a uma conta qualquer. O Lichess não deixa uma conta desafiar
# a si mesma: OPPONENT tem de ser diferente da conta do token.
lichess-user: check-token
	@test -n "$(OPPONENT)" || { \
	  echo 'Erro: informe a conta a desafiar — make lichess-user OPPONENT=fulano'; \
	  exit 2; }
	$(APP) $(LICHESS_OPTS) --lichess-challenge $(OPPONENT) $(ARGS)

# Seek aberto: o Lichess pareia com um humano qualquer e sorteia a cor
# (COLOR é ignorado aqui — o endpoint de seek não aceita escolha).
lichess-seek: check-token
	$(APP) $(LICHESS_OPTS) $(ARGS)

# Retoma no tabuleiro físico uma partida já em andamento na conta.
lichess-game: check-token
	@test -n "$(GAME)" || { \
	  echo 'Erro: informe o id da partida — make lichess-game GAME=AbCdEfGh'; \
	  exit 2; }
	$(APP) --mode lichess --lichess-game $(GAME) $(ARGS)

# ---------------------------------------------------------------------------
#  Processo C — compilação e partidas com hardware de verdade
# ---------------------------------------------------------------------------

board-input: $(C_BIN)

$(C_BIN): $(C_OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ $(C_LIBS)
	@echo 'Compilado: $@ (wiringPi: $(if $(HAVE_WIRINGPI),sim,não))'

# Todo objeto depende de todos os cabeçalhos: são poucos arquivos, e assim
# mexer num .h não deixa um .o velho para trás.
build/%.o: src/%.c $(C_HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) $(C_DEFS) -c $< -o $@

# Partida contra o Stockfish lendo o tabuleiro pelo processo C em vez do mock.
# INPUT_LAYER escolhe a camada: reed (padrão) ou keypad.
stockfish-hw: $(C_BIN)
	$(HW_ENV) $(APP) $(STOCKFISH_OPTS) $(ARGS)

# O mesmo, jogando online contra a IA do Lichess.
lichess-ai-hw: check-token $(C_BIN)
	$(HW_ENV) $(APP) $(LICHESS_OPTS) --lichess-ai $(LICHESS_LEVEL) $(ARGS)

# Plano B: partida com os lances digitados no teclado matricial 4x4.
keypad:
	@$(MAKE) --no-print-directory stockfish-hw INPUT_LAYER=keypad

# Só o processo C, com as teclas vindo do terminal em vez do GPIO: mostra os
# eventos IPC gerados e não precisa de Raspberry Pi nem de teclado.
# Digite, por exemplo, "AA2AA4#" para o lance e2e4.
keypad-stdin: $(C_BIN)
	$(RUN) ./$(C_BIN) --input keypad --keys stdin --color $(COLOR) \
	       $(BOARD_INPUT_ARGS)

# ---------------------------------------------------------------------------
#  Apoio
# ---------------------------------------------------------------------------

# Falha antes de abrir a janela quando não há token: o erro do servidor
# (401) não diz que a credencial simplesmente não foi encontrada.
check-token:
	@$(RUN) $(PYTHON) -c 'from app.config import LICHESS_TOKEN; raise SystemExit(0 if LICHESS_TOKEN else 1)' || { \
	  echo 'Erro: token do Lichess não encontrado.'; \
	  echo 'Crie um em https://lichess.org/account/oauth/token/create com os escopos'; \
	  echo 'board:play e challenge:write, e salve-o em .lichess_token:'; \
	  echo "  echo 'lip_seu_token' > .lichess_token && chmod 600 .lichess_token"; \
	  exit 2; }

mock:
	$(RUN) $(PYTHON) -m mock.hardware_mock --mode $(MOCK_MODE) --color $(COLOR) $(MOCK_ARGS)

test:
	$(RUN) $(PYTHON) tests/run_all.py

deps:
	$(RUN) $(PYTHON) -m pip install -r requirements.txt

shell:
	nix develop $(CURDIR)

# Mesmo ambiente para quem não tem os experimental-features de flakes.
shell-classic:
	nix-shell $(CURDIR)

clean:
	find . -name '__pycache__' -type d -prune -exec rm -rf {} +
	rm -rf .pytest_cache .mypy_cache .ruff_cache build

# ---------------------------------------------------------------------------
#  Documentação
# ---------------------------------------------------------------------------

# Dois relatórios, duas ferramentas:
#
#   make pdf    docs/Relatorio-1.md  -> PDF (pandoc + xelatex)
#   make pdf3   docs/Relatorio-3.tex -> PDF (xelatex direto)
#
# Os dois dependem dos diagramas (`make diagramas`), gerados dos .mmd por
# mmdc. `USE_NIX=1` traz pandoc, texlive e mermaid-cli sem instalar nada.
REPORT_SRC  := docs/Relatorio-1.md
REPORT_PDF  := docs/Relatorio-1.pdf

REPORT3_TEX := docs/Relatorio-3.tex
REPORT3_PDF := docs/Relatorio-3.pdf

DIAGRAMS_SRC = $(wildcard docs/diagramas/*.mmd)
DIAGRAMS_PNG = $(DIAGRAMS_SRC:.mmd=.png)

docs/diagramas/%.png: docs/diagramas/%.mmd
	$(RUN) mmdc -i $< -o $@ -b white -s 4

diagramas: $(DIAGRAMS_PNG)

pdf: $(DIAGRAMS_PNG) $(REPORT_PDF)

pdf3: $(DIAGRAMS_PNG) $(REPORT3_PDF)

# Duas passadas: a primeira escreve o .toc e os \label, a segunda os resolve.
# -output-directory faz os auxiliares ficarem em docs/ (limpos no fim), e
# `cd docs` não serve aqui porque os \includegraphics são relativos a ele.
$(REPORT3_PDF): $(REPORT3_TEX) $(DIAGRAMS_PNG)
	$(RUN) xelatex -interaction=nonstopmode -halt-on-error \
	       -output-directory=docs $(REPORT3_TEX) >/dev/null
	$(RUN) xelatex -interaction=nonstopmode -halt-on-error \
	       -output-directory=docs $(REPORT3_TEX) >/dev/null
	@rm -f docs/Relatorio-3.aux docs/Relatorio-3.log docs/Relatorio-3.out \
	       docs/Relatorio-3.toc
	@echo "PDF gerado: $(REPORT3_PDF)"

$(REPORT_PDF): $(REPORT_SRC) $(DIAGRAMS_PNG) docs/header.tex
	pandoc $(REPORT_SRC) \
	  -o $(REPORT_PDF) \
	  --pdf-engine=xelatex \
	  -H docs/header.tex \
	  -V geometry:margin=2.5cm \
	  -V fontsize=12pt \
	  -V mainfont="DejaVu Sans" \
	  -V monofont="DejaVu Sans Mono" \
	  -V lang=pt-BR \
	  --highlight-style=tango \
	  --toc \
	  -V toc-title="Sumário" \
	  --metadata title="Tabuleiro de Xadrez Eletrônico com Integração a Chess Engine" \
	  --metadata author="PCS3732 — Laboratório de Processadores" \
	  --metadata date="Julho 2026"
	@echo "PDF gerado: $(REPORT_PDF)"

.DEFAULT_GOAL := help
