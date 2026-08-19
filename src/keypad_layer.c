/*
 * keypad_layer.c — Implementação da entrada por teclado (ver keypad_layer.h).
 */

#include "keypad_layer.h"

#include <ctype.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

/* Uma linha de comando cabe no prefixo mais as 64 casas, com folga. */
#define COMMAND_MAX 128

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

/* Conta à camada Python o que está sendo digitado agora.
 *
 * Sem isto, o teclado só apareceria na aplicação quando o lance ficasse
 * pronto: o jogador digita quatro teclas às cegas e o monitor à frente dele
 * não mostra nada. Com a origem já resolvida (duas teclas), o Python
 * consegue ainda destacar para onde aquela peça pode ir.
 *
 * A linha vai pelo mesmo canal dos eventos, marcada com '@' — ver ipc.h.
 */
static void keypad_emit_entry(const keypad_state *state)
{
    char entry[LCD_COLUMNS * 2];
    const char *origin = "";
    const char *target = "";
    int square;

    /* Só um lance tem origem e destino; nos comandos (retirar, colocar) a
     * casa digitada não é uma peça escolhida, e prever lances dali seria
     * mentira. */
    if (state->mode == ENTRY_MOVE) {
        if (state->length >= 2 && (square = board_parse_square(state->text)) >= 0) {
            origin = board_square_name(square);
        }
        if (state->length >= 4
                && (square = board_parse_square(state->text + 2)) >= 0) {
            target = board_square_name(square);
        }
    }

    /* Entrada vazia no modo normal não tem o que mostrar: a barra de status
     * da aplicação volta a dizer de quem é a vez. */
    if (state->length > 0 || state->mode != ENTRY_MOVE) {
        render_entry(state, entry, sizeof(entry));
    } else {
        entry[0] = '\0';
    }

    char line[160];
    snprintf(line, sizeof(line), "@entry|%s|%s|%s|%s",
             origin, target, entry, state->status);
    ipc_emit_line(line);
}

static void keypad_refresh(const keypad_state *state)
{
    char entry[LCD_COLUMNS * 2];
    render_entry(state, entry, sizeof(entry));

    lcd_line(0, state->status);
    lcd_line(1, entry);

    fprintf(stderr, "[teclado] %-16s | %s\n", state->status, entry);

    keypad_emit_entry(state);
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
/*  Comandos vindos da camada Python                                        */
/* ------------------------------------------------------------------------ */

/* O espelho desta camada é uma aposta: ele nasce na posição inicial e só muda
 * com o que o jogador digita. Isso basta enquanto todas as peças que saem do
 * tabuleiro saem por lance do próprio jogador — mas não é o caso quando o
 * oponente captura. A peça capturada é do jogador, está na mesa e no espelho,
 * e nada no teclado avisou que ela saiu.
 *
 * Antes, quem avisava era o jogador, digitando `0 1 <casa> #`. Agora a camada
 * Python — que é quem sabe o tabuleiro virtual — manda o espelho dela por
 * stdin, e esta camada adota. Ver `_push_mirror_to_hardware` em app/main.py.
 *
 * O canal é de mão única no sentido inverso ao dos eventos: nada do que chega
 * aqui vira evento de volta, senão o Python leria como movimento o que ele
 * mesmo acabou de mandar.
 */

#define SYNC_PREFIX "@sync|"

/* Acumula uma linha '@...' vinda de stdin.
 *
 * Precisa ser um acumulador, e não um `fgets`, porque no modo `--keys stdin`
 * o mesmo descritor traz as teclas: só o que começa uma linha com '@' é
 * comando, o resto continua sendo tecla, caractere a caractere. */
typedef struct {
    char text[COMMAND_MAX];
    int length;
    bool active;      /* dentro de uma linha de comando */
    bool line_start;  /* o próximo caractere abre uma linha */
} command_reader;

static void command_reader_init(command_reader *reader)
{
    reader->length = 0;
    reader->text[0] = '\0';
    reader->active = false;
    reader->line_start = true;
}

/* Consome um caractere.
 *
 * Returns:
 *     true se `reader->text` passou a conter uma linha de comando completa;
 *     false se o caractere foi consumido pelo comando em andamento. Quando
 *     devolve false com `*key` diferente de '\0', o caractere não era de
 *     comando nenhum e o chamador decide o que fazer com ele. */
static bool command_reader_feed(command_reader *reader, char c, char *key)
{
    *key = '\0';

    if (reader->active) {
        if (c == '\n' || c == '\r') {
            reader->text[reader->length] = '\0';
            reader->active = false;
            reader->line_start = true;
            return true;
        }
        if (reader->length < COMMAND_MAX - 1) {
            reader->text[reader->length++] = c;
        }
        /* Linha maior que o buffer: o excedente é descartado e o comando
         * truncado será recusado na validação, que é o certo — melhor não
         * aplicar meio espelho. */
        return false;
    }

    if (reader->line_start && c == '@') {
        reader->active = true;
        reader->length = 0;
        reader->text[reader->length++] = c;
        return false;
    }

    reader->line_start = (c == '\n' || c == '\r');
    *key = c;
    return false;
}

/* Adota o espelho que a camada Python mandou.
 *
 * Args:
 *     payload: 64 caracteres '0'/'1', na ordem a1..h8 (índice = fileira*8 +
 *              coluna, a mesma de `board_square`).
 *
 * Returns:
 *     true se o espelho foi adotado. */
static bool keypad_apply_sync(keypad_state *state, const char *payload)
{
    if (strlen(payload) != (size_t)BOARD_SQUARES) {
        fprintf(stderr, "[teclado] @sync com %zu casas (esperava %d) — "
                        "ignorado.\n", strlen(payload), BOARD_SQUARES);
        return false;
    }

    /* Validação antes de aplicar: um payload meio truncado não pode deixar
     * metade do espelho atualizada. */
    for (int square = 0; square < BOARD_SQUARES; square++) {
        if (payload[square] != '0' && payload[square] != '1') {
            fprintf(stderr, "[teclado] @sync com caractere invalido '%c' na "
                            "casa %s — ignorado.\n",
                    payload[square], board_square_name(square));
            return false;
        }
    }

    int changed = 0;
    int last = -1;
    bool last_occupied = false;

    for (int square = 0; square < BOARD_SQUARES; square++) {
        bool occupied = (payload[square] == '1');
        if (state->mirror.occupied[square] != occupied) {
            state->mirror.occupied[square] = occupied;
            changed++;
            last = square;
            last_occupied = occupied;
            fprintf(stderr, "[teclado] sincronizado: %s %s\n",
                    board_square_name(square),
                    occupied ? "ocupada" : "vazia");
        }
    }

    if (changed == 0) {
        return false;
    }

    /* Uma casa só é o caso normal (a peça que o oponente capturou), e aí o
     * LCD diz qual peça sair da mesa em vez de um "sincronizado" genérico. */
    if (changed == 1) {
        char message[STATUS_MAX];
        snprintf(message, sizeof(message), "%s %s",
                 last_occupied ? "Ponha" : "Retire", board_square_name(last));
        keypad_status(state, message);
    } else {
        keypad_status(state, "Sincronizado");
    }
    return true;
}

/* Despacha uma linha de comando completa. */
static void keypad_command(keypad_state *state, const char *line)
{
    if (strncmp(line, SYNC_PREFIX, strlen(SYNC_PREFIX)) == 0) {
        if (keypad_apply_sync(state, line + strlen(SYNC_PREFIX))) {
            keypad_refresh(state);
        }
        return;
    }

    fprintf(stderr, "[teclado] Comando desconhecido: '%s'\n", line);
}

/* Lê o que houver em stdin sem bloquear e trata os comandos.
 *
 * Só é usada no modo GPIO: ali stdin não tem outro dono. As teclas que
 * aparecerem por engano (alguém rodando o binário à mão num terminal) são
 * descartadas — quem manda nas teclas é a matriz.
 *
 * Returns:
 *     false em EOF (a camada Python fechou o canal). */
static bool keypad_poll_commands(keypad_state *state, command_reader *reader)
{
    struct pollfd fds = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };

    while (poll(&fds, 1, 0) > 0) {
        /* Descritor quebrado ou inválido: desistir é melhor que insistir a
         * cada varredura num canal que não vai voltar. */
        if (fds.revents & (POLLERR | POLLNVAL)) {
            return false;
        }
        if (!(fds.revents & (POLLIN | POLLHUP))) {
            break;
        }

        char buffer[COMMAND_MAX];
        ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) {
            return false;  /* EOF ou erro: não há mais comandos a esperar */
        }
        for (ssize_t i = 0; i < count; i++) {
            char key;
            if (command_reader_feed(reader, buffer[i], &key)) {
                keypad_command(state, reader->text);
            }
        }
    }
    return true;
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
    config->raw_mode = false;
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

/* Uma varredura: devolve a primeira tecla pressionada, ou '\0'.
 *
 * `out_row` e `out_col` (opcionais) recebem a interseção onde a tecla foi
 * encontrada — é o que o modo de bancada precisa para dizer qual par de
 * pinos fechou. */
static char keypad_scan_once(const keypad_config *config,
                             int *out_row, int *out_col)
{
    char found = '\0';

    for (int row = 0; row < KEYPAD_ROWS && found == '\0'; row++) {
        gpio_write(config->row_pins[row], !config->active_low);
        gpio_delay_us(5);

        for (int col = 0; col < KEYPAD_COLUMNS; col++) {
            bool level = gpio_read(config->column_pins[col]);
            if (config->active_low ? !level : level) {
                found = KEYMAP[row][col];
                if (out_row) *out_row = row;
                if (out_col) *out_col = col;
                break;
            }
        }

        gpio_write(config->row_pins[row], config->active_low);
    }

    return found;
}

static void keypad_announce(const keypad_state *state)
{
    if (state->config.raw_mode) {
        fprintf(stderr,
                "[bancada] Modo de conferência da fiação: cada tecla mostra\n"
                "[bancada] em que interseção da matriz ela foi lida. Nenhum\n"
                "[bancada] evento é enviado. Ctrl+C encerra.\n"
                "[bancada] Linhas:  %d %d %d %d\n"
                "[bancada] Colunas: %d %d %d %d\n"
                "[bancada] Se nenhuma tecla aparecer, tente --active-low.\n",
                state->config.row_pins[0], state->config.row_pins[1],
                state->config.row_pins[2], state->config.row_pins[3],
                state->config.column_pins[0], state->config.column_pins[1],
                state->config.column_pins[2], state->config.column_pins[3]);
        return;
    }

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

/* Modo de teste sem hardware: as teclas chegam por stdin.
 *
 * O mesmo descritor traz os comandos da camada Python, e é por isso que as
 * teclas passam pelo acumulador: uma linha começada em '@' é comando, o resto
 * continua sendo tecla. */
static int keypad_run_stdin(keypad_state *state)
{
    fprintf(stderr, "[teclado] Lendo teclas de stdin (Ctrl+D encerra).\n");

    command_reader reader;
    command_reader_init(&reader);

    int c;
    while (app_running() && (c = getchar()) != EOF) {
        char raw;
        if (command_reader_feed(&reader, (char)c, &raw)) {
            keypad_command(state, reader.text);
            continue;
        }
        if (raw == '\0') {
            continue;  /* consumido pelo comando em andamento */
        }

        char key = (char)toupper((unsigned char)raw);
        /* Espaços e quebras de linha são ignorados, para poder digitar
         * "AA2 AA4 #". O teste de '\0' evita casar com o terminador que o
         * strchr também encontra. */
        if (strchr("0123456789ABCD*#", key) != NULL) {
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
    int cand_row = -1, cand_col = -1;

    /* Comandos da camada Python só são esperados quando stdin é um cano: num
     * terminal ele é do usuário, e ler dali roubaria o que ele digita. */
    command_reader reader;
    command_reader_init(&reader);
    bool listen_commands = !isatty(STDIN_FILENO);
    if (listen_commands) {
        fprintf(stderr, "[teclado] Ouvindo comandos da camada Python em stdin.\n");
    }

    while (app_running()) {
        if (listen_commands && !keypad_poll_commands(state, &reader)) {
            listen_commands = false;  /* canal fechado: segue só com o teclado */
        }

        int row = -1, col = -1;
        char key = keypad_scan_once(config, &row, &col);

        if (key == candidate) {
            if (stable < config->debounce_cycles) {
                stable++;
            }
        } else {
            candidate = key;
            cand_row = row;
            cand_col = col;
            stable = 1;
        }

        /* Só a transição para uma tecla confirmada conta: segurar a tecla
         * não repete, e soltar volta o estado para '\0' sem gerar evento. */
        if (stable >= config->debounce_cycles && candidate != current) {
            current = candidate;
            if (current == '\0') {
                /* nada a fazer: é a tecla sendo solta */
            } else if (config->raw_mode) {
                fprintf(stderr,
                        "[bancada] tecla '%c'  (linha %d = pino %d, "
                        "coluna %d = pino %d)\n",
                        current, cand_row, config->row_pins[cand_row],
                        cand_col, config->column_pins[cand_col]);
            } else {
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
    if (!config->raw_mode) {
        keypad_refresh(&state);
    }

    int result = config->keys_from_stdin
        ? keypad_run_stdin(&state)
        : keypad_run_gpio(&state);

    fprintf(stderr, "[teclado] Camada de teclado encerrada.\n");
    return result;
}
