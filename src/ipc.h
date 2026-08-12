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
 * É este módulo que faz as duas camadas de entrada — reed switches e teclado
 * matricial — serem intercambiáveis: as duas falam exatamente a mesma língua
 * com o Python, que por isso não precisa saber qual delas está do outro lado.
 *
 * O destino é stdout (modo IPC 'subprocess'/'stdin') ou um Named Pipe
 * (modo 'pipe'). Mensagens para humanos vão sempre para stderr, para não
 * contaminar o canal de eventos.
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

void ipc_close(void);

#endif /* IPC_H */
