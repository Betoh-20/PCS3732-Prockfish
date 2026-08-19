"""
menu.py — Menu de configuração exibido antes (e entre) as partidas.

É a versão na tela do que o Makefile faz na linha de comando: escolher o modo
de jogo, o oponente, a cor das peças, a camada de entrada (mock, reed switches
ou teclado matricial) e os parâmetros da engine e do Lichess — e, se o
processo C ainda não estiver compilado, compilá-lo sem sair da interface.

São duas apresentações do mesmo menu, descritas por `app.launcher.OPTIONS`:

  - `SetupMenu`: janela pygame, operada por teclado ou mouse;
  - `run_text_menu`: lista numerada no terminal, usada quando não há pygame
    ou display (acesso por SSH, por exemplo).

`run_setup_menu` escolhe entre as duas e devolve a configuração escolhida, ou
None se o usuário pediu para sair.
"""

import logging
import sys
from typing import Optional

try:
    import pygame
    PYGAME_AVAILABLE = True
except ImportError:
    PYGAME_AVAILABLE = False

from app.config import (
    BOARD_SIZE, STATUS_BAR_HEIGHT,
    BG_COLOR, STATUS_BG_COLOR, TEXT_COLOR, COORD_COLOR,
    INVALID_MOVE_COLOR, LIGHT_SQUARE_COLOR, DARK_SQUARE_COLOR,
)
from app.display import set_display_mode, toggle_fullscreen
from app.launcher import (
    LaunchConfig, Option, build_board_input, visible_options,
)

logger = logging.getLogger(__name__)

# Ações do menu (linhas que fazem algo em vez de guardar um valor).
ACTION_START = "start"
ACTION_BUILD = "build"
ACTION_QUIT = "quit"

SUCCESS_COLOR = (100, 200, 100)
WARNING_COLOR = (220, 180, 90)
SELECTION_COLOR = (70, 90, 70)


def _action_rows(config: LaunchConfig) -> list[tuple[str, str]]:
    """Ações oferecidas para a configuração atual, na ordem em que aparecem."""
    rows = [(ACTION_START, "INICIAR PARTIDA")]
    if config.uses_c_process:
        rows.append((ACTION_BUILD, "Compilar processo C (make board-input)"))
    rows.append((ACTION_QUIT, "Sair"))
    return rows


def _run_build_action() -> tuple[str, str]:
    """Compila o processo C e devolve (mensagem, severidade)."""
    ok, message = build_board_input()
    return message, ("success" if ok else "error")


# ---------------------------------------------------------------------------
#  Menu gráfico
# ---------------------------------------------------------------------------

class SetupMenu:
    """Menu de configuração em pygame.

    Cada linha é uma opção de `app.launcher.OPTIONS` ou uma ação. As setas
    cima/baixo escolhem a linha, esquerda/direita giram o valor, Enter edita
    campos de texto e ativa as ações. O mouse faz o mesmo: clicar numa linha a
    seleciona, clicar de novo no valor o avança (botão direito volta).
    """

    ROW_HEIGHT = 30
    PADDING = 18

    def __init__(self, config: LaunchConfig, token: str = "", notice: str = "",
                 notice_type: str = "info"):
        if not PYGAME_AVAILABLE:
            raise ImportError("pygame não está instalado.")

        self.config = config
        self._token = token
        self._notice = notice
        self._notice_type = notice_type

        self._width = BOARD_SIZE + 56
        self._height = BOARD_SIZE + 56 + STATUS_BAR_HEIGHT

        self._selected = 0
        self._scroll = 0
        self._editing: Optional[Option] = None
        self._edit_text = ""
        self._rows: list[tuple] = []
        self._row_rects: list[tuple] = []   # (rect, índice) da última pintura
        self._issues: list[tuple[str, str]] = []
        self._blocked = False
        self._option_count = 0
        self._scroll_hint = ""

        self._screen = None
        self._clock = None
        self._font_title = None
        self._font_row = None
        self._font_small = None

    # -- Ciclo de vida ------------------------------------------------------

    def run(self) -> Optional[LaunchConfig]:
        """Abre o menu e devolve a configuração escolhida (None = sair)."""
        pygame.init()
        pygame.display.set_caption("Tabuleiro de Xadrez Eletrônico — configuração")
        self._screen, self.config.fullscreen = set_display_mode(
            (self._width, self._height), self.config.fullscreen
        )
        self._clock = pygame.time.Clock()
        self._font_title = pygame.font.SysFont("arial", 24, bold=True)
        self._font_row = pygame.font.SysFont("arial", 17)
        self._font_small = pygame.font.SysFont("arial", 14)

        try:
            return self._loop()
        finally:
            pygame.quit()

    def _loop(self) -> Optional[LaunchConfig]:
        while True:
            self._rebuild_rows()
            for event in pygame.event.get():
                result = self._handle_event(event)
                if result is not None:
                    # False = sair; True = começar a partida.
                    return self.config if result else None
            self._draw()
            self._clock.tick(30)

    # -- Linhas -------------------------------------------------------------

    def _rebuild_rows(self) -> None:
        """Recalcula as linhas visíveis (elas dependem do que está escolhido).

        A linha selecionada é mantida dentro dos limites: mudar o modo de jogo
        pode fazer sumir a opção que estava sob o cursor.
        """
        rows: list[tuple] = [("option", option) for option in visible_options(self.config)]
        self._option_count = len(rows)
        rows.append(("separator", None))
        rows += [("action", action) for action in _action_rows(self.config)]
        self._rows = rows
        self._selected = max(0, min(self._selected, len(rows) - 1))
        if self._rows[self._selected][0] == "separator":
            self._selected += 1

    def _visible_row_count(self) -> int:
        """Quantas opções cabem entre o cabeçalho e o bloco de ações."""
        available = (
            self._height - self._list_top()
            - self._footer_height() - self._actions_height()
        )
        return max(3, available // self.ROW_HEIGHT)

    def _actions_height(self) -> int:
        """Altura do bloco fixo de ações (com o separador acima dele).

        As ações não rolam junto com a lista: "INICIAR PARTIDA" ficando de
        fora da tela — o que acontece com a lista cheia, no modo Lichess com
        o teclado matricial — seria esconder justamente o que se veio fazer.
        """
        return (len(self._rows) - self._option_count) * self.ROW_HEIGHT

    def _actions_top(self) -> int:
        return self._height - self._footer_height() - self._actions_height()

    def _list_top(self) -> int:
        return 96

    def _footer_height(self) -> int:
        return 118

    def _move_selection(self, delta: int) -> None:
        """Anda pelas linhas, pulando o separador."""
        count = len(self._rows)
        index = self._selected
        for _ in range(count):
            index = (index + delta) % count
            if self._rows[index][0] != "separator":
                break
        self._selected = index
        self._ensure_visible()

    def _ensure_visible(self) -> None:
        """Rola a lista de opções até a linha escolhida aparecer."""
        visible = self._visible_row_count()
        self._scroll = max(0, min(self._scroll, self._option_count - visible))

        if self._selected >= self._option_count:
            return  # ações: sempre visíveis, não rolam

        if self._selected < self._scroll:
            self._scroll = self._selected
        elif self._selected >= self._scroll + visible:
            self._scroll = self._selected - visible + 1

    # -- Eventos ------------------------------------------------------------

    def _handle_event(self, event) -> Optional[bool]:
        """Processa um evento.

        Returns:
            None para continuar no menu, True para iniciar a partida e False
            para sair da aplicação.
        """
        if event.type == pygame.QUIT:
            return False

        if self._editing is not None:
            return self._handle_edit_key(event)

        if event.type == pygame.KEYDOWN:
            return self._handle_key(event)

        if event.type == pygame.MOUSEBUTTONDOWN:
            return self._handle_click(event)

        return None

    def _handle_key(self, event) -> Optional[bool]:
        key = event.key

        if key == pygame.K_F11:
            # A escolha fica na configuração: a partida abre do mesmo jeito
            # que o menu, sem ter que apertar F11 de novo lá dentro.
            self.config.fullscreen = toggle_fullscreen(self.config.fullscreen)
            return None
        if key in (pygame.K_ESCAPE, pygame.K_q):
            return False
        if key in (pygame.K_UP, pygame.K_k):
            self._move_selection(-1)
        elif key in (pygame.K_DOWN, pygame.K_j):
            self._move_selection(1)
        elif key in (pygame.K_LEFT, pygame.K_h):
            self._cycle_selected(-1)
        elif key in (pygame.K_RIGHT, pygame.K_l):
            self._cycle_selected(1)
        elif key in (pygame.K_RETURN, pygame.K_KP_ENTER, pygame.K_SPACE):
            return self._activate()
        elif key == pygame.K_F5:
            self._notice, self._notice_type = _run_build_action()
        return None

    def _handle_edit_key(self, event) -> Optional[bool]:
        """Digitação num campo de texto (ou numérico)."""
        if event.type != pygame.KEYDOWN:
            return None

        if event.key == pygame.K_ESCAPE:
            self._editing = None
        elif event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
            option = self._editing
            if not option.apply_text(self.config, self._edit_text):
                self._notice = f"Valor inválido para {option.label}."
                self._notice_type = "error"
            self._editing = None
        elif event.key == pygame.K_BACKSPACE:
            self._edit_text = self._edit_text[:-1]
        elif event.unicode and event.unicode.isprintable():
            self._edit_text += event.unicode
        return None

    def _handle_click(self, event) -> Optional[bool]:
        if event.button == 4:                      # roda para cima
            self._scroll = max(0, self._scroll - 1)
            return None
        if event.button == 5:                      # roda para baixo
            self._scroll = min(
                max(0, self._option_count - self._visible_row_count()),
                self._scroll + 1,
            )
            return None

        for rect, index in self._row_rects:
            if not rect.collidepoint(event.pos):
                continue
            kind = self._rows[index][0]
            if kind == "separator":
                return None
            already_selected = self._selected == index
            self._selected = index
            if kind == "action":
                return self._activate()
            # Um clique escolhe a linha; o seguinte já mexe no valor, para
            # que o dedo (a tela do Pi é sensível ao toque) não mude nada
            # sem querer só de mirar noutra opção.
            if already_selected:
                if event.button == 3:
                    self._cycle_selected(-1)
                else:
                    self._cycle_selected(1)
            return None
        return None

    def _selected_option(self) -> Optional[Option]:
        kind, payload = self._rows[self._selected]
        return payload if kind == "option" else None

    def _cycle_selected(self, delta: int) -> None:
        option = self._selected_option()
        if option is None:
            return
        if option.kind == "text":
            self._begin_edit(option)
            return
        option.cycle(self.config, delta)

    def _begin_edit(self, option: Option) -> None:
        """Abre a digitação de uma opção, já com o valor atual no campo."""
        self._editing = option
        self._edit_text = str(getattr(self.config, option.field))

    def _activate(self) -> Optional[bool]:
        """Enter/clique sobre a linha selecionada."""
        kind, payload = self._rows[self._selected]

        if kind == "option":
            option: Option = payload
            if option.editable_text():
                self._begin_edit(option)
            else:
                option.cycle(self.config, 1)
            return None

        action = payload[0]
        if action == ACTION_QUIT:
            return False
        if action == ACTION_BUILD:
            self._notice, self._notice_type = _run_build_action()
            return None

        blocking = self.config.blocking_issue(self._token)
        if blocking:
            self._notice, self._notice_type = blocking, "error"
            return None
        return True

    # -- Desenho ------------------------------------------------------------

    def _draw(self) -> None:
        # A validação olha o disco (binários do Stockfish e do processo C):
        # uma vez por quadro, e não uma por linha desenhada.
        self._issues = self.config.issues(self._token)
        self._blocked = any(severity == "error" for severity, _ in self._issues)

        self._screen.fill(BG_COLOR)
        self._draw_header()
        self._draw_rows()
        self._draw_footer()
        pygame.display.flip()

    def _draw_header(self) -> None:
        title = self._font_title.render(
            "Tabuleiro de Xadrez Eletrônico", True, TEXT_COLOR
        )
        self._screen.blit(title, (self.PADDING, 20))

        summary = self._font_small.render(
            self.config.summary(), True, LIGHT_SQUARE_COLOR
        )
        self._screen.blit(summary, (self.PADDING, 56))

        pygame.draw.line(
            self._screen, COORD_COLOR,
            (0, self._list_top() - 10), (self._width, self._list_top() - 10), 1,
        )

    def _draw_rows(self) -> None:
        self._row_rects = []
        visible = self._visible_row_count()
        self._ensure_visible()
        top = self._list_top()

        for offset in range(visible):
            index = self._scroll + offset
            if index >= self._option_count:
                break
            y = top + offset * self.ROW_HEIGHT
            rect = pygame.Rect(0, y, self._width, self.ROW_HEIGHT)
            self._row_rects.append((rect, index))
            self._draw_row(self._rows[index], rect, index == self._selected)

        # Aviso de que a lista continua fora da área visível, escrito na
        # linha do separador (é o único espaço livre com a lista cheia).
        self._scroll_hint = " ".join(filter(None, (
            "^ mais acima" if self._scroll else "",
            "v mais abaixo" if self._scroll + visible < self._option_count else "",
        )))

        # Separador e ações, sempre no mesmo lugar.
        for offset, index in enumerate(range(self._option_count, len(self._rows))):
            y = self._actions_top() + offset * self.ROW_HEIGHT
            rect = pygame.Rect(0, y, self._width, self.ROW_HEIGHT)
            self._row_rects.append((rect, index))
            self._draw_row(self._rows[index], rect, index == self._selected)

    def _draw_row(self, row: tuple, rect, selected: bool) -> None:
        kind, payload = row

        if kind == "separator":
            right = self._width - self.PADDING
            if self._scroll_hint:
                hint = self._font_small.render(
                    self._scroll_hint, True, COORD_COLOR
                )
                right -= hint.get_width() + 10
                self._screen.blit(hint, (right + 10, rect.centery - 8))
            pygame.draw.line(
                self._screen, COORD_COLOR,
                (self.PADDING, rect.centery), (right, rect.centery), 1,
            )
            return

        if selected:
            pygame.draw.rect(self._screen, SELECTION_COLOR, rect)

        if kind == "action":
            action, label = payload
            color = SUCCESS_COLOR if action == ACTION_START else TEXT_COLOR
            if action == ACTION_START and self._blocked:
                # Apagado enquanto houver impedimento: o motivo está no rodapé.
                color = COORD_COLOR
            text = self._font_row.render(label, True, color)
            self._screen.blit(text, (self.PADDING + 8, rect.y + 6))
            return

        option: Option = payload
        label = self._font_row.render(option.label, True, TEXT_COLOR)
        self._screen.blit(label, (self.PADDING + 8, rect.y + 6))

        if self._editing is option:
            value_text = self._edit_text + "_"
            color = SUCCESS_COLOR
        else:
            value_text = option.display(self.config)
            color = LIGHT_SQUARE_COLOR if selected else DARK_SQUARE_COLOR

        # As setas só aparecem na linha escolhida: elas são a dica de que dá
        # para girar o valor ali.
        if selected and self._editing is None and option.kind != "text":
            value_text = f"<  {value_text}  >"

        value = self._font_row.render(value_text, True, color)
        self._screen.blit(
            value, (self._width - value.get_width() - self.PADDING - 8, rect.y + 6)
        )

    def _draw_footer(self) -> None:
        top = self._height - self._footer_height()
        pygame.draw.rect(
            self._screen, STATUS_BG_COLOR,
            pygame.Rect(0, top, self._width, self._footer_height()),
        )
        pygame.draw.line(
            self._screen, COORD_COLOR, (0, top), (self._width, top), 1
        )

        y = top + 8
        lines: list[tuple[str, tuple[int, int, int]]] = []

        if self._editing is not None:
            lines.append((
                f"Digitando {self._editing.label} — Enter confirma, Esc cancela.",
                SUCCESS_COLOR,
            ))
        elif self._notice:
            lines.append((self._notice, self._message_color(self._notice_type)))

        option = self._selected_option()
        if option is not None and option.hint:
            lines.append((option.hint, COORD_COLOR))

        for severity, message in self._issues[:2]:
            color = INVALID_MOVE_COLOR if severity == "error" else WARNING_COLOR
            lines.append((message, color))

        lines.append((f"Equivalente no terminal: {self.config.equivalent_command()}",
                      COORD_COLOR))
        lines.append((
            "Setas: escolher/alterar · Enter: editar ou iniciar · "
            "F5: compilar · F11: tela cheia · Esc: sair",
            COORD_COLOR,
        ))

        for text, color in lines[:5]:
            surface = self._fit(text, color, self._width - 2 * self.PADDING)
            self._screen.blit(surface, (self.PADDING, y))
            y += 20

    @staticmethod
    def _message_color(message_type: str) -> tuple[int, int, int]:
        if message_type == "error":
            return INVALID_MOVE_COLOR
        if message_type == "success":
            return SUCCESS_COLOR
        return TEXT_COLOR

    def _fit(self, text: str, color: tuple[int, int, int], max_width: int):
        """Renderiza cortando com reticências ASCII o que não couber."""
        font = self._font_small
        if font.size(text)[0] <= max_width:
            return font.render(text, True, color)
        while text and font.size(text + "...")[0] > max_width:
            text = text[:-1]
        return font.render(f"{text}...", True, color)


# ---------------------------------------------------------------------------
#  Menu de terminal
# ---------------------------------------------------------------------------

def run_text_menu(
    config: LaunchConfig,
    token: str = "",
    notice: str = "",
) -> Optional[LaunchConfig]:
    """Mesma configuração, em lista numerada — para quando não há janela.

    Returns:
        A configuração escolhida, ou None se o usuário pediu para sair.
    """
    if notice:
        print(f"\n{notice}")

    while True:
        options = visible_options(config)
        print("\n=== Tabuleiro de Xadrez Eletrônico — configuração ===")
        print(f"  {config.summary()}\n")

        for number, option in enumerate(options, start=1):
            print(f"  {number:2d}. {option.label:<28} {option.display(config)}")

        actions = _action_rows(config)
        letters = {"s": ACTION_START, "c": ACTION_BUILD, "q": ACTION_QUIT}
        available = {
            letter: action for letter, action in letters.items()
            if action in {a for a, _ in actions}
        }
        print()
        for letter, action in available.items():
            label = next(label for a, label in actions if a == action)
            print(f"   {letter}. {label}")

        for severity, message in config.issues(token):
            print(f"  [{severity.upper()}] {message}")
        print(f"\n  Equivalente no terminal: {config.equivalent_command()}")

        try:
            choice = input("\nNúmero para alterar, ou s/c/q: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return None

        action = available.get(choice)
        if action == ACTION_QUIT:
            return None
        if action == ACTION_BUILD:
            print(_run_build_action()[0])
            continue
        if action == ACTION_START:
            blocking = config.blocking_issue(token)
            if blocking:
                print(f"  Não dá para começar: {blocking}")
                continue
            return config

        if not choice.isdigit() or not 1 <= int(choice) <= len(options):
            print("  Opção desconhecida.")
            continue

        _edit_text_option(config, options[int(choice) - 1])


def _edit_text_option(config: LaunchConfig, option: Option) -> None:
    """Altera uma opção no menu de terminal.

    Escolhas e booleanos giram sozinhos (é o mesmo gesto da seta na janela);
    o resto é digitado.
    """
    if option.kind in ("choice", "bool"):
        option.cycle(config, 1)
        print(f"  {option.label}: {option.display(config)}")
        return

    if option.hint:
        print(f"  {option.hint}")
    try:
        value = input(f"  {option.label} [{option.display(config)}]: ")
    except (EOFError, KeyboardInterrupt):
        print()
        return
    if value.strip() and not option.apply_text(config, value):
        print("  Valor inválido — nada mudou.")


# ---------------------------------------------------------------------------
#  Escolha da apresentação
# ---------------------------------------------------------------------------

def run_setup_menu(
    config: LaunchConfig,
    token: str = "",
    notice: str = "",
    notice_type: str = "info",
    prefer_gui: bool = True,
) -> Optional[LaunchConfig]:
    """Exibe o menu na janela quando dá, e no terminal quando não dá."""
    if prefer_gui and PYGAME_AVAILABLE:
        try:
            return SetupMenu(config, token, notice, notice_type).run()
        except pygame.error as exc:
            # Sem display (SSH sem X, por exemplo): o menu de terminal serve.
            logger.warning("Menu gráfico indisponível (%s) — usando o terminal.", exc)
            pygame.quit()

    if not sys.stdin or not sys.stdin.isatty():
        logger.error(
            "Sem interface gráfica e sem terminal interativo para o menu: "
            "passe as opções pela linha de comando (--help)."
        )
        return None

    return run_text_menu(config, token, notice)
