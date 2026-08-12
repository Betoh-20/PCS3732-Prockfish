/*
 * main.c — Processo C do tabuleiro: escolhe a camada de entrada e a liga
 * ao canal IPC da camada Python.
 *
 * São duas camadas intercambiáveis, e a escolha é a única coisa que muda:
 *
 *   --input reed     matriz 8×8 de reed switches (caminho principal)
 *   --input keypad   teclado matricial 4×4, lances digitados (plano B)
 *
 * As duas emitem os mesmos eventos "casa:estado" em stdout (ou num FIFO),
 * então a camada Python não sabe — nem precisa saber — qual delas está
 * rodando do outro lado.
 *
 * Exemplos:
 *
 *     ./build/board_input --input reed
 *     ./build/board_input --input keypad --color white
 *     ./build/board_input --input keypad --output /tmp/chess_board_pipe
 *     ./build/board_input --input keypad --keys stdin      (teste sem hardware)
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpio.h"
#include "ipc.h"
#include "keypad_layer.h"
#include "lcd.h"
#include "reed_layer.h"
#include "runstate.h"

#define DEFAULT_I2C_BUS  "/dev/i2c-1"
#define DEFAULT_LCD_ADDR 0x27

typedef enum {
    INPUT_REED,
    INPUT_KEYPAD
} input_layer;

static void print_usage(const char *program)
{
    fprintf(stderr,
        "Uso: %s [opções]\n"
        "\n"
        "Camada de entrada do tabuleiro de xadrez eletrônico. Emite eventos\n"
        "\"casa:estado\" (ex: \"e2:0,e4:1\") para a camada Python.\n"
        "\n"
        "Gerais:\n"
        "  --input reed|keypad   Camada de entrada (padrão: reed).\n"
        "  --color white|black   Cor das peças do jogador (padrão: white).\n"
        "                        Define a posição inicial no modo keypad.\n"
        "  --output CAMINHO      Escreve num Named Pipe em vez de stdout\n"
        "                        (modo IPC 'pipe' do Python). '-' = stdout.\n"
        "  --poll-ms N           Intervalo entre varreduras.\n"
        "  --debounce N          Leituras estáveis exigidas por mudança.\n"
        "  --active-low          Inverte a polaridade da matriz escolhida.\n"
        "  --active-high         Idem, no outro sentido.\n"
        "  --help                Esta ajuda.\n"
        "\n"
        "Camada reed:\n"
        "  --reed-flip           Gira o mapeamento 180° (a1 no canto oposto).\n"
        "  --no-initial          Não envia o estado completo na primeira\n"
        "                        leitura estável.\n"
        "\n"
        "Camada keypad:\n"
        "  --keys gpio|stdin     Origem das teclas (padrão: gpio). 'stdin'\n"
        "                        aceita 0-9 A-D * # e dispensa hardware.\n"
        "  --auto-enter          Envia o lance na quarta tecla, sem '#'.\n"
        "  --lcd                 Usa um display 16x2 no I2C para mostrar o\n"
        "                        que está sendo digitado. Desligado por\n"
        "                        padrão; sem ele o eco sai só em stderr.\n"
        "  --i2c-bus CAMINHO     Barramento I2C (padrão: %s).\n"
        "  --lcd-addr 0xNN       Endereço do expansor (padrão: 0x%02X).\n",
        program, DEFAULT_I2C_BUS, DEFAULT_LCD_ADDR);
}

/* Lê o valor de uma opção, avançando o índice.
 *
 * Returns:
 *     O valor, ou NULL (com erro impresso) se a opção veio sem argumento.
 */
static const char *option_value(int argc, char **argv, int *index)
{
    if (*index + 1 >= argc) {
        fprintf(stderr, "Erro: a opção '%s' exige um valor.\n", argv[*index]);
        return NULL;
    }
    (*index)++;
    return argv[*index];
}

int main(int argc, char **argv)
{
    input_layer layer = INPUT_REED;
    player_color color = COLOR_WHITE;
    const char *output_path = NULL;
    /* O display é opcional e fica desligado por padrão: a bancada nem sempre
     * tem um, e o retorno do que está sendo digitado sai igual em stderr. */
    bool want_lcd = false;
    const char *i2c_bus = DEFAULT_I2C_BUS;
    int lcd_addr = DEFAULT_LCD_ADDR;
    int poll_ms = -1;
    int debounce = -1;
    int polarity = -1;  /* -1 = padrão da camada, 0 = ativo alto, 1 = ativo baixo */
    bool reed_flip = false;
    bool reed_emit_initial = true;
    bool keys_from_stdin = false;
    bool auto_enter = false;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return 0;

        } else if (strcmp(arg, "--input") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            if (strcmp(value, "reed") == 0) {
                layer = INPUT_REED;
            } else if (strcmp(value, "keypad") == 0) {
                layer = INPUT_KEYPAD;
            } else {
                fprintf(stderr, "Erro: camada desconhecida '%s' "
                                "(use reed ou keypad).\n", value);
                return 2;
            }

        } else if (strcmp(arg, "--color") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            if (strcmp(value, "white") == 0) {
                color = COLOR_WHITE;
            } else if (strcmp(value, "black") == 0) {
                color = COLOR_BLACK;
            } else {
                fprintf(stderr, "Erro: cor desconhecida '%s' "
                                "(use white ou black).\n", value);
                return 2;
            }

        } else if (strcmp(arg, "--output") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            output_path = value;

        } else if (strcmp(arg, "--poll-ms") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            poll_ms = atoi(value);

        } else if (strcmp(arg, "--debounce") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            debounce = atoi(value);

        } else if (strcmp(arg, "--active-low") == 0) {
            polarity = 1;
        } else if (strcmp(arg, "--active-high") == 0) {
            polarity = 0;

        } else if (strcmp(arg, "--reed-flip") == 0) {
            reed_flip = true;
        } else if (strcmp(arg, "--no-initial") == 0) {
            reed_emit_initial = false;

        } else if (strcmp(arg, "--keys") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            if (strcmp(value, "stdin") == 0) {
                keys_from_stdin = true;
            } else if (strcmp(value, "gpio") == 0) {
                keys_from_stdin = false;
            } else {
                fprintf(stderr, "Erro: origem de teclas desconhecida '%s' "
                                "(use gpio ou stdin).\n", value);
                return 2;
            }

        } else if (strcmp(arg, "--auto-enter") == 0) {
            auto_enter = true;

        } else if (strcmp(arg, "--lcd") == 0) {
            want_lcd = true;
        } else if (strcmp(arg, "--no-lcd") == 0) {
            want_lcd = false;
        } else if (strcmp(arg, "--i2c-bus") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            i2c_bus = value;
        } else if (strcmp(arg, "--lcd-addr") == 0) {
            if ((value = option_value(argc, argv, &i)) == NULL) return 2;
            lcd_addr = (int)strtol(value, NULL, 0);  /* aceita 0x27 e 39 */

        } else {
            fprintf(stderr, "Erro: opção desconhecida '%s'.\n\n", arg);
            print_usage(argv[0]);
            return 2;
        }
    }

    app_install_signal_handlers();

    if (!ipc_open(output_path)) {
        return 1;
    }

    /* O GPIO é opcional: a camada que precisar dele reclama por conta
     * própria, e o modo `--keys stdin` roda sem ele. */
    if (!gpio_setup()) {
        fprintf(stderr, "[main] GPIO não inicializado "
                        "(sem wiringPi ou sem permissão).\n");
    }

    if (want_lcd) {
        lcd_init(i2c_bus, lcd_addr);
    }

    int result;

    if (layer == INPUT_KEYPAD) {
        keypad_config config;
        keypad_config_default(&config);
        config.color = color;
        config.keys_from_stdin = keys_from_stdin;
        config.auto_enter = auto_enter;
        if (poll_ms > 0)  config.poll_ms = poll_ms;
        if (debounce > 0) config.debounce_cycles = debounce;
        if (polarity >= 0) config.active_low = (polarity == 1);

        result = keypad_layer_run(&config);
    } else {
        reed_config config;
        reed_config_default(&config);
        config.flip = reed_flip;
        config.emit_initial = reed_emit_initial;
        if (poll_ms > 0)  config.poll_ms = poll_ms;
        if (debounce > 0) config.debounce_cycles = debounce;
        if (polarity >= 0) config.active_low = (polarity == 1);

        result = reed_layer_run(&config);
    }

    lcd_close();
    ipc_close();
    return result;
}
