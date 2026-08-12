/*
 * runstate.h — Sinalização de encerramento compartilhada pelas camadas.
 *
 * As duas camadas de entrada rodam um laço infinito de varredura. Um
 * Ctrl+C (ou o SIGTERM que a camada Python manda ao encerrar o subprocesso)
 * precisa sair desse laço pela porta da frente, para o FIFO e o display
 * serem fechados direito.
 */

#ifndef RUNSTATE_H
#define RUNSTATE_H

#include <stdbool.h>

/* Instala os tratadores de SIGINT/SIGTERM. */
void app_install_signal_handlers(void);

bool app_running(void);

void app_request_stop(void);

#endif /* RUNSTATE_H */
