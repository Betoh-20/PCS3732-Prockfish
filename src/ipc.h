/*
 * ipc.h — Serialização e envio dos eventos para a camada Python.
 *
 * Protocolo (o mesmo de `app/ipc_reader.py` e do mock em Python):
 *
 *     casa:estado[,casa:estado...]\n
 *     Exemplo: "e2:0,e4:1\n"
 *
 *     casa   — notação algébrica a1..h8
 *     estado — 0 (desocupada) ou 1 (ocupada)
 *
 * Há um segundo tipo de linha, que não é evento de sensor e por isso começa
 * com '@' (caractere que nenhuma casa pode ter):
 *
 *     @entry|origem|destino|texto|status\n
 *     Exemplo: "@entry|e2||Lance: E2_|Digitando..."
 *
 * É o que a camada de teclado usa para contar o que está sendo digitado
 * antes do '#', para que a aplicação mostre o lance em formação e destaque
 * os destinos da peça escolhida. Quem só entende `casa:estado` ignora essas
 * linhas sem prejuízo: nenhum evento de tabuleiro vai por elas.
 *
 * É este módulo que faz as duas camadas de entrada — reed switches e teclado
 * matricial — serem intercambiáveis: as duas falam exatamente a mesma língua
 * com o Python, que por isso não precisa saber qual delas está do outro lado.
 *
 * O destino é stdout (modo IPC 'subprocess'/'stdin') ou um Named Pipe
 * (modo 'pipe'). Mensagens para humanos vão sempre para stderr, para não
 * contaminar o canal de eventos.
 *
 * ---------------------------------------------------------------------------
 *  Sentido inverso (Python → C)
 * ---------------------------------------------------------------------------
 *
 * Há um canal de volta, por stdin, que NÃO passa por este módulo: só a camada
 * de teclado o escuta (ver keypad_layer.c), e nada do que chega por ele vira
 * evento de saída. Uma linha, um comando:
 *
 *     @sync|<64 caracteres '0'/'1'>\n
 *
 * É o espelho de ocupação da camada Python, na ordem a1..h8, que a camada de
 * teclado adota como sendo o dela. Existe porque o teclado não tem como saber
 * que o oponente capturou uma peça do jogador — nenhuma tecla foi digitada, e
 * mesmo assim uma casa esvaziou.
 */

#ifndef IPC_H
#define IPC_H

#include <stdbool.h>
#include <stddef.h>

#include "board.h"

/* Abre o canal de eventos.
 *
 * Args:
 *     path: caminho de um FIFO, ou NULL/"-" para usar stdout. O FIFO é
 *           criado se não existir; a abertura bloqueia até a camada Python
 *           abrir a outra ponta, que é o comportamento esperado do modo
 *           'pipe'.
 *
 * Returns:
 *     true se o canal está pronto para escrita.
 */
bool ipc_open(const char *path);

/* Emite uma linha com todas as mudanças passadas. Nada é escrito se
 * `count` for zero — linha vazia não é evento. */
void ipc_emit(const board_change *changes, size_t count);

/* Atalho para o caso de uma casa só. */
void ipc_emit_single(int square, int state);

/* Emite uma linha de status já formatada (as que começam com '@').
 *
 * Args:
 *     line: conteúdo da linha, sem o '\n' final. */
void ipc_emit_line(const char *line);

void ipc_close(void);

#endif /* IPC_H */
