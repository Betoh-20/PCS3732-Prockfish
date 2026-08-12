/*
 * ipc.c — Implementação do canal de eventos (ver ipc.h).
 */

#include "ipc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static FILE *g_out = NULL;
static bool g_owns_out = false;

bool ipc_open(const char *path)
{
    if (path == NULL || strcmp(path, "-") == 0) {
        g_out = stdout;
        g_owns_out = false;
        /* Linha a linha: a camada Python lê com readline() e um evento
         * preso no buffer é uma jogada que não chega. */
        setvbuf(g_out, NULL, _IOLBF, 0);
        return true;
    }

    /* Named Pipe (FIFO). Criá-lo aqui deixa o processo C independente da
     * ordem de partida: quem chegar primeiro espera o outro. */
    if (mkfifo(path, 0600) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ipc] Não foi possível criar o FIFO '%s': %s\n",
                path, strerror(errno));
        return false;
    }

    fprintf(stderr, "[ipc] Aguardando a camada Python abrir '%s'...\n", path);
    g_out = fopen(path, "w");  /* bloqueia até haver um leitor */
    if (g_out == NULL) {
        fprintf(stderr, "[ipc] Não foi possível abrir o FIFO '%s': %s\n",
                path, strerror(errno));
        return false;
    }

    g_owns_out = true;
    setvbuf(g_out, NULL, _IOLBF, 0);
    fprintf(stderr, "[ipc] Conectado a '%s'.\n", path);
    return true;
}

void ipc_emit(const board_change *changes, size_t count)
{
    if (g_out == NULL || count == 0) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        fprintf(g_out, "%s%s:%d",
                (i > 0) ? "," : "",
                board_square_name(changes[i].square),
                changes[i].state ? 1 : 0);
    }
    fputc('\n', g_out);
    fflush(g_out);
}

void ipc_emit_single(int square, int state)
{
    board_change change = { square, state };
    ipc_emit(&change, 1);
}

void ipc_close(void)
{
    if (g_out != NULL) {
        fflush(g_out);
        if (g_owns_out) {
            fclose(g_out);
        }
    }
    g_out = NULL;
    g_owns_out = false;
}
