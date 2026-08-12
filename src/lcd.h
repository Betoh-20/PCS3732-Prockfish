/*
 * lcd.h — Display LCD 16x2 com expansor I2C (PCF8574), opcional.
 *
 * Adaptado do driver usado no experimento 6 da bancada (`calc_desafio.c`).
 * Serve de retorno visual para a camada de teclado: sem tabuleiro físico, o
 * jogador precisa ver o que está digitando antes de confirmar.
 *
 * O display é opcional em todos os sentidos: se o barramento I2C não abrir,
 * `lcd_init()` devolve false e as demais funções viram no-ops — o programa
 * segue funcionando, com o eco indo só para stderr.
 */

#ifndef LCD_H
#define LCD_H

#include <stdbool.h>

#define LCD_COLUMNS 16

/* Abre o barramento e inicializa o display em modo 4 bits, 2 linhas.
 *
 * Args:
 *     bus:  caminho do barramento (ex: "/dev/i2c-1").
 *     addr: endereço do expansor (tipicamente 0x27 ou 0x3F).
 */
bool lcd_init(const char *bus, int addr);

bool lcd_available(void);

void lcd_clear(void);

/* Escreve `text` na linha indicada (0 ou 1), truncando ou completando com
 * espaços até 16 colunas — assim uma mensagem curta não deixa restos da
 * anterior na tela. */
void lcd_line(int row, const char *text);

void lcd_close(void);

#endif /* LCD_H */
