/*
 * board.c — Implementação das casas e do espelho de ocupação (ver board.h).
 */

#include "board.h"

#include <ctype.h>
#include <string.h>

/* Nomes pré-calculados: a conversão acontece a cada evento emitido, e uma
 * tabela evita montar a string toda vez (e evita devolver ponteiro para
 * buffer local, que o chamador guardaria por engano). */
static char g_names[BOARD_SQUARES][3];
static bool g_names_ready = false;

static void ensure_names(void)
{
    if (g_names_ready) {
        return;
    }
    for (int rank = 0; rank < BOARD_RANKS; rank++) {
        for (int file = 0; file < BOARD_FILES; file++) {
            char *name = g_names[rank * BOARD_FILES + file];
            name[0] = (char)('a' + file);
            name[1] = (char)('1' + rank);
            name[2] = '\0';
        }
    }
    g_names_ready = true;
}

int board_square(int file, int rank)
{
    if (file < 0 || file >= BOARD_FILES || rank < 0 || rank >= BOARD_RANKS) {
        return -1;
    }
    return rank * BOARD_FILES + file;
}

int board_square_file(int square)
{
    return (square < 0 || square >= BOARD_SQUARES) ? -1 : square % BOARD_FILES;
}

int board_square_rank(int square)
{
    return (square < 0 || square >= BOARD_SQUARES) ? -1 : square / BOARD_FILES;
}

const char *board_square_name(int square)
{
    if (square < 0 || square >= BOARD_SQUARES) {
        return "??";
    }
    ensure_names();
    return g_names[square];
}

int board_parse_square(const char *name)
{
    if (name == NULL || strlen(name) < 2) {
        return -1;
    }
    int file = tolower((unsigned char)name[0]) - 'a';
    int rank = name[1] - '1';
    return board_square(file, rank);
}

void board_mirror_clear(board_mirror *mirror)
{
    memset(mirror->occupied, 0, sizeof(mirror->occupied));
}

void board_mirror_initial(board_mirror *mirror, player_color color)
{
    board_mirror_clear(mirror);

    /* Só as peças do jogador ficam no tabuleiro físico. */
    int first = (color == COLOR_WHITE) ? 0 : 6;  /* fileiras 1-2 ou 7-8 */
    for (int rank = first; rank < first + 2; rank++) {
        for (int file = 0; file < BOARD_FILES; file++) {
            mirror->occupied[board_square(file, rank)] = true;
        }
    }
}

size_t board_mirror_snapshot(const board_mirror *mirror, board_change *out)
{
    for (int square = 0; square < BOARD_SQUARES; square++) {
        out[square].square = square;
        out[square].state = mirror->occupied[square] ? 1 : 0;
    }
    return (size_t)BOARD_SQUARES;
}
