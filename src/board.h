/*
 * board.h — Casas do tabuleiro e espelho de ocupação.
 *
 * O processo C não conhece regras de xadrez: para ele o tabuleiro é só uma
 * grade 8×8 de "tem ímã / não tem ímã", exatamente o que os reed switches
 * medem. Este módulo guarda essa grade e traduz índices em nomes de casa
 * (`a1`..`h8`), que é o vocabulário do protocolo IPC.
 *
 * IMPORTANTE: o tabuleiro físico tem apenas as peças do jogador — as do
 * oponente existem só na GUI. Por isso a posição inicial do espelho depende
 * da cor com que se joga (fileiras 1 e 2 nas brancas, 7 e 8 nas pretas).
 */

#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stddef.h>

#define BOARD_FILES   8
#define BOARD_RANKS   8
#define BOARD_SQUARES (BOARD_FILES * BOARD_RANKS)

typedef enum {
    COLOR_WHITE,
    COLOR_BLACK
} player_color;

/* Uma mudança de estado de uma casa, na forma como o protocolo IPC a
 * transmite: "e2:0" é {square = índice de e2, state = 0}. */
typedef struct {
    int square;  /* índice 0..63 */
    int state;   /* 0 = desocupada, 1 = ocupada */
} board_change;

/* Espelho da ocupação do tabuleiro mantido pelo processo C.
 *
 * Na camada reed ele é a leitura estável anterior (base da detecção de
 * diferenças). Na camada de teclado, onde não há tabuleiro físico nenhum,
 * ele é a única noção de "onde estão as peças" — o que permite recusar na
 * origem lances impossíveis (mover de uma casa vazia). */
typedef struct {
    bool occupied[BOARD_SQUARES];
} board_mirror;

/* Índice da casa a partir de coluna (0='a') e fileira (0='1'). */
int board_square(int file, int rank);

int board_square_file(int square);
int board_square_rank(int square);

/* Nome da casa ("e4") para um índice válido; "??" fora da faixa.
 * O ponteiro aponta para uma tabela estática — não deve ser liberado. */
const char *board_square_name(int square);

/* Índice da casa a partir do nome ("e4", maiúsculas aceitas).
 * Devolve -1 se o nome não for uma casa válida. */
int board_parse_square(const char *name);

void board_mirror_clear(board_mirror *mirror);

/* Monta a posição inicial das peças do jogador: fileiras 1-2 (brancas) ou
 * 7-8 (pretas). É o mesmo estado que a camada Python assume ao iniciar. */
void board_mirror_initial(board_mirror *mirror, player_color color);

/* Copia o espelho inteiro para `out` (64 posições) como lista de mudanças,
 * para reenviar o estado completo à camada Python.
 *
 * Returns:
 *     Número de mudanças escritas (sempre BOARD_SQUARES).
 */
size_t board_mirror_snapshot(const board_mirror *mirror, board_change *out);

#endif /* BOARD_H */
