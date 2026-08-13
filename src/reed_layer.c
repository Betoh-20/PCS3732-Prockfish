/*
 * reed_layer.c — Implementação da varredura dos reed switches (ver reed_layer.h).
 */

#include "reed_layer.h"

#include <stdio.h>
#include <string.h>

#include "gpio.h"
#include "ipc.h"
#include "runstate.h"

void reed_config_default(reed_config *config)
{
    static const int default_lines[BOARD_RANKS]   = { 4,  5,  6, 12, 13, 16, 19, 20 };
    static const int default_columns[BOARD_FILES] = { 21, 22, 23, 24, 25, 26, 27, 17 };

    memcpy(config->line_pins, default_lines, sizeof(default_lines));
    memcpy(config->column_pins, default_columns, sizeof(default_columns));

    config->active_low = true;
    config->debounce_cycles = 5;
    config->poll_ms = 10;
    config->flip = false;
    config->emit_initial = true;
}

static void reed_setup_pins(const reed_config *config)
{
    for (int col = 0; col < BOARD_FILES; col++) {
        gpio_direction_set(config->column_pins[col], GPIO_INPUT);
        gpio_pull_set(config->column_pins[col],
                      config->active_low ? GPIO_PULL_UP : GPIO_PULL_DOWN);
    }

    /* As linhas ficam em alta impedância entre as varreduras: só a linha
     * sendo lida vira saída. É o que impede uma linha de curto-circuitar a
     * outra através de um reed switch fechado. */
    for (int line = 0; line < BOARD_RANKS; line++) {
        gpio_direction_set(config->line_pins[line], GPIO_INPUT);
    }
}

/* Índice da casa correspondente a uma interseção da matriz. */
static int reed_square(const reed_config *config, int line, int col)
{
    int rank = config->flip ? (BOARD_RANKS - 1 - line) : line;
    int file = config->flip ? (BOARD_FILES - 1 - col) : col;
    return board_square(file, rank);
}

/* Uma varredura completa da matriz para `out`. */
static void reed_scan(const reed_config *config, bool out[BOARD_SQUARES])
{
    for (int line = 0; line < BOARD_RANKS; line++) {
        int line_pin = config->line_pins[line];

        gpio_direction_set(line_pin, GPIO_OUTPUT);
        gpio_write(line_pin, !config->active_low);

        gpio_delay_us(5);  /* acomodação do nível na linha */

        for (int col = 0; col < BOARD_FILES; col++) {
            bool level = gpio_read(config->column_pins[col]);
            bool closed = config->active_low ? !level : level;
            out[reed_square(config, line, col)] = closed;
        }

        gpio_direction_set(line_pin, GPIO_INPUT);
    }
}

/* Emite as casas em que `current` difere de `previous`. */
static void reed_emit_diff(const bool current[BOARD_SQUARES],
                           const bool previous[BOARD_SQUARES])
{
    board_change changes[BOARD_SQUARES];
    size_t count = 0;

    for (int square = 0; square < BOARD_SQUARES; square++) {
        if (current[square] != previous[square]) {
            changes[count].square = square;
            changes[count].state = current[square] ? 1 : 0;
            count++;
        }
    }

    if (count > 0) {
        ipc_emit(changes, count);
    }
}

int reed_layer_run(const reed_config *config)
{
    if (!gpio_available()) {
        fprintf(stderr,
                "[reed] GPIO indisponível: a camada de reed switches precisa "
                "de um Raspberry Pi com a wiringPi instalada.\n");
        return 1;
    }

    reed_setup_pins(config);

    board_mirror stable;      /* última grade confirmada (base do diff) */
    bool raw[BOARD_SQUARES];  /* leitura da varredura atual */
    bool candidate[BOARD_SQUARES];
    int stable_count = 0;
    bool have_baseline = false;

    board_mirror_clear(&stable);
    memset(candidate, 0, sizeof(candidate));

    fprintf(stderr,
            "[reed] Varrendo a matriz 8×8 (debounce: %d ciclos de %d ms).\n",
            config->debounce_cycles, config->poll_ms);

    while (app_running()) {
        reed_scan(config, raw);

        /* Debouncing por amostragem consecutiva: a grade só é aceita depois
         * de se repetir idêntica `debounce_cycles` vezes. */
        if (memcmp(raw, candidate, sizeof(raw)) == 0) {
            if (stable_count < config->debounce_cycles) {
                stable_count++;
            }
        } else {
            memcpy(candidate, raw, sizeof(raw));
            stable_count = 1;
        }

        if (stable_count >= config->debounce_cycles) {
            if (!have_baseline) {
                /* Primeira leitura confirmada: é a montagem do tabuleiro. */
                have_baseline = true;
                memcpy(stable.occupied, candidate, sizeof(candidate));
                if (config->emit_initial) {
                    board_change snapshot[BOARD_SQUARES];
                    size_t count = board_mirror_snapshot(&stable, snapshot);
                    ipc_emit(snapshot, count);
                    fprintf(stderr, "[reed] Estado inicial enviado.\n");
                }
            } else if (memcmp(candidate, stable.occupied,
                              sizeof(candidate)) != 0) {
                reed_emit_diff(candidate, stable.occupied);
                memcpy(stable.occupied, candidate, sizeof(candidate));
            }
        }

        gpio_delay_ms((unsigned)config->poll_ms);
    }

    fprintf(stderr, "[reed] Varredura encerrada.\n");
    return 0;
}
