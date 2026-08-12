/*
 * lcd.h — Display LCD 16x2 com expansor I2C (PCF8574), opcional.
 *
 * Adaptado do driver usado no experimento 6 da bancada (`calc_desafio.c`).
 * Serve de retorno visual opcional para a camada de teclado, que mostra o
 * lance sendo digitado antes de confirmar.
 *
 * O display fica DESLIGADO por padrão (só entra com `--lcd`): o eco em stderr
 * já cobre a mesma necessidade, e nem toda bancada tem um.
 *
 * Quando ligado, ele falha para o lado seguro. `open()` do barramento e
 * `ioctl(I2C_SLAVE)` funcionam mesmo sem display nenhum ligado — o ioctl só
 * registra o endereço de destino, não vai ao barramento conferir. Então a
 * ausência de display só aparece na primeira escrita, e é ali que ele se
 * desliga: uma mensagem, e o programa segue com o eco em stderr.
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
