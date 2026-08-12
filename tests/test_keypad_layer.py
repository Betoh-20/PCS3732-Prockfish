"""Camada de teclado do processo C: teclas digitadas → eventos IPC.

Roda o binário `build/board_input` em `--input keypad --keys stdin`, que lê as
teclas do terminal em vez do GPIO, e confere os eventos que ele emite. É o
mesmo caminho de código do teclado de verdade — só a origem das teclas muda —
então isto cobre o multi-toque das colunas, os comandos e as recusas locais
sem precisar de Raspberry Pi.

A suíte é pulada (sem falhar) quando o binário não foi compilado:

    make board-input
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD = REPO / "build"

sys.path.insert(0, str(REPO))
# O mesmo parser que a aplicação usa: assim as linhas "@entry" são conferidas
# pelos dois lados do protocolo, e não só pela forma que o C escreve.
from app.ipc_reader import parse_entry  # noqa: E402

# O `.exe` cobre o binário compilado no Windows para testar a lógica do
# teclado fora do Raspberry Pi (o GPIO não entra neste caminho de código).
BINARY = next(
    (path for path in (BUILD / "board_input", BUILD / "board_input.exe")
     if path.exists()),
    None,
)

if BINARY is None:
    print(f"Binário do processo C não encontrado em {BUILD}.")
    print("Compile com `make board-input` para rodar esta suíte. Pulando.")
    sys.exit(0)

failures = []


def check(label, condition, extra=""):
    print(f"  {'PASS' if condition else 'FAIL'}  {label} {extra}")
    if not condition:
        failures.append(label)


def run_keys_all(keys: str, color: str = "white") -> list[str]:
    """Digita `keys` no processo C e devolve todas as linhas que ele emitiu.

    A saída usa só ASCII: o console do Windows (cp1252) não codifica as setas
    e a suíte morreria no primeiro `print` em vez de reportar o resultado.
    """
    result = subprocess.run(
        [str(BINARY), "--input", "keypad", "--keys", "stdin",
         "--color", color, "--no-lcd"],
        input=keys,
        capture_output=True,
        text=True,
        timeout=15,
    )
    return [line for line in result.stdout.splitlines() if line.strip()]


def run_keys(keys: str, color: str = "white") -> list[str]:
    """Só os eventos de sensor — o canal que a camada reed também usa."""
    return [
        line for line in run_keys_all(keys, color)
        if parse_entry(line) is None
    ]


def run_entries(keys: str, color: str = "white") -> list:
    """Só as linhas de digitação, já desserializadas em `KeypadEntry`."""
    parsed = (parse_entry(line) for line in run_keys_all(keys, color))
    return [entry for entry in parsed if entry is not None]


def expect(label, keys, events, color="white"):
    """Confere a lista completa de eventos gerada por uma sequência de teclas."""
    produced = run_keys(keys, color)
    check(label, produced == events, f"-- teclas {keys!r} -> {produced}")


print("\n--- Colunas: tecla simples e tecla repetida ---")

# A tecla repetida vale pela letra seguinte do bloco: AA=e, BB=f, CC=g, DD=h.
expect("a2a4 com teclas simples", "A2A4#", ["a2:0,a4:1"])
expect("e2e4 com tecla repetida", "AA2AA4#", ["e2:0,e4:1"])
expect("b1c3 (cavalo), teclas simples", "B1C3#", ["b1:0,c3:1"])
expect("g1f3 (cavalo), CC e BB", "CC1BB3#", ["g1:0,f3:1"])
expect("d2h6 mistura os blocos", "D2DD6#", ["d2:0,h6:1"])

# O terceiro toque volta para a letra simples — dá para corrigir sem apagar.
expect("terceiro toque volta a 'a'", "AAA2AAA4#", ["a2:0,a4:1"])

print("\n--- Correção com '*' ---")

expect("apaga a fileira errada", "AA2AA3*4#", ["e2:0,e4:1"])
expect("apaga tudo e redigita", "AA2****A2A4#", ["a2:0,a4:1"])
expect("'#' com entrada incompleta não emite", "AA2#", [])

print("\n--- Recusas locais (o que o tabuleiro físico também recusaria) ---")

# Sem peça na origem: no tabuleiro físico não haveria o que levantar.
expect("origem vazia não emite", "AA5AA6#", [])
# Destino com peça do próprio jogador: não existe lance assim.
expect("destino ocupado não emite", "A1A2#", [])
expect("origem igual ao destino não emite", "AA2AA2#", [])

print("\n--- Comandos ---")

# 0-1: retirar a peça (é o que a aplicação pede quando o oponente captura).
expect("comando 0-1 retira a peça", "01AA2#", ["e2:0"])
# 0-2: colocar uma peça numa casa vazia.
expect("comando 0-2 coloca a peça", "02AA5#", ["e5:1"])
expect("0-1 numa casa já vazia não emite", "01AA5#", [])
expect("0-2 numa casa já ocupada não emite", "02AA2#", [])

print("\n--- Sequências de partida ---")

# Depois de e2e4 a casa e2 fica vazia e e4 ocupada: um segundo lance de e2
# é recusado, e um lance a partir de e4 é aceito.
expect(
    "espelho acompanha os lances",
    "AA2AA4#" "AA4AA5#",
    ["e2:0,e4:1", "e4:0,e5:1"],
)

# Roque curto: rei e depois torre, como no tabuleiro físico. O cavalo de g1 e
# o bispo de f1 saem antes (abertura italiana), senão as casas estariam
# ocupadas e o próprio processo C recusaria.
expect(
    "roque curto em dois lances",
    "AA2AA4#" "CC1BB3#" "BB1C4#" "AA1CC1#" "DD1BB1#",
    ["e2:0,e4:1", "g1:0,f3:1", "f1:0,c4:1", "e1:0,g1:1", "h1:0,f1:1"],
)

# Jogando de pretas, as peças começam nas fileiras 7 e 8.
expect("de pretas, e7e5", "AA7AA5#", ["e7:0,e5:1"], color="black")
expect("de pretas, e2 está vazia", "AA2AA4#", [], color="black")

print("\n--- Reenvio do estado completo ---")

resync = run_keys("09#")
check(
    "0-9 reenvia as 64 casas",
    len(resync) == 1 and len(resync[0].split(",")) == 64,
    f"-- {len(resync)} linha(s)",
)
check(
    "0-9 traz as fileiras 1 e 2 ocupadas",
    bool(resync) and "e2:1" in resync[0] and "e5:0" in resync[0],
)

print("\n--- Digitação em andamento (linhas '@entry') ---")

# A cada tecla sai uma linha contando o buffer: é o que a aplicação mostra na
# barra de status e usa para destacar os destinos da peça escolhida.
digitando = run_entries("AA2")

# Um binário compilado antes dessa parte do protocolo não emite linha nenhuma;
# falhar aqui só mandaria procurar bug onde não há um.
if not digitando:
    print("  Binário sem as linhas '@entry' — recompile com `make board-input`.")
    print(f"\n{'=' * 70}")
    if failures:
        print(f"FALHARAM {len(failures)}: {', '.join(failures)}")
        sys.exit(1)
    print("Camada de teclado OK (digitação não conferida).")
    sys.exit(0)

check(
    "cada tecla emite uma linha de digitação",
    len(digitando) == 4,          # a inicial + uma por tecla
    f"-- {len(digitando)} linha(s)",
)
check(
    "origem sai quando a casa fica completa",
    bool(digitando) and digitando[-1].origin == "e2"
    and digitando[-1].target is None,
    f"-- {digitando[-1] if digitando else None}",
)
check(
    "o texto digitado acompanha as teclas",
    bool(digitando) and digitando[-1].text.endswith("E2_"),
    f"-- {digitando[-1].text if digitando else None!r}",
)

meio = run_entries("AA2A")
check(
    "casa incompleta não vira origem nova",
    meio[-1].origin == "e2" and meio[-1].target is None,
    f"-- {meio[-1]}",
)

completo = run_entries("AA2AA4")
check(
    "destino sai na quarta tecla",
    completo[-1].origin == "e2" and completo[-1].target == "e4",
    f"-- {completo[-1]}",
)

enviado = run_entries("AA2AA4#")
check(
    "'#' limpa a digitação",
    not enviado[-1].active and enviado[-1].origin is None
    and enviado[-1].target is None,
    f"-- {enviado[-1]}",
)

# Nos comandos a casa digitada não é uma peça escolhida: prever lances a
# partir dela seria mentira.
comando = run_entries("01AA2")
check(
    "comando não emite origem",
    comando[-1].origin is None and "Retirar" in comando[-1].text,
    f"-- {comando[-1]}",
)

# O apagar tem de desfazer a origem também, senão o destaque ficaria numa
# casa que o jogador acabou de descartar.
apagado = run_entries("AA2*")
check(
    "'*' desfaz a origem",
    apagado[-1].origin is None,
    f"-- {apagado[-1]}",
)

check(
    "as linhas de digitação não contaminam os eventos",
    run_keys("AA2AA4#") == ["e2:0,e4:1"],
)

print(f"\n{'=' * 70}")
if failures:
    print(f"FALHARAM {len(failures)}: {', '.join(failures)}")
    sys.exit(1)
print("Camada de teclado OK.")
