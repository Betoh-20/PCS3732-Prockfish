/*
 * reed_layer.h — Camada de entrada 1: matriz 8×8 de reed switches.
 *
 * É o caminho principal do projeto: as peças do jogador têm ímã e fecham o
 * reed switch da casa onde estão. A varredura energiza uma linha por vez e
 * lê as oito colunas, reconstruindo a grade 8×8 com 16 pinos.
 *
 * Reúne os módulos descritos na arquitetura: varredura → debouncing (N
 * leituras consecutivas idênticas) → detecção de diferenças → IPC.
 */

#ifndef REED_LAYER_H
#define REED_LAYER_H

#include <stdbool.h>

#include "board.h"

typedef struct {
    /* Pinos BCM. O padrão é a fiação já usada em `poc_xadrez/matrix.cpp`. */
    int line_pins[BOARD_RANKS];
    int column_pins[BOARD_FILES];

    /* Coluna em nível baixo = ímã presente. É a convenção da matriz atual
     * (colunas em pull-up, linha varrida puxada para o terra). */
    bool active_low;

    /* Leituras idênticas consecutivas exigidas antes de aceitar a grade
     * como estável. Com poll_ms=10 e 5 ciclos, ~50 ms de latência. */
    int debounce_cycles;

    int poll_ms;

    /* Gira o mapeamento 180°: use quando a bancada estiver montada com a
     * casa a1 no canto oposto ao esperado. */
    bool flip;

    /* Emite a primeira leitura estável inteira (64 casas). Sem isso, uma
     * peça faltando na montagem inicial só apareceria quando fosse mexida:
     * o espelho do Python começa com o tabuleiro cheio. */
    bool emit_initial;
} reed_config;

void reed_config_default(reed_config *config);

/* Laço de varredura. Só retorna quando `app_running()` fica falso.
 *
 * Returns:
 *     0 em encerramento normal, 1 se o GPIO não pôde ser inicializado.
 */
int reed_layer_run(const reed_config *config);

#endif /* REED_LAYER_H */
