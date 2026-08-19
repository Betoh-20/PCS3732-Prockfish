"""
display.py — Criação da janela, compartilhada pelo menu e pelo tabuleiro.

As duas telas da aplicação (`app.menu.SetupMenu` e `app.gui.ChessGUI`) têm
layout de tamanho fixo, com as posições calculadas a partir de `BOARD_SIZE`.
Para ocupar o monitor inteiro sem refazer esse layout, a janela é criada com
`pygame.SCALED`: o desenho continua acontecendo na resolução lógica de sempre
e o pygame o amplia para a tela, preservando a proporção (sobram barras nas
laterais em monitores mais largos).

`SCALED` também converte `event.pos` de volta para a resolução lógica, então
todos os testes de clique das duas telas seguem valendo sem conversão.
"""

import logging
from typing import Optional

try:
    import pygame
    PYGAME_AVAILABLE = True
except ImportError:
    PYGAME_AVAILABLE = False

logger = logging.getLogger(__name__)


def display_flags(fullscreen: bool) -> int:
    """Flags de `pygame.display.set_mode` para o layout de tamanho fixo."""
    flags = pygame.SCALED
    if fullscreen:
        flags |= pygame.FULLSCREEN
    return flags


def set_display_mode(
    size: tuple[int, int], fullscreen: bool
) -> tuple["pygame.Surface", bool]:
    """Cria a janela no tamanho lógico pedido.

    Args:
        size: Resolução lógica do layout (não a da tela).
        fullscreen: Se a janela deve abrir ocupando o monitor.

    Returns:
        Tupla (superfície, tela_cheia). O segundo elemento pode vir False
        mesmo com `fullscreen=True`: se o driver de vídeo recusar a tela
        cheia, abrir em janela é melhor do que não abrir nada — e quem chamou
        precisa saber disso para não mostrar um estado que não é o real.
    """
    try:
        return pygame.display.set_mode(size, display_flags(fullscreen)), fullscreen
    except pygame.error as exc:
        if not fullscreen:
            raise
        logger.warning("Tela cheia indisponível (%s) — abrindo em janela.", exc)
        return pygame.display.set_mode(size, display_flags(False)), False


def toggle_fullscreen(current: bool) -> bool:
    """Alterna entre tela cheia e janela (o F11 das duas telas).

    Args:
        current: Se a janela está em tela cheia agora.

    Returns:
        O estado depois da troca — lido da própria superfície, e não deduzido,
        porque a troca pode falhar.
    """
    try:
        pygame.display.toggle_fullscreen()
    except pygame.error as exc:
        logger.warning("Não foi possível alternar a tela cheia: %s", exc)
        return current

    surface: Optional["pygame.Surface"] = pygame.display.get_surface()
    if surface is None:
        return current
    return bool(surface.get_flags() & pygame.FULLSCREEN)
