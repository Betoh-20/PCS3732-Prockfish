/*
 * keypad_layer.h — Camada de entrada 2: teclado matricial 4×4.
 *
 * Plano B para quando a matriz de reed switches não estiver pronta: em vez
 * de ler as peças no tabuleiro, o jogador DIGITA o lance no teclado, no
 * formato E3E4 (coluna, fileira, coluna, fileira).
 *
 * Do lado do Python nada muda: esta camada emite exatamente os mesmos
 * eventos "casa:estado" que a matriz emitiria (`e3:0,e4:1`), então validação
 * de lances, roque, capturas e GUI continuam funcionando como estão.
 *
 * ---------------------------------------------------------------------------
 *  Teclado (membrana 4×4 padrão)
 * ---------------------------------------------------------------------------
 *
 *      1  2  3  A
 *      4  5  6  B
 *      7  8  9  C
 *      *  0  #  D
 *
 *  Colunas do tabuleiro: o teclado só tem A-D, e o tabuleiro precisa de a-h.
 *  A tecla repetida na mesma posição vale pela letra seguinte do bloco:
 *
 *      A → a      AA → e
 *      B → b      BB → f
 *      C → c      CC → g
 *      D → d      DD → h
 *
 *  Não há ambiguidade nem tempo de espera envolvido: o lance alterna coluna
 *  e fileira, então duas letras seguidas só podem ser a mesma casa sendo
 *  redigitada. Um terceiro toque volta para a letra simples (A → a → e → a),
 *  o que permite corrigir sem apagar.
 *
 *  Fileiras: teclas 1 a 8.
 *
 *  Controle:
 *      #  confirma (envia o lance)
 *      *  apaga a última tecla; com a entrada vazia, sai do modo de comando
 *
 *  Comandos (prefixo 0, disponível só com a entrada vazia):
 *      0 1 <casa> #   retira a peça da casa      → emite "e5:0"
 *      0 2 <casa> #   coloca uma peça na casa    → emite "e5:1"
 *      0 9 #          reenvia o estado completo das 64 casas
 *      0 0 #          volta o espelho à posição inicial e o reenvia
 *
 *  O comando de retirada é o que fecha o ciclo com a camada Python: quando o
 *  oponente captura uma peça do jogador, a aplicação pede "remova a peça de
 *  e5" — com a matriz de reed isso é tirar a peça da mesa, aqui é `0 1 E5 #`.
 */

#ifndef KEYPAD_LAYER_H
#define KEYPAD_LAYER_H

#include <stdbool.h>

#include "board.h"

#define KEYPAD_ROWS    4
#define KEYPAD_COLUMNS 4

typedef struct {
    /* Pinos BCM. O padrão é a fiação do experimento 6 da bancada. */
    int row_pins[KEYPAD_ROWS];
    int column_pins[KEYPAD_COLUMNS];

    /* Falso (padrão): linha energizada em nível alto, colunas em pull-down —
     * a ligação usada na bancada. Verdadeiro inverte a polaridade, para
     * teclados ligados com pull-up. */
    bool active_low;

    int debounce_cycles;
    int poll_ms;

    /* Envia o lance assim que a quarta tecla é digitada, sem esperar o '#'.
     * Mais rápido para demonstrar, mais fácil de errar sem chance de apagar. */
    bool auto_enter;

    /* Lê as teclas de stdin em vez do GPIO (aceita 0-9, A-D, '*' e '#').
     * Permite exercitar o parser e o IPC sem hardware nenhum. */
    bool keys_from_stdin;

    /* Cor das peças do jogador: define a posição inicial do espelho. */
    player_color color;
} keypad_config;

void keypad_config_default(keypad_config *config);

/* Laço de leitura do teclado. Só retorna quando `app_running()` fica falso
 * (ou em EOF, no modo stdin).
 *
 * Returns:
 *     0 em encerramento normal, 1 se o GPIO não pôde ser inicializado.
 */
int keypad_layer_run(const keypad_config *config);

#endif /* KEYPAD_LAYER_H */
