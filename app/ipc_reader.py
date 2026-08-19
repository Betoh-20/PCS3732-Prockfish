"""
ipc_reader.py — Módulo IPC (Named Pipe / stdin / subprocess).

Responsável por receber dados do processo C (ou mock),
desserializar os eventos de mudança do tabuleiro e entregá-los
ao motor de estado do jogo.

Protocolo de eventos:
    Cada linha contém pares "casa:estado" separados por vírgula.
    Exemplo: "e2:0,e4:1\n"
    - casa: notação algébrica (a1–h8)
    - estado: 0 (desocupada) ou 1 (ocupada)

    A camada de teclado do processo C envia também linhas de status, que
    começam com '@' e não descrevem sensor nenhum:
        "@entry|e2||Lance: E2_|Digitando...\n"
    É o lance em formação, antes do '#' — ver `KeypadEntry`.

Canal de volta (aplicação → processo C):
    Pelo stdin do subprocesso, quando `pipe_stdin` está ligado. Uma linha,
    um comando; hoje só existe o espelho do tabuleiro:
        "@sync|<64 caracteres '0'/'1'>\n"
    É o que dispensa o jogador de digitar `0 1 <casa> #` quando o oponente
    captura uma peça dele. Ver `src/ipc.h` e `app/main.py`.

Modos suportados:
    - 'subprocess': Inicia o processo C/mock como subprocesso e lê stdout
    - 'stdin': Lê da entrada padrão (útil para piping)
    - 'pipe': Lê de um Named Pipe / FIFO (somente Linux)
"""

import os
import sys
import logging
import subprocess
import threading
from dataclasses import dataclass
from queue import Queue, Empty
from typing import Optional

from app.config import (
    IPC_MODE, PIPE_PATH, C_PROCESS_PATH,
    EVENT_SEPARATOR, FIELD_SEPARATOR, FILES, RANKS,
)

logger = logging.getLogger(__name__)

# Marcador das linhas de status do processo C (nunca começam uma casa).
ENTRY_PREFIX = "@entry|"


def _square_or_none(name: str) -> Optional[str]:
    """Valida um nome de casa vindo do processo C."""
    name = name.strip().lower()
    if len(name) == 2 and name[0] in FILES and name[1] in RANKS:
        return name
    return None


@dataclass(frozen=True)
class KeypadEntry:
    """O lance que está sendo digitado no teclado matricial.

    Chega a cada tecla, antes do '#' que envia o lance. Serve para mostrar
    na tela o que já foi digitado e, com a origem resolvida, destacar os
    destinos legais daquela peça.

    Attributes:
        origin: Casa de origem, quando as duas primeiras teclas já formaram
            uma casa válida (None nos comandos e antes disso).
        target: Casa de destino, quando as quatro teclas já foram digitadas.
        text: O que aparece no LCD ("Lance: E2_"); vazio quando não há nada
            sendo digitado.
        status: Mensagem curta do processo C ("Digitando...", "Origem vazia").
    """

    origin: Optional[str] = None
    target: Optional[str] = None
    text: str = ""
    status: str = ""

    @property
    def active(self) -> bool:
        """Se há digitação em andamento para mostrar."""
        return bool(self.text)

    def display(self) -> str:
        """Linha a exibir na barra de status da aplicação."""
        if self.text and self.status:
            return f"{self.text} · {self.status}"
        return self.text or self.status


def parse_entry(line: str) -> Optional[KeypadEntry]:
    """Desserializa uma linha de digitação do teclado matricial.

    Args:
        line: Linha no formato "@entry|origem|destino|texto|status".

    Returns:
        A digitação corrente, ou None se a linha não for desse tipo.
    """
    line = line.strip()
    if not line.startswith(ENTRY_PREFIX):
        return None

    # Campos a mais (uma versão futura do processo C) são ignorados; a menos
    # entram vazios, para que a aplicação não quebre com um binário antigo.
    fields = line[len(ENTRY_PREFIX):].split("|")
    fields += [""] * (4 - len(fields))
    origin, target, text, status = fields[:4]

    return KeypadEntry(
        origin=_square_or_none(origin),
        target=_square_or_none(target),
        text=text.strip(),
        status=status.strip(),
    )


def parse_event(line: str) -> Optional[dict[str, int]]:
    """Desserializa uma linha de evento IPC.

    Args:
        line: Linha de texto no formato "a1:0,e4:1"

    Returns:
        Dicionário {casa: estado} ou None se a linha for inválida.
        Exemplo: {"e2": 0, "e4": 1}
    """
    line = line.strip()
    if not line:
        return None

    changes: dict[str, int] = {}
    try:
        pairs = line.split(EVENT_SEPARATOR)
        for pair in pairs:
            pair = pair.strip()
            if not pair:
                continue
            square, state_str = pair.split(FIELD_SEPARATOR)
            square = square.strip().lower()

            # Validação do nome da casa
            if len(square) != 2 or square[0] not in FILES or square[1] not in RANKS:
                logger.warning("Casa inválida no evento IPC: '%s'", square)
                return None

            state = int(state_str.strip())
            if state not in (0, 1):
                logger.warning("Estado inválido no evento IPC: '%s'", state_str)
                return None

            changes[square] = state

    except (ValueError, IndexError) as exc:
        logger.warning("Erro ao parsear evento IPC '%s': %s", line, exc)
        return None

    return changes if changes else None


class IPCReader:
    """Leitor de eventos IPC do processo C / mock.

    Lê eventos de uma fonte (subprocess, stdin ou named pipe)
    em uma thread separada e os coloca em uma fila thread-safe.
    """

    def __init__(
        self,
        mode: str = IPC_MODE,
        pipe_path: str = PIPE_PATH,
        process_path: str = C_PROCESS_PATH,
        process_args: Optional[list[str]] = None,
        pipe_stdin: bool = False,
    ):
        self._mode = mode
        self._pipe_path = pipe_path
        self._process_path = process_path
        self._process_args = list(process_args or [])
        self._pipe_stdin = pipe_stdin
        self._queue: Queue[dict[str, int]] = Queue()
        # Fila separada para a digitação do teclado: ela é informação de
        # tela, e misturá-la com os eventos de sensor obrigaria todo mundo
        # que lê um evento a saber distinguir os dois.
        self._entries: Queue[KeypadEntry] = Queue()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._process: Optional[subprocess.Popen] = None
        self._source = None  # file-like object para leitura

    def set_pipe_stdin(self, pipe_stdin: bool) -> None:
        """Define se o stdin do subprocesso é nosso. Só vale antes de `start()`.

        Desligado por padrão porque quase todo mundo do outro lado quer o
        terminal: o mock lê comandos do usuário e o processo C em
        `--keys stdin` lê as teclas dali. Ligar isto rouba esse terminal, e em
        troca `send_to_process` deixa de ser um pedido no vácuo.
        """
        self._pipe_stdin = pipe_stdin

    def set_process_args(self, args: list[str]) -> None:
        """Define os argumentos do subprocesso. Só vale antes de `start()`.

        Usado para informar ao mock a cor das peças físicas, que no modo
        Lichess só é conhecida depois que a partida começa.
        """
        self._process_args = list(args)

    def start(self) -> None:
        """Inicia a leitura de eventos em background."""
        if self._running:
            return

        self._running = True

        if self._mode == "subprocess":
            self._start_subprocess()
        elif self._mode == "stdin":
            self._source = sys.stdin
        elif self._mode == "pipe":
            self._start_pipe()
        else:
            raise ValueError(f"Modo IPC desconhecido: {self._mode}")

        self._thread = threading.Thread(
            target=self._read_loop,
            name="IPCReader",
            daemon=True,
        )
        self._thread.start()
        logger.info("IPCReader iniciado no modo '%s'", self._mode)

    def _start_subprocess(self) -> None:
        """Inicia o processo C/mock como subprocesso.

        O mock é um script Python e precisa do interpretador; o processo C é
        um binário nativo e é executado direto. A extensão do caminho é o que
        distingue os dois — sem isso, apontar CHESS_C_PROCESS para o binário
        compilado faria o Python tentar interpretá-lo como fonte.

        No Windows, abre uma janela de console separada para que o
        usuário possa digitar jogadas interativamente. No Linux, o
        stdin/stderr são herdados do processo pai (terminal) — a menos que
        `pipe_stdin` esteja ligado, e aí o stdin vira um cano nosso, por onde
        a aplicação manda comandos (ver `send_to_process`).
        """
        if self._process_path.endswith(".py"):
            cmd = [sys.executable, self._process_path, *self._process_args]
        else:
            cmd = [self._process_path, *self._process_args]
        logger.info("Iniciando subprocesso: %s", " ".join(cmd))

        popen_kwargs = dict(
            stdout=subprocess.PIPE,  # IPC events vêm por aqui
            text=True,
            bufsize=1,  # line-buffered
        )

        if sys.platform == "win32":
            # Abre console próprio: o mock mostra prompts no console
            # e o usuário digita lá. stdout continua piped para IPC.
            popen_kwargs["stdin"] = None       # console próprio
            popen_kwargs["stderr"] = None      # console próprio
            popen_kwargs["creationflags"] = subprocess.CREATE_NEW_CONSOLE
        else:
            # No Linux, herda stdin/stderr do terminal pai
            popen_kwargs["stdin"] = (
                subprocess.PIPE if self._pipe_stdin else None
            )
            popen_kwargs["stderr"] = None

        self._process = subprocess.Popen(cmd, **popen_kwargs)
        self._source = self._process.stdout

    def _start_pipe(self) -> None:
        """Abre o Named Pipe (FIFO) para leitura. Somente Linux."""
        if sys.platform == "win32":
            raise OSError(
                "Named Pipes FIFO não são suportados no Windows. "
                "Use o modo 'subprocess' ou 'stdin'."
            )

        # Cria o FIFO se não existir
        if not os.path.exists(self._pipe_path):
            os.mkfifo(self._pipe_path)
            logger.info("FIFO criado em: %s", self._pipe_path)

        logger.info("Aguardando conexão no Named Pipe: %s", self._pipe_path)
        # open() bloqueia até que o outro lado abra para escrita
        self._source = open(self._pipe_path, "r")

    def _read_loop(self) -> None:
        """Loop de leitura em thread separada."""
        try:
            while self._running and self._source:
                line = self._source.readline()
                if not line:
                    # EOF — o processo C encerrou ou pipe foi fechado
                    logger.info("Fonte IPC encerrou (EOF).")
                    break

                entry = parse_entry(line)
                if entry is not None:
                    self._entries.put(entry)
                    logger.debug("Digitação recebida: %s", entry)
                    continue

                event = parse_event(line)
                if event is not None:
                    self._queue.put(event)
                    logger.debug("Evento recebido: %s", event)

        except Exception as exc:
            if self._running:
                logger.error("Erro na leitura IPC: %s", exc)
        finally:
            self._running = False

    def read_event(self, timeout: float = 0.05) -> Optional[dict[str, int]]:
        """Lê o próximo evento da fila.

        Args:
            timeout: Tempo máximo de espera em segundos (padrão 50ms).

        Returns:
            Dicionário {casa: estado} ou None se não houver evento.
        """
        try:
            return self._queue.get(timeout=timeout)
        except Empty:
            return None

    def read_entry(self) -> Optional[KeypadEntry]:
        """Lê a próxima digitação do teclado matricial, sem esperar.

        Returns:
            A digitação corrente, ou None se nada novo chegou. Só a camada
            de teclado do processo C envia isto.
        """
        try:
            return self._entries.get_nowait()
        except Empty:
            return None

    def has_events(self) -> bool:
        """Verifica se há eventos pendentes na fila."""
        return not self._queue.empty()

    @property
    def mode(self) -> str:
        """Modo IPC em uso: 'subprocess', 'stdin' ou 'pipe'."""
        return self._mode

    @property
    def is_running(self) -> bool:
        """Indica se o leitor está ativo."""
        return self._running

    def send_to_process(self, message: str) -> None:
        """Envia uma mensagem para o subprocesso (via stdin).

        É por aqui que a camada de teclado do processo C recebe o espelho do
        tabuleiro (`@sync|...`). Sem `pipe_stdin`, o stdin é do terminal e não
        nosso: a mensagem é descartada em silêncio, que é o certo — quem não
        pediu o canal não depende dele.
        """
        if self._process and self._process.stdin:
            try:
                self._process.stdin.write(message + "\n")
                self._process.stdin.flush()
            except (OSError, BrokenPipeError) as exc:
                logger.error("Erro ao enviar para subprocesso: %s", exc)

    def stop(self) -> None:
        """Para a leitura e libera recursos."""
        self._running = False

        # Encerra o subprocesso se existir. O stdin fecha primeiro: o EOF é o
        # aviso de que não vem mais comando, e o processo C sai do laço por
        # conta própria antes do terminate.
        if self._process:
            if self._process.stdin:
                try:
                    self._process.stdin.close()
                except OSError:
                    pass
            try:
                self._process.terminate()
                self._process.wait(timeout=3)
            except Exception:
                self._process.kill()
            self._process = None

        # Fecha o source (exceto stdin)
        if self._source and self._source is not sys.stdin:
            try:
                self._source.close()
            except Exception:
                pass
            self._source = None

        # Aguarda a thread finalizar
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2)
        self._thread = None

        logger.info("IPCReader encerrado.")

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
        return False
