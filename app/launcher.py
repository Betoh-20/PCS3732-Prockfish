"""
launcher.py — Configuração de uma partida, escolhida antes de começar.

Reúne num objeto só tudo o que hoje se escolhe pelo Makefile (modo de jogo,
cor, camada de entrada, parâmetros da engine e do Lichess) e sabe traduzir
essas escolhas para o que a aplicação precisa: o caminho e os argumentos do
processo de hardware e os parâmetros de `ChessApplication`.

Este módulo é deliberadamente livre de pygame: a lista de opções (`OPTIONS`)
descreve o que pode ser configurado, e tanto o menu gráfico quanto o menu de
terminal (`app.menu`) são desenhados a partir dela.
"""

from __future__ import annotations

import dataclasses
import logging
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Callable, Optional

from app.config import (
    GameMode, PlayerColor,
    IPC_MODE, STOCKFISH_PATH, STOCKFISH_TIME_LIMIT,
    LICHESS_TIME_MINUTES, LICHESS_INCREMENT,
    C_PROCESS_PATH, C_PROCESS_ARGS, MOCK_PROCESS_PATH,
)
from app.lichess_client import is_board_time_control, explain_time_control

logger = logging.getLogger(__name__)

REPO_ROOT = Path(__file__).resolve().parent.parent

# Conta desafiada por padrão (a mesma do alvo `make random-sir`).
DEFAULT_OPPONENT = "random-sir"


# ---------------------------------------------------------------------------
#  Processo C — descoberta e compilação
# ---------------------------------------------------------------------------

def expected_board_input_path() -> Path:
    """Onde o binário do processo C é gravado pela compilação."""
    name = "board_input.exe" if sys.platform == "win32" else "board_input"
    return REPO_ROOT / "build" / name


def board_input_binary() -> Optional[str]:
    """Caminho do processo C, ou None se ele ainda não foi compilado.

    Um `$CHESS_C_PROCESS` apontando para outro binário continua valendo: quem
    configurou o ambiente à mão não deveria ser sobreposto pelo menu.
    """
    if C_PROCESS_PATH != MOCK_PROCESS_PATH:
        return C_PROCESS_PATH

    # Nas duas extensões: no Windows o binário sai com .exe, mas um build
    # feito no WSL/MSYS pode ter deixado o nome sem extensão.
    for name in ("board_input", "board_input.exe"):
        candidate = REPO_ROOT / "build" / name
        if candidate.is_file():
            return str(candidate)
    return None


def _wiringpi_available() -> bool:
    """Se o cabeçalho da wiringPi existe (mesmo teste do Makefile)."""
    return any(
        Path(p).is_file()
        for p in ("/usr/include/wiringPi.h", "/usr/local/include/wiringPi.h")
    )


def build_board_input() -> tuple[bool, str]:
    """Compila o processo C, como faz `make board-input`.

    Tenta o próprio make primeiro — é ele que define as flags de verdade — e
    só cai no compilador direto quando não há make na máquina (caso comum no
    Windows), para que a interface não fique dependendo de uma ferramenta a
    mais do que o necessário.

    Returns:
        Tupla (sucesso, mensagem) pronta para ser exibida no menu.
    """
    if shutil.which("make"):
        return _run_build(
            ["make", "board-input"], "make board-input"
        )

    compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
    if not compiler:
        return False, "Nem 'make' nem um compilador C foram encontrados no PATH."

    sources = sorted(str(p) for p in (REPO_ROOT / "src").glob("*.c"))
    if not sources:
        return False, "Nenhum fonte C encontrado em src/."

    (REPO_ROOT / "build").mkdir(exist_ok=True)
    command = [
        compiler, "-O2", "-Wall", "-Wextra", "-std=gnu11",
        "-o", str(expected_board_input_path()), *sources,
    ]
    if _wiringpi_available():
        command[1:1] = ["-DHAVE_WIRINGPI"]
        command.append("-lwiringPi")

    return _run_build(command, Path(compiler).name)


def _run_build(command: list[str], label: str) -> tuple[bool, str]:
    """Roda um comando de compilação e resume o resultado numa linha."""
    logger.info("Compilando o processo C: %s", " ".join(command))
    try:
        result = subprocess.run(
            command, cwd=REPO_ROOT, capture_output=True, text=True, timeout=300
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return False, f"Falha ao executar {label}: {exc}"

    if result.returncode == 0:
        return True, f"Processo C compilado ({label})."

    # A última linha de erro é a que interessa na barra de status; o resto vai
    # para o log, onde cabe.
    output = (result.stderr or result.stdout or "").strip()
    logger.error("Compilação falhou:\n%s", output)
    last = output.splitlines()[-1] if output else "sem saída"
    return False, f"Compilação falhou ({label}): {last}"


# ---------------------------------------------------------------------------
#  Configuração da partida
# ---------------------------------------------------------------------------

# Camadas de entrada oferecidas: o mock em Python e as duas camadas do
# processo C.
INPUT_MOCK = "mock"
INPUT_REED = "reed"
INPUT_KEYPAD = "keypad"

# De onde sai a partida no modo Lichess.
SOURCE_AI = "ai"
SOURCE_CHALLENGE = "challenge"
SOURCE_SEEK = "seek"
SOURCE_GAME = "game"


@dataclasses.dataclass
class LaunchConfig:
    """Tudo o que define uma partida, do modo de jogo à camada de entrada."""

    mode: str = "stockfish"                    # stockfish | lichess
    lichess_source: str = SOURCE_AI            # ai | challenge | seek | game
    color: str = "white"                       # white | black
    input_source: str = INPUT_MOCK             # mock | reed | keypad

    # Mock do hardware
    mock_mode: str = "gui"                     # gui | interactive | auto

    # Processo C
    keypad_keys: str = "gpio"                  # gpio | stdin
    keypad_auto_enter: bool = False
    keypad_lcd: bool = False
    board_input_args: str = ""                 # equivalente a BOARD_INPUT_ARGS

    # Stockfish
    stockfish_time: float = STOCKFISH_TIME_LIMIT
    stockfish_path: str = STOCKFISH_PATH

    # Lichess
    lichess_ai_level: int = 3
    lichess_opponent: str = DEFAULT_OPPONENT
    lichess_game_id: str = ""
    lichess_time: int = LICHESS_TIME_MINUTES
    lichess_increment: int = LICHESS_INCREMENT
    lichess_timeout: float = 180.0
    lichess_rated: bool = False

    # Gerais
    ipc_mode: str = IPC_MODE                   # subprocess | stdin | pipe
    flip: bool = False
    no_gui: bool = False
    log_level: str = "INFO"

    # -- Conversões ---------------------------------------------------------

    @property
    def game_mode(self) -> GameMode:
        return GameMode.STOCKFISH if self.mode == "stockfish" else GameMode.LICHESS

    @property
    def player_color(self) -> PlayerColor:
        return PlayerColor.WHITE if self.color == "white" else PlayerColor.BLACK

    @property
    def uses_c_process(self) -> bool:
        return self.input_source in (INPUT_REED, INPUT_KEYPAD)

    def hardware_path(self) -> str:
        """Caminho do processo que alimenta o IPC (mock ou processo C).

        Com o processo C ainda não compilado devolve o caminho esperado dele:
        quem valida isso é `issues()`, e assim a mensagem de erro fala do
        arquivo que falta em vez de um caminho vazio.
        """
        if not self.uses_c_process:
            return MOCK_PROCESS_PATH
        return board_input_binary() or str(expected_board_input_path())

    def hardware_args(self) -> list[str]:
        """Argumentos do processo de hardware, fora `--color`/`--flip`.

        A cor é acrescentada pela aplicação, que é quem sabe a cor final (no
        modo Lichess ela só é conhecida depois que a partida começa).
        """
        if not self.uses_c_process:
            return ["--mode", self.mock_mode]
        return ["--input", self.input_source, *self._c_process_flags()]

    def _c_process_flags(self) -> list[str]:
        """Opções do processo C fora a escolha da camada (`--input`)."""
        flags: list[str] = []
        if self.input_source == INPUT_KEYPAD:
            flags += ["--keys", self.keypad_keys]
            if self.keypad_auto_enter:
                flags.append("--auto-enter")
            if self.keypad_lcd:
                flags.append("--lcd")
        return flags + self.board_input_args.split()

    def app_kwargs(self, token: str, token_origin: str) -> dict[str, Any]:
        """Parâmetros de `ChessApplication` correspondentes a esta escolha."""
        is_lichess = self.game_mode == GameMode.LICHESS
        return {
            "mode": self.game_mode,
            "player_color": self.player_color,
            "ipc_mode": self.ipc_mode,
            "stockfish_path": self.stockfish_path or STOCKFISH_PATH,
            "stockfish_time": self.stockfish_time,
            "lichess_token": token,
            "lichess_token_origin": token_origin,
            # Cada origem de partida do Lichess é um parâmetro diferente da
            # aplicação, e só um deles vale por vez.
            "lichess_game_id": (
                (self.lichess_game_id or None)
                if is_lichess and self.lichess_source == SOURCE_GAME else None
            ),
            "lichess_ai_level": (
                self.lichess_ai_level
                if is_lichess and self.lichess_source == SOURCE_AI else None
            ),
            "lichess_challenge_user": (
                (self.lichess_opponent or None)
                if is_lichess and self.lichess_source == SOURCE_CHALLENGE else None
            ),
            "lichess_rated": self.lichess_rated,
            "lichess_time": self.lichess_time,
            "lichess_increment": self.lichess_increment,
            "lichess_timeout": self.lichess_timeout,
            "no_gui": self.no_gui,
            "flip_board": self.flip,
            "hardware_path": self.hardware_path(),
            "hardware_args": self.hardware_args(),
        }

    # -- Validação ----------------------------------------------------------

    def issues(self, token: str = "") -> list[tuple[str, str]]:
        """Problemas da configuração atual, do impeditivo ao meramente chato.

        Returns:
            Lista de tuplas (severidade, mensagem), com severidade "error"
            (impede a partida) ou "warning".
        """
        problems: list[tuple[str, str]] = []
        lichess = self.game_mode == GameMode.LICHESS

        if lichess and not token:
            problems.append((
                "error",
                "Token do Lichess não encontrado — salve um em .lichess_token "
                "(escopos board:play e challenge:write).",
            ))

        if lichess and self.lichess_source == SOURCE_CHALLENGE \
                and not self.lichess_opponent.strip():
            problems.append(
                ("error", "Informe a conta a desafiar em 'Oponente'.")
            )

        if lichess and self.lichess_source == SOURCE_GAME \
                and not self.lichess_game_id.strip():
            problems.append(
                ("error", "Informe o id da partida a retomar.")
            )

        if lichess and self.lichess_source != SOURCE_GAME \
                and not is_board_time_control(self.lichess_time, self.lichess_increment):
            explanation = explain_time_control(
                self.lichess_time, self.lichess_increment
            )
            # Mesma régua da linha de comando: para o seek é impeditivo; num
            # desafio o Lichess até cria a partida, mas ela não é jogável no
            # ritmo de um tabuleiro físico.
            severity = "error" if self.lichess_source == SOURCE_SEEK else "warning"
            problems.append((severity, explanation))

        if self.uses_c_process and board_input_binary() is None:
            problems.append((
                "error",
                f"Processo C não compilado ({expected_board_input_path().name}) "
                "— use 'Compilar processo C'.",
            ))

        if self.game_mode == GameMode.STOCKFISH:
            path = self.stockfish_path or STOCKFISH_PATH
            if not (Path(path).is_file() or shutil.which(path)):
                problems.append((
                    "warning",
                    f"Stockfish não encontrado em '{path}' — ajuste o caminho "
                    "ou defina CHESS_STOCKFISH_PATH.",
                ))

        return problems

    def blocking_issue(self, token: str = "") -> Optional[str]:
        """A primeira mensagem que impede a partida de começar, se houver."""
        for severity, message in self.issues(token):
            if severity == "error":
                return message
        return None

    # -- Descrições ---------------------------------------------------------

    def summary(self) -> str:
        """Resumo de uma linha do que vai ser jogado."""
        if self.game_mode == GameMode.STOCKFISH:
            opponent = f"Stockfish ({self.stockfish_time:g}s por lance)"
        elif self.lichess_source == SOURCE_AI:
            opponent = f"IA do Lichess nível {self.lichess_ai_level}"
        elif self.lichess_source == SOURCE_CHALLENGE:
            opponent = f"desafio a {self.lichess_opponent or '?'}"
        elif self.lichess_source == SOURCE_GAME:
            opponent = f"partida {self.lichess_game_id or '?'}"
        else:
            opponent = "oponente humano (seek)"

        entrada = {
            INPUT_MOCK: f"mock ({self.mock_mode})",
            INPUT_REED: "reed switches",
            INPUT_KEYPAD: f"teclado 4x4 ({self.keypad_keys})",
        }[self.input_source]

        cor = "brancas" if self.color == "white" else "pretas"
        return f"{opponent} · peças {cor} · entrada: {entrada}"

    def equivalent_command(self) -> str:
        """O comando de terminal que faz o mesmo que esta configuração.

        Serve de ponte para quem já conhece os alvos do Makefile (e para
        reproduzir a partida sem passar pelo menu). Quase tudo cabe num alvo
        com variáveis; o que não cabe — o modo do mock, que o Makefile só
        expõe no alvo `mock` — vira a linha do `python -m app.main`.
        """
        target = self._make_target()
        if target is None:
            return self.cli_command()

        variables = [f"COLOR={self.color}", *self._make_variables()]
        extras = self._extra_cli_flags(include_hardware=False)
        if extras:
            variables.append(f'ARGS="{" ".join(extras)}"')
        return " ".join(["make", target, *variables])

    def cli_command(self) -> str:
        """A linha completa do `python -m app.main` equivalente."""
        return "python -m app.main --no-menu " + " ".join(self.cli_arguments())

    def cli_arguments(self) -> list[str]:
        """Os argumentos de `app.main` que reproduzem esta configuração."""
        args = ["--mode", self.mode, "--color", self.color]

        if self.game_mode == GameMode.STOCKFISH:
            args += ["--stockfish-time", f"{self.stockfish_time:g}"]
            if self.stockfish_path:
                args += ["--stockfish-path", self.stockfish_path]
        else:
            if self.lichess_source == SOURCE_AI:
                args += ["--lichess-ai", str(self.lichess_ai_level)]
            elif self.lichess_source == SOURCE_CHALLENGE:
                args += ["--lichess-challenge", self.lichess_opponent]
            elif self.lichess_source == SOURCE_GAME:
                args += ["--lichess-game", self.lichess_game_id]
            if self.lichess_source != SOURCE_GAME:
                args += [
                    "--lichess-time", str(self.lichess_time),
                    "--lichess-increment", str(self.lichess_increment),
                ]
            if self.lichess_source in (SOURCE_CHALLENGE, SOURCE_SEEK):
                args += ["--lichess-timeout", f"{self.lichess_timeout:g}"]

        args += ["--log-level", self.log_level]
        return args + self._extra_cli_flags()

    def _extra_cli_flags(self, include_hardware: bool = True) -> list[str]:
        """Opções sem variável própria no Makefile (vão pelo ARGS dele).

        Args:
            include_hardware: Se as opções da camada de entrada entram na
                lista. Num alvo do Makefile elas já vêm por INPUT_LAYER e
                BOARD_INPUT_ARGS, e repeti-las no ARGS seria conflito.
        """
        extras: list[str] = []
        if self.flip:
            extras.append("--flip")
        if self.no_gui:
            extras.append("--no-gui")
        if self.lichess_rated and self.game_mode == GameMode.LICHESS:
            extras.append("--lichess-rated")
        if self.ipc_mode != IPC_MODE:
            extras += ["--ipc", self.ipc_mode]

        if not self.uses_c_process:
            if self.mock_mode != "gui":
                extras += ["--mock-mode", self.mock_mode]
        elif include_hardware:
            extras += ["--input", self.input_source]
            board_args = self._c_process_flags()
            if board_args:
                extras += ["--board-input-args", f'"{" ".join(board_args)}"']

        return extras

    def _make_target(self) -> Optional[str]:
        """O alvo do Makefile desta configuração, ou None se não houver um."""
        if self.game_mode == GameMode.STOCKFISH:
            if self.input_source == INPUT_KEYPAD:
                return "keypad"
            return "stockfish-hw" if self.uses_c_process else "stockfish"

        if self.lichess_source == SOURCE_AI:
            return "lichess-ai-hw" if self.uses_c_process else "lichess-ai"

        # Os demais modos do Lichess não têm alvo com o processo C.
        if self.uses_c_process:
            return None
        return {
            SOURCE_CHALLENGE: "lichess-user",
            SOURCE_GAME: "lichess-game",
        }.get(self.lichess_source, "lichess-seek")

    def _make_variables(self) -> list[str]:
        """As variáveis a passar ao alvo escolhido, fora COLOR."""
        variables: list[str] = []

        if self.game_mode == GameMode.STOCKFISH:
            if self.stockfish_time != STOCKFISH_TIME_LIMIT:
                variables.append(f"STOCKFISH_TIME={self.stockfish_time:g}")
            if self.stockfish_path != STOCKFISH_PATH:
                variables.append(f"STOCKFISH_PATH={self.stockfish_path}")
        else:
            if self.lichess_source == SOURCE_AI:
                variables.append(f"LICHESS_LEVEL={self.lichess_ai_level}")
            elif self.lichess_source == SOURCE_CHALLENGE:
                variables.append(f"OPPONENT={self.lichess_opponent}")
            elif self.lichess_source == SOURCE_GAME:
                variables.append(f"GAME={self.lichess_game_id}")
            if self.lichess_source != SOURCE_GAME:
                if self.lichess_time != LICHESS_TIME_MINUTES:
                    variables.append(f"LICHESS_TIME={self.lichess_time}")
                if self.lichess_increment != LICHESS_INCREMENT:
                    variables.append(f"LICHESS_INC={self.lichess_increment}")
            if self.lichess_timeout != 180.0:
                variables.append(f"LICHESS_TIMEOUT={self.lichess_timeout:g}")

        if self.uses_c_process:
            # `make keypad` já fixa a camada; nos alvos `-hw` ela é variável.
            if self._make_target() != "keypad":
                variables.append(f"INPUT_LAYER={self.input_source}")
            board_args = self._c_process_flags()
            if board_args:
                variables.append(f'BOARD_INPUT_ARGS="{" ".join(board_args)}"')

        if self.log_level != "INFO":
            variables.append(f"LOG_LEVEL={self.log_level}")

        return variables

    # -- Origem na linha de comando ----------------------------------------

    @classmethod
    def from_namespace(cls, args) -> "LaunchConfig":
        """Configuração inicial a partir dos argumentos da linha de comando.

        Assim o menu abre já com o que foi pedido no terminal (ou nos alvos do
        Makefile), em vez de descartar essas opções.
        """
        config = cls(
            mode=args.mode,
            color=args.color,
            ipc_mode=args.ipc,
            mock_mode=args.mock_mode,
            stockfish_path=args.stockfish_path,
            stockfish_time=args.stockfish_time,
            lichess_rated=args.lichess_rated,
            lichess_time=args.lichess_time,
            lichess_increment=args.lichess_increment,
            lichess_timeout=args.lichess_timeout,
            flip=args.flip,
            no_gui=args.no_gui,
            log_level=args.log_level,
        )

        if args.lichess_game:
            config.lichess_source = SOURCE_GAME
            config.lichess_game_id = args.lichess_game
        elif args.lichess_challenge:
            config.lichess_source = SOURCE_CHALLENGE
            config.lichess_opponent = args.lichess_challenge
        elif args.lichess_ai is not None:
            config.lichess_source = SOURCE_AI
            config.lichess_ai_level = args.lichess_ai
        elif args.mode == "lichess":
            config.lichess_source = SOURCE_SEEK

        if args.input:
            config.input_source = args.input
            config._adopt_c_args(shlex.split(args.board_input_args or ""))
        else:
            config._adopt_environment()
        return config

    def _adopt_environment(self) -> None:
        """Herda a camada de entrada configurada por variável de ambiente.

        É assim que os alvos `-hw` do Makefile escolhem o processo C; sem isto
        o menu abriria no mock mesmo tendo sido chamado por `make keypad`.
        """
        if C_PROCESS_PATH == MOCK_PROCESS_PATH:
            return

        self.input_source = INPUT_REED
        self._adopt_c_args(list(C_PROCESS_ARGS))

    def _adopt_c_args(self, tokens: list[str]) -> None:
        """Lê opções do processo C para os campos correspondentes.

        O que o menu não conhece continua valendo: sobra em `board_input_args`
        e é repassado ao binário como veio.
        """
        leftover: list[str] = []
        index = 0
        while index < len(tokens):
            token = tokens[index]
            value = tokens[index + 1] if index + 1 < len(tokens) else None

            if token == "--input" and value in (INPUT_REED, INPUT_KEYPAD):
                self.input_source = value
                index += 2
            elif token == "--keys" and value in ("gpio", "stdin"):
                self.keypad_keys = value
                index += 2
            elif token == "--auto-enter":
                self.keypad_auto_enter = True
                index += 1
            elif token == "--lcd":
                self.keypad_lcd = True
                index += 1
            else:
                leftover.append(token)
                index += 1

        self.board_input_args = " ".join(leftover)


# ---------------------------------------------------------------------------
#  Opções do menu
# ---------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Option:
    """Uma linha configurável do menu.

    `kind` decide como o valor é editado: "choice" e "bool" giram com as
    setas, "int" e "float" giram e também aceitam digitação, e "text" só é
    editado digitando.
    """

    field: str
    label: str
    kind: str                                    # choice|bool|int|float|text
    choices: tuple[str, ...] = ()
    labels: dict[Any, str] = dataclasses.field(default_factory=dict)
    step: float = 1.0
    minimum: float = 0.0
    maximum: float = 1_000_000.0
    hint: str = ""
    visible: Optional[Callable[[LaunchConfig], bool]] = None

    def is_visible(self, config: LaunchConfig) -> bool:
        return self.visible is None or self.visible(config)

    def display(self, config: LaunchConfig) -> str:
        """Valor atual formatado para a tela."""
        value = getattr(config, self.field)
        if self.kind == "bool":
            return "sim" if value else "não"
        if self.kind == "float":
            return self.labels.get(value) or f"{value:g}"
        if self.kind == "text":
            return str(value) if str(value) else "(vazio)"
        return self.labels.get(value, str(value))

    def cycle(self, config: LaunchConfig, delta: int) -> None:
        """Avança (ou recua) o valor desta opção."""
        value = getattr(config, self.field)

        if self.kind == "bool":
            setattr(config, self.field, not value)
            return

        if self.kind == "choice":
            options = self.choices
            index = options.index(value) if value in options else 0
            setattr(config, self.field, options[(index + delta) % len(options)])
            return

        if self.kind in ("int", "float"):
            new = value + delta * self.step
            new = max(self.minimum, min(self.maximum, new))
            setattr(
                config, self.field,
                int(new) if self.kind == "int" else round(float(new), 2),
            )

    def editable_text(self) -> bool:
        """Se esta opção pode ser preenchida digitando."""
        return self.kind in ("text", "int", "float")

    def apply_text(self, config: LaunchConfig, text: str) -> bool:
        """Aplica um valor digitado.

        Returns:
            False se o texto não vale para esta opção (o valor antigo fica).
        """
        text = text.strip()
        if self.kind == "text":
            setattr(config, self.field, text)
            return True

        if not text:
            return False
        try:
            number = float(text)
        except ValueError:
            return False
        if not self.minimum <= number <= self.maximum:
            return False
        setattr(
            config, self.field,
            int(number) if self.kind == "int" else round(number, 2),
        )
        return True


def _is_lichess(config: LaunchConfig) -> bool:
    return config.mode == "lichess"


def _is_stockfish(config: LaunchConfig) -> bool:
    return config.mode == "stockfish"


OPTIONS: tuple[Option, ...] = (
    Option(
        "mode", "Modo de jogo", "choice",
        choices=("stockfish", "lichess"),
        labels={"stockfish": "Stockfish local", "lichess": "Lichess (online)"},
        hint="Contra a engine na própria máquina ou pela Board API do Lichess.",
    ),
    Option(
        "lichess_source", "Partida no Lichess", "choice",
        choices=(SOURCE_AI, SOURCE_CHALLENGE, SOURCE_SEEK, SOURCE_GAME),
        labels={
            SOURCE_AI: "IA do Lichess",
            SOURCE_CHALLENGE: "desafiar uma conta",
            SOURCE_SEEK: "procurar um humano",
            SOURCE_GAME: "retomar partida",
        },
        hint="Quem será o oponente online.",
        visible=_is_lichess,
    ),
    Option(
        "lichess_ai_level", "Nível da IA", "int",
        minimum=1, maximum=8,
        hint="1 (mais fraca) a 8 (mais forte).",
        visible=lambda c: _is_lichess(c) and c.lichess_source == SOURCE_AI,
    ),
    Option(
        "lichess_opponent", "Oponente", "text",
        hint="Conta a desafiar. Enter edita; o desafio precisa ser aceito lá.",
        visible=lambda c: _is_lichess(c) and c.lichess_source == SOURCE_CHALLENGE,
    ),
    Option(
        "lichess_game_id", "Id da partida", "text",
        hint="Partida já em andamento na conta (ex: AbCdEfGh).",
        visible=lambda c: _is_lichess(c) and c.lichess_source == SOURCE_GAME,
    ),
    Option(
        "lichess_time", "Tempo (min)", "int",
        minimum=1, maximum=180,
        hint="A Board API exige o equivalente a 8+0 ou mais lento.",
        visible=lambda c: _is_lichess(c) and c.lichess_source != SOURCE_GAME,
    ),
    Option(
        "lichess_increment", "Incremento (s)", "int",
        minimum=0, maximum=180,
        hint="Segundos somados ao relógio a cada lance.",
        visible=lambda c: _is_lichess(c) and c.lichess_source != SOURCE_GAME,
    ),
    Option(
        "lichess_timeout", "Espera máx. (s)", "float",
        step=30, minimum=30, maximum=1800,
        hint="Tempo máximo esperando o oponente aceitar ou aparecer.",
        visible=lambda c: _is_lichess(c) and c.lichess_source in (
            SOURCE_CHALLENGE, SOURCE_SEEK
        ),
    ),
    Option(
        "lichess_rated", "Ranqueada", "bool",
        hint="Partida valendo rating (padrão: casual).",
        visible=lambda c: _is_lichess(c) and c.lichess_source in (
            SOURCE_CHALLENGE, SOURCE_SEEK
        ),
    ),
    Option(
        "stockfish_time", "Tempo por lance (s)", "float",
        step=0.5, minimum=0.1, maximum=30.0,
        hint="Quanto o Stockfish pensa em cada jogada.",
        visible=_is_stockfish,
    ),
    Option(
        "stockfish_path", "Binário do Stockfish", "text",
        hint="Vazio usa o stockfish do PATH ou $CHESS_STOCKFISH_PATH.",
        visible=_is_stockfish,
    ),
    Option(
        "color", "Cor das peças físicas", "choice",
        choices=("white", "black"),
        labels={"white": "brancas", "black": "pretas"},
        hint="No seek do Lichess quem sorteia a cor é o pareamento.",
    ),
    Option(
        "input_source", "Entrada do tabuleiro", "choice",
        choices=(INPUT_MOCK, INPUT_REED, INPUT_KEYPAD),
        labels={
            INPUT_MOCK: "mock (sem hardware)",
            INPUT_REED: "reed switches (processo C)",
            INPUT_KEYPAD: "teclado 4x4 (processo C)",
        },
        hint="De onde vêm os lances: simulação ou o hardware pelo processo C.",
    ),
    Option(
        "mock_mode", "Modo do mock", "choice",
        choices=("gui", "interactive", "auto"),
        labels={
            "gui": "matriz de botões",
            "interactive": "terminal",
            "auto": "eventos aleatórios",
        },
        hint="Como o mock do hardware é operado.",
        visible=lambda c: c.input_source == INPUT_MOCK,
    ),
    Option(
        "keypad_keys", "Teclas do keypad", "choice",
        choices=("gpio", "stdin"),
        labels={"gpio": "GPIO (hardware)", "stdin": "terminal (sem hardware)"},
        hint="'terminal' permite testar o plano B sem Raspberry Pi.",
        visible=lambda c: c.input_source == INPUT_KEYPAD,
    ),
    Option(
        "keypad_auto_enter", "Enviar sem '#'", "bool",
        hint="Envia o lance na quarta tecla, sem confirmação.",
        visible=lambda c: c.input_source == INPUT_KEYPAD,
    ),
    Option(
        "keypad_lcd", "Display 16x2 (I2C)", "bool",
        hint="Ecoa no LCD o lance que está sendo digitado.",
        visible=lambda c: c.input_source == INPUT_KEYPAD,
    ),
    Option(
        "board_input_args", "Args extras do processo C", "text",
        hint="Repassados ao binário (ex: --poll-ms 5 --active-low).",
        visible=lambda c: c.uses_c_process,
    ),
    Option(
        "flip", "Inverter o tabuleiro", "bool",
        hint="Por padrão ele é desenhado da perspectiva do jogador físico.",
    ),
    Option(
        "no_gui", "Sem janela do tabuleiro", "bool",
        hint="Joga só com o log no terminal (útil por SSH, sem display).",
    ),
    Option(
        "ipc_mode", "IPC", "choice",
        choices=("subprocess", "stdin", "pipe"),
        hint="Como os eventos chegam do processo de hardware.",
    ),
    Option(
        "log_level", "Nível de log", "choice",
        choices=("DEBUG", "INFO", "WARNING", "ERROR"),
        hint="Detalhamento das mensagens no terminal.",
    ),
)


def visible_options(config: LaunchConfig) -> list[Option]:
    """As opções que fazem sentido para a configuração atual."""
    return [option for option in OPTIONS if option.is_visible(config)]
