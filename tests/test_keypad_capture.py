"""Peça capturada pelo oponente sai sozinha do espelho, no modo teclado.

São dois lados do mesmo mecanismo, e a suíte cobre os dois:

  A. O processo C aceita `@sync|<64 casas>` por stdin, adota aquele espelho e
     não devolve evento nenhum por causa disso. É o que faz uma recaptura na
     casa esvaziada deixar de ser recusada como "Destino ocupado".

  B. A aplicação decide quando mandar esse espelho (só no teclado, nunca na
     matriz de reed) e tira do dela a peça que o oponente capturou.

A parte A precisa do binário compilado (`make board-input`); sem ele só a
parte B roda, e a suíte diz que pulou em vez de falhar.
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO))

import chess  # noqa: E402

from app.config import GameMode, PlayerColor  # noqa: E402
from app.ipc_reader import parse_entry  # noqa: E402
from app.main import ChessApplication, SYNC_SQUARES  # noqa: E402

BUILD = REPO / "build"
BINARY = next(
    (path for path in (BUILD / "board_input", BUILD / "board_input.exe")
     if path.exists()),
    None,
)

failures = []


def check(label, condition, extra=""):
    print(f"  {'PASS' if condition else 'FAIL'}  {label} {extra}")
    if not condition:
        failures.append(label)


# ---------------------------------------------------------------------------
#  Parte A — o processo C adota o espelho que chega por stdin
# ---------------------------------------------------------------------------

def mirror_payload(occupied: set[str]) -> str:
    """Serializa um espelho no formato do comando `@sync`."""
    return "".join("1" if sq in occupied else "0" for sq in SYNC_SQUARES)


def initial_white() -> set[str]:
    """As casas ocupadas no começo de uma partida, jogando de brancas."""
    return {sq for sq in SYNC_SQUARES if sq[1] in "12"}


def run_c(stdin: str) -> tuple[list[str], list[str]]:
    """Roda o processo C com `stdin` e separa eventos de linhas de digitação."""
    result = subprocess.run(
        [str(BINARY), "--input", "keypad", "--keys", "stdin",
         "--color", "white", "--no-lcd"],
        input=stdin, capture_output=True, text=True, timeout=15,
    )
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    events = [line for line in lines if parse_entry(line) is None]
    return events, lines


def sync_line(occupied: set[str]) -> str:
    return f"@sync|{mirror_payload(occupied)}\n"


if BINARY is None:
    print(f"Binário do processo C não encontrado em {BUILD} — parte A pulada.")
    print("Compile com `make board-input` para rodá-la.\n")
else:
    print("--- O espelho do processo C aceita a correção da aplicação ---")

    # e2e4 e, na sequência, uma peça de d2 indo para e4. Sem a correção do
    # espelho, e4 está ocupada pela peça que acabou de chegar lá e o processo
    # C recusa o segundo lance — é exatamente o que acontece hoje quando o
    # oponente captura em e4 e o jogador quer recapturar.
    sem_sync, _ = run_c("AA2AA4#" "D2AA4#")
    check("sem a correção, a recaptura é recusada",
          sem_sync == ["e2:0,e4:1"], f"-- {sem_sync}")

    # O mesmo roteiro, com a aplicação avisando que e4 esvaziou (o oponente
    # capturou a peça que estava lá).
    depois = initial_white() - {"e2", "e4"}
    com_sync, _ = run_c("AA2AA4#\n" + sync_line(depois) + "D2AA4#")
    check("com a correção, a recaptura passa",
          com_sync == ["e2:0,e4:1", "d2:0,e4:1"], f"-- {com_sync}")

    # O canal é de mão única: o que a aplicação manda não pode voltar como
    # evento, senão ela leria como lance o que ela mesma acabou de dizer.
    so_sync, _ = run_c(sync_line(initial_white() - {"d2"}))
    check("a correção não gera evento de volta", so_sync == [], f"-- {so_sync}")

    # Mas o LCD conta o que aconteceu: é assim que o jogador sabe qual peça
    # tirar da mesa.
    _, linhas = run_c(sync_line(initial_white() - {"d2"}))
    entradas = [parse_entry(line) for line in linhas]
    check("a correção aparece no LCD",
          any(e and "d2" in e.status.lower() for e in entradas),
          f"-- {[e.status for e in entradas if e]}")

    print("\n--- Comandos malformados não mexem no espelho ---")

    # Meio espelho aplicado seria pior que espelho nenhum: o processo C valida
    # o payload inteiro antes de tocar em qualquer casa.
    curto, _ = run_c("@sync|0101\n" "AA2AA4#")
    check("payload curto é ignorado", curto == ["e2:0,e4:1"], f"-- {curto}")

    invalido, _ = run_c("@sync|" + "x" * 64 + "\n" "AA2AA4#")
    check("payload com caractere inválido é ignorado",
          invalido == ["e2:0,e4:1"], f"-- {invalido}")

    desconhecido, _ = run_c("@nada|1\n" "AA2AA4#")
    check("comando desconhecido é ignorado", desconhecido == ["e2:0,e4:1"],
          f"-- {desconhecido}")

    # As teclas continuam entrando pelo mesmo stdin: uma linha '@' não pode
    # engolir o que vem depois dela.
    misturado, _ = run_c("AA2\n" + sync_line(initial_white()) + "AA4#")
    check("as teclas sobrevivem a um comando no meio",
          misturado == ["e2:0,e4:1"], f"-- {misturado}")


# ---------------------------------------------------------------------------
#  Parte B — a aplicação decide quando mandar o espelho
# ---------------------------------------------------------------------------

class FakeIPC:
    """Guarda o que a aplicação mandaria ao processo de hardware."""

    mode = "subprocess"

    def __init__(self):
        self.sent = []
        self.pipe_stdin = False

    def set_process_args(self, args):
        pass

    def set_pipe_stdin(self, pipe_stdin):
        self.pipe_stdin = pipe_stdin

    def send_to_process(self, message):
        self.sent.append(message)

    def start(self):
        pass

    def stop(self):
        pass

    def read_event(self, timeout=0.05):
        return None

    def read_entry(self):
        return None


def build_app(hardware_args, hardware_path=str(REPO / "build" / "board_input")):
    app = ChessApplication(
        mode=GameMode.STOCKFISH,
        player_color=PlayerColor.WHITE,
        no_gui=True,
        hardware_path=hardware_path,
        hardware_args=hardware_args,
    )
    app.ipc_reader = FakeIPC()
    return app


print("\n--- Quando a aplicação usa o canal ---")

# A matriz de reed lê o tabuleiro de verdade: sobrescrever o que ela leu
# esconderia do jogador a peça que ele precisa tirar da mesa.
cases = [
    ("teclado no GPIO usa o canal", ["--input", "keypad"], True),
    ("teclado no GPIO, --keys explícito",
     ["--input", "keypad", "--keys", "gpio"], True),
    ("teclado por stdin não usa (o canal é das teclas)",
     ["--input", "keypad", "--keys", "stdin"], False),
    ("matriz de reed não usa", ["--input", "reed"], False),
    ("sem camada escolhida não usa", [], False),
]
for label, args, expected in cases:
    app = build_app(args)
    check(label, app._supports_mirror_sync(app._hardware_process_args()) is expected)

# O mock tem tabuleiro próprio e não entende o comando.
app = build_app(["--input", "keypad"], hardware_path=str(REPO / "mock" / "hardware_mock.py"))
check("o mock não usa o canal",
      app._supports_mirror_sync(app._hardware_process_args()) is False)


print("\n--- A peça capturada sai do espelho sozinha ---")

app = build_app(["--input", "keypad"])
app._mirror_sync = True

# 1.e4 d5 2.exd5 Qxd5 — a dama preta captura o peão branco de d5, que está na
# mesa e no espelho e que nenhuma tecla vai retirar.
for uci in ("e2e4", "d7d5", "e4d5"):
    app.game_state.apply_move(chess.Move.from_uci(uci))
app._sync_mirror_to_board()

check("antes da captura, o peão está no espelho",
      app.physical_board_state["d5"] is True)

app.game_state.apply_move(chess.Move.from_uci("d8d5"))
app.ipc_reader.sent.clear()
app._absorb_captured_pieces()

check("a casa capturada esvazia no espelho da aplicação",
      app.physical_board_state["d5"] is False)
check("o espelho vai para o processo C",
      len(app.ipc_reader.sent) == 1
      and app.ipc_reader.sent[0].startswith("@sync|"),
      f"-- {app.ipc_reader.sent}")

payload = app.ipc_reader.sent[0][len("@sync|"):]
check("o espelho enviado tem as 64 casas", len(payload) == 64, f"-- {len(payload)}")
check("e d5 vai como vazia",
      payload[SYNC_SQUARES.index("d5")] == "0")
check("enquanto e4, que o jogador esvaziou jogando, também",
      payload[SYNC_SQUARES.index("e4")] == "0")
check("e as peças que ficaram continuam ocupadas",
      payload[SYNC_SQUARES.index("e1")] == "1"
      and payload[SYNC_SQUARES.index("a2")] == "1")

# Nada a absorver: o canal fica quieto. Sem isto, todo lance do oponente
# mandaria um espelho igual ao anterior.
app.ipc_reader.sent.clear()
app._absorb_captured_pieces()
check("sem captura, nada é enviado", app.ipc_reader.sent == [],
      f"-- {app.ipc_reader.sent}")

# O tabuleiro deixa de pedir a remoção: para a aplicação, aquela casa está
# resolvida. O que resta ao jogador é físico, e vira aviso passageiro.
missing, extra = app._board_diff()
check("a instrução de remover some", (missing, extra) == ([], []),
      f"-- faltam {missing}, sobram {extra}")


print("\n--- Na matriz de reed nada muda ---")

app = build_app(["--input", "reed"])
app._mirror_sync = app._supports_mirror_sync(app._hardware_process_args())
for uci in ("e2e4", "d7d5", "e4d5"):
    app.game_state.apply_move(chess.Move.from_uci(uci))
app._sync_mirror_to_board()
app.game_state.apply_move(chess.Move.from_uci("d8d5"))
app._absorb_captured_pieces()

check("o peão capturado continua no espelho até o jogador tirá-lo",
      app.physical_board_state["d5"] is True)
_missing, extra = app._board_diff()
check("e a instrução física continua sendo pedida", extra == ["d5"], f"-- {extra}")
check("nada foi mandado ao processo C", app.ipc_reader.sent == [],
      f"-- {app.ipc_reader.sent}")


# ---------------------------------------------------------------------------

print(f"\n{'=' * 70}")
if failures:
    print(f"FALHARAM {len(failures)}: {', '.join(failures)}")
    sys.exit(1)
print("Retirada automática da peça capturada OK.")
