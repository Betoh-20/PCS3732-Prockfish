/*
 * keypad_layer.c — Implementação da entrada por teclado (ver keypad_layer.h).
 */

#include "keypad_layer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "gpio.h"
#include "ipc.h"
#include "lcd.h"
#include "runstate.h"

/* Disposição de um teclado de membrana 4×4 comum. */
static const char KEYMAP[KEYPAD_ROWS][KEYPAD_COLUMNS] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' },
};

#define STATUS_MAX (LCD_COLUMNS + 1)

/* O que está sendo digitado agora. O modo determina quantas teclas a entrada
 * ainda espera e o que o '#' vai fazer com elas. */
typedef enum {
    ENTRY_MOVE,     /* <coluna><fileira><coluna><fileira> — o caso normal */
    ENTRY_COMMAND,  /* '0' digitado: esperando o dígito do comando */
    ENTRY_REMOVE,   /* 0-1: casa a esvaziar */
    ENTRY_PLACE,    /* 0-2: casa a ocupar */
    ENTRY_RESYNC,   /* 0-9: reenviar o estado completo */
    ENTRY_RESET     /* 0-0: voltar à posição inicial */
} entry_mode;

typedef struct {
    keypad_config config;
    board_mirror mirror;

    entry_mode mode;
    char text[5];       /* casas já resolvidas, ex: "e2e4" */
    int length;

    /* Multi-toque: qual tecla de letra gerou `text[letter_slot]` e quantas
     * vezes ela foi pressionada seguidas. Sem isso não haveria como saber
     * que o segundo 'A' é a continuação do primeiro, e não uma coluna nova. */
    char letter_key;
    int letter_slot;
    int letter_taps;

    char status[STATUS_MAX];
} keypad_state;

/* ------------------------------------------------------------------------ */
/*  Apresentação                                                            */
/* ------------------------------------------------------------------------ */

/* As mensagens são ASCII sem acento de propósito: o LCD HD44780 não tem os
 * caracteres acentuados, e elas são as mesmas que vão para o stderr. */
static void keypad_status(keypad_state *state, const char *message)
{
    snprintf(state->status, sizeof(state->status), "%s", message);
}

static const char *mode_prefix(entry_mode mode)
{
    switch (mode) {
    case ENTRY_MOVE:    return "Lance: ";
    case ENTRY_COMMAND: return "Comando: ";
    case ENTRY_REMOVE:  return "Retirar: ";
    case ENTRY_PLACE:   return "Colocar: ";
    case ENTRY_RESYNC:  return "Reenviar? ";
    case ENTRY_RESET:   return "Reiniciar? ";
    }
    return "";
}

/* Quantas teclas de casa o modo corrente ainda espera antes do '#'. */
static int expected_length(entry_mode mode)
{
    switch (mode) {
    case ENTRY_MOVE:              return 4;
    case ENTRY_REMOVE:
    case ENTRY_PLACE:             return 2;
    case ENTRY_COMMAND:
    case ENTRY_RESYNC:
    case ENTRY_RESET:             return 0;
    }
    return 0;
}

/* Linha do que está sendo digitado, em maiúsculas (mais legível no LCD).
 * Um '_' no fim marca que ainda falta tecla. */
static void render_entry(const keypad_state *state, char *out, size_t size)
{
    char typed[8];
    int i = 0;
    for (; i < state->length && i < (int)sizeof(typed) - 2; i++) {
        typed[i] = (char)toupper((unsigned char)state->text[i]);
    }
    if (state->length < expected_length(state->mode)) {
        typed[i++] = '_';
    }
    typed[i] = '\0';

    snprintf(out, size, "%s%s", mode_prefix(state->mode), typed);
}

static void keypad_refresh(const keypad_state *state)
{
    char entry[LCD_COLUMNS * 2];
    render_entry(state, entry, sizeof(entry));

    lcd_line(0, state->status);
    lcd_line(1, entry);

    fprintf(stderr, "[teclado] %-16s | %s\n", state->status, entry);
}

/* ------------------------------------------------------------------------ */
/*  Entrada                                                                 */
/* ------------------------------------------------------------------------ */

static void entry_clear(keypad_state *state)
{
    state->mode = ENTRY_MOVE;
    state->length = 0;
    state->text[0] = '\0';
    state->letter_key = '\0';
    state->letter_slot = -1;
    state->letter_taps = 0;
}

/* Coluna do tabuleiro para uma tecla de letra, conforme o número de toques:
 * ímpar = a letra da tecla (A→a), par = a letra seguinte do bloco (AA→e). */
static char file_for_key(char key, int taps)
{
    int base = key - 'A';                       /* A..D → 0..3 */
    int offset = (taps % 2 == 0) ? 4 : 0;       /* segundo toque salta a-d → e-h */
    return (char)('a' + base + offset);
}

static void keypad_letter(keypad_state *state, char key)
{
    /* Mesma tecla, mesma posição: é o multi-toque, não uma coluna nova. */
    if (state->length > 0
            && state->letter_key == key
            && state->letter_slot == state->length - 1) {
        state->letter_taps++;
        state->text[state->letter_slot] = file_for_key(key, state->letter_taps);
        keypad_status(state, "Coluna trocada");
        return;
    }

    if (state->length >= expected_length(state->mode)) {
        keypad_status(state, "Confirme com #");
        return;
    }
    /* Posição par é coluna, ímpar é fileira — a alternância é o que torna o
     * multi-toque não ambíguo. */
    if (state->length % 2 != 0) {
        keypad_status(state, "Falta a fileira");
        return;
    }

    state->text[state->length] = file_for_key(key, 1);
    state->letter_key = key;
    state->letter_slot = state->length;
    state->letter_taps = 1;
    state->length++;
    state->text[state->length] = '\0';
    keypad_status(state, "Digitando...");
}

static void keypad_rank(keypad_state *state, char key)
{
    if (key == '9') {
        keypad_status(state, "Fileira e 1 a 8");
        return;
    }
    if (state->length >= expected_length(state->mode)) {
        keypad_status(state, "Confirme com #");
        return;
    }
    if (state->length % 2 == 0) {
        keypad_status(state, "Falta a coluna");
        return;
    }

    state->text[state->length] = key;
    state->length++;
    state->text[state->length] = '\0';
    state->letter_key = '\0';
    state->letter_slot = -1;
    keypad_status(state, "Digitando...");
}

static void keypad_select_command(keypad_state *state, char key)
{
    switch (key) {
    case '1':
        state->mode = ENTRY_REMOVE;
        keypad_status(state, "Retirar peca");
        return;
    case '2':
        state->mode = ENTRY_PLACE;
        keypad_status(state, "Colocar peca");
        return;
    case '9':
        state->mode = ENTRY_RESYNC;
        keypad_status(state, "Reenviar tudo?");
        return;
    case '0':
        state->mode = ENTRY_RESET;
        keypad_status(state, "Posicao inicial?");
        return;
    default:
        keypad_status(state, "Use 1 2 9 ou 0");
        return;
    }
}

static void keypad_backspace(keypad_state *state)
{
    if (state->length > 0) {
        state->length--;
        state->text[state->length] = '\0';
        if (state->letter_slot >= state->length) {
            state->letter_key = '\0';
            state->letter_slot = -1;
        }
        keypad_status(state, "Apagado");
        return;
    }

    if (state->mode != ENTRY_MOVE) {
        entry_clear(state);
        keypad_status(state, "Digite o lance");
        return;
    }

    keypad_status(state, "Nada a apagar");
}

/* ------------------------------------------------------------------------ */
/*  Execução                                                                */
/* ------------------------------------------------------------------------ */

static void emit_full_state(keypad_state *state)
{
    board_change snapshot[BOARD_SQUARES];
    size_t count = board_mirror_snapshot(&state->mirror, snapshot);
    ipc_emit(snapshot, count);
}

/* Envia o lance digitado.
 *
 * As duas recusas locais (origem vazia, destino ocupado) são as mesmas que o
 * tabuleiro físico imporia sozinho — não dá para levantar uma peça que não
 * está lá, nem pôr duas peças suas na mesma casa. A legalidade do lance
 * continua sendo assunto do Python, como na camada de reed.
 *
 * Returns:
 *     true se o evento foi emitido (aí a entrada pode ser limpa).
 */
static bool keypad_send_move(keypad_state *state)
{
    int from = board_parse_square(state->text);
    int to = board_parse_square(state->text + 2);

    if (from < 0 || to < 0) {
        keypad_status(state, "Casa invalida");
        return false;
    }
    if (from == to) {
        keypad_status(state, "Origem=destino");
        return false;
    }
    if (!state->mirror.occupied[from]) {
        keypad_status(state, "Origem vazia");
        return false;
    }
    if (state->mirror.occupied[to]) {
        keypad_status(state, "Destino ocupado");
        return false;
    }

    state->mirror.occupied[from] = false;
    state->mirror.occupied[to] = true;

    board_change changes[2] = { { from, 0 }, { to, 1 } };
    ipc_emit(changes, 2);

    char message[STATUS_MAX];
    snprintf(message, sizeof(message), "Enviado %s%s",
             board_square_name(from), board_square_name(to));
    keypad_status(state, message);
    return true;
}

static bool keypad_send_square(keypad_state *state, int target_state)
{
    int square = board_parse_square(state->text);
    if (square < 0) {
        keypad_status(state, "Casa invalida");
        return false;
    }

    bool occupied = state->mirror.occupied[square];
    if (occupied == (target_state == 1)) {
        keypad_status(state, occupied ? "Ja tem peca" : "Ja esta vazia");
        return false;
    }

    state->mirror.occupied[square] = (target_state == 1);
    ipc_emit_single(square, target_state);

    char message[STATUS_MAX];
    snprintf(message, sizeof(message), "%s %s",
             target_state ? "Colocada em" : "Retirada de",
             board_square_name(square));
    keypad_status(state, message);
    return true;
}

static void keypad_confirm(keypad_state *state)
{
    if (state->mode == ENTRY_COMMAND) {
        keypad_status(state, "Use 1 2 9 ou 0");
        return;
    }

    if (state->length != expected_length(state->mode)) {
        keypad_status(state, "Faltam teclas");
        return;
    }

    bool done = false;

    switch (state->mode) {
    case ENTRY_MOVE:
        done = keypad_send_move(state);
        break;
    case ENTRY_REMOVE:
        done = keypad_send_square(state, 0);
        break;
    case ENTRY_PLACE:
        done = keypad_send_square(state, 1);
        break;
    case ENTRY_RESYNC:
        emit_full_state(state);
        keypad_status(state, "Estado reenviado");
        done = true;
        break;
    case ENTRY_RESET:
        board_mirror_initial(&state->mirror, state->config.color);
        emit_full_state(state);
        keypad_status(state, "Posicao inicial");
        done = true;
        break;
    case ENTRY_COMMAND:
        break;
    }

    if (done) {
        /* Só a entrada volta ao zero: o status do que acabou de acontecer
         * ("Enviado e2e4") fica na tela até a próxima tecla. */
        entry_clear(state);
    }
}

static void keypad_key(keypad_state *state, char key)
{
    if (key == '#') {
        keypad_confirm(state);
    } else if (key == '*') {
        keypad_backspace(state);
    } else if (state->mode == ENTRY_COMMAND) {
        keypad_select_command(state, key);
    } else if (state->mode == ENTRY_RESYNC || state->mode == ENTRY_RESET) {
        keypad_status(state, "Confirme com #");
    } else if (key >= 'A' && key <= 'D') {
        keypad_letter(state, key);
    } else if (key >= '1' && key <= '9') {
        keypad_rank(state, key);
    } else if (key == '0') {
        if (state->mode == ENTRY_MOVE && state->length == 0) {
            state->mode = ENTRY_COMMAND;
            keypad_status(state, "Comando: 1 2 9 0");
        } else {
            keypad_status(state, "Fileira e 1 a 8");
        }
    } else {
        keypad_status(state, "Tecla invalida");
    }

    /* Atalho de demonstração: fecha o lance sem esperar o '#'. */
    if (state->config.auto_enter
            && state->mode == ENTRY_MOVE
            && state->length == expected_length(ENTRY_MOVE)) {
        keypad_confirm(state);
    }

    keypad_refresh(state);
}

/* ------------------------------------------------------------------------ */
/*  Varredura do teclado                                                    */
/* ------------------------------------------------------------------------ */

void keypad_config_default(keypad_config *config)
{
    static const int default_rows[KEYPAD_ROWS]       = { 16, 20, 21, 26 };
    static const int default_columns[KEYPAD_COLUMNS] = { 19, 13,  6,  5 };

    memcpy(config->row_pins, default_rows, sizeof(default_rows));
    memcpy(config->column_pins, default_columns, sizeof(default_columns));

    config->active_low = false;
    config->debounce_cycles = 2;
    config->poll_ms = 25;
    config->auto_enter = false;
    config->keys_from_stdin = false;
    config->color = COLOR_WHITE;
}

static void keypad_setup_pins(const keypad_config *config)
{
    for (int row = 0; row < KEYPAD_ROWS; row++) {
        gpio_direction_set(config->row_pins[row], GPIO_OUTPUT);
        gpio_write(config->row_pins[row], config->active_low);  /* repouso */
    }
    for (int col = 0; col < KEYPAD_COLUMNS; col++) {
        gpio_direction_set(config->column_pins[col], GPIO_INPUT);
        gpio_pull_set(config->column_pins[col],
                      config->active_low ? GPIO_PULL_UP : GPIO_PULL_DOWN);
    }
}

/* Uma varredura: devolve a primeira tecla pressionada, ou '\0'. */
static char keypad_scan_once(const keypad_config *config)
{
    char found = '\0';

    for (int row = 0; row < KEYPAD_ROWS && found == '\0'; row++) {
        gpio_write(config->row_pins[row], !config->active_low);
        gpio_delay_us(5);

        for (int col = 0; col < KEYPAD_COLUMNS; col++) {
            bool level = gpio_read(config->column_pins[col]);
            if (config->active_low ? !level : level) {
                found = KEYMAP[row][col];
                break;
            }
        }

        gpio_write(config->row_pins[row], config->active_low);
    }

    return found;
}

static void keypad_announce(const keypad_state *state)
{
    fprintf(stderr,
            "[teclado] Camada de teclado 4x4 ativa (pecas %s).\n"
            "[teclado]   Lance: coluna + fileira + coluna + fileira, ex: A2A4\n"
            "[teclado]   Colunas e-h: repita a tecla (AA=e BB=f CC=g DD=h)\n"
            "[teclado]   '#' confirma, '*' apaga\n"
            "[teclado]   0 1 <casa> #  retira a peca da casa\n"
            "[teclado]   0 2 <casa> #  coloca uma peca na casa\n"
            "[teclado]   0 9 #         reenvia o estado das 64 casas\n"
            "[teclado]   0 0 #         volta a posicao inicial\n",
            state->config.color == COLOR_WHITE ? "brancas" : "pretas");
}

/* Modo de teste sem hardware: as teclas chegam por stdin. */
static int keypad_run_stdin(keypad_state *state)
{
    fprintf(stderr, "[teclado] Lendo teclas de stdin (Ctrl+D encerra).\n");

    int c;
    while (app_running() && (c = getchar()) != EOF) {
        char key = (char)toupper(c);
        /* Espaços e quebras de linha são ignorados, para poder digitar
         * "AA2 AA4 #". O teste de '\0' evita casar com o terminador que o
         * strchr também encontra. */
        if (key != '\0' && strchr("0123456789ABCD*#", key) != NULL) {
            keypad_key(state, key);
        }
    }
    return 0;
}

static int keypad_run_gpio(keypad_state *state)
{
    const keypad_config *config = &state->config;

    if (!gpio_available()) {
        fprintf(stderr,
                "[teclado] GPIO indisponível: rode num Raspberry Pi com a "
                "wiringPi, ou use --keys stdin para testar sem hardware.\n");
        return 1;
    }

    keypad_setup_pins(config);

    char candidate = '\0';  /* tecla vista na varredura mais recente */
    char current = '\0';    /* tecla já confirmada (só muda depois de estável) */
    int stable = 0;

    while (app_running()) {
        char key = keypad_scan_once(config);

        if (key == candidate) {
            if (stable < config->debounce_cycles) {
                stable++;
            }
        } else {
            candidate = key;
            stable = 1;
        }

        /* Só a transição para uma tecla confirmada conta: segurar a tecla
         * não repete, e soltar volta o estado para '\0' sem gerar evento. */
        if (stable >= config->debounce_cycles && candidate != current) {
            current = candidate;
            if (current != '\0') {
                keypad_key(state, current);
            }
        }

        gpio_delay_ms((unsigned)config->poll_ms);
    }

    return 0;
}

int keypad_layer_run(const keypad_config *config)
{
    keypad_state state;
    memset(&state, 0, sizeof(state));
    state.config = *config;

    board_mirror_initial(&state.mirror, config->color);
    entry_clear(&state);
    keypad_status(&state, "Digite o lance");

    keypad_announce(&state);
    keypad_refresh(&state);

    int result = config->keys_from_stdin
        ? keypad_run_stdin(&state)
        : keypad_run_gpio(&state);

    fprintf(stderr, "[teclado] Camada de teclado encerrada.\n");
    return result;
}
