/*
 * runstate.c — Implementação da sinalização de encerramento (ver runstate.h).
 */

#include "runstate.h"

#include <signal.h>
#include <stddef.h>  /* NULL — o <signal.h> da glibc traz por tabela, mas
                      * depender disso é acidente e não portabilidade. */

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int signum)
{
    (void)signum;
    g_running = 0;
}

void app_install_signal_handlers(void)
{
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = handle_signal;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    /* Se a camada Python fechar a ponta de leitura, a próxima escrita vira
     * SIGPIPE. Ignorá-lo transforma isso num erro de write() tratável em
     * vez de uma morte silenciosa no meio de um evento. */
    action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &action, NULL);
}

bool app_running(void)
{
    return g_running != 0;
}

void app_request_stop(void)
{
    g_running = 0;
}
