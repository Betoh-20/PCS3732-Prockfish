/*
 * lcd.c — Driver I2C do LCD 16x2 (ver lcd.h).
 *
 * O expansor PCF8574 entrega 8 bits ao display, usados assim:
 *
 *     bit 0 (0x01) — RS: 0 = comando, 1 = dado
 *     bit 2 (0x04) — E : pulso de escrita
 *     bit 3 (0x08) — backlight
 *     bits 4-7     — barramento de dados (modo 4 bits)
 *
 * Cada byte vai em dois nibbles, cada um com E em 1 e depois em 0.
 */

#include "lcd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/i2c-dev.h>
#endif

#define LCD_MODE_CMD  0x00
#define LCD_MODE_CHR  0x01
#define LCD_ENABLE    0x04
#define LCD_BACKLIGHT 0x08

/* Endereços da DDRAM da primeira coluna de cada linha. */
static const uint8_t LCD_ROW_ADDR[2] = { 0x80, 0xC0 };

static int g_fd = -1;

/* Escreve um byte no display.
 *
 * Na primeira falha o display é DESLIGADO em vez de a falha ser só
 * reportada. Sem isso, um display ausente ou mal endereçado custa 34
 * mensagens de erro e ~100 ms de `usleep` a cada tecla — o `ioctl(I2C_SLAVE)`
 * não vai ao barramento conferir quem está lá, então uma escrita que falha é
 * a primeira notícia de que não há display, e a única útil.
 *
 * Returns:
 *     true se o byte foi escrito.
 */
static bool lcd_write_byte(uint8_t bits, uint8_t mode)
{
    if (g_fd < 0) {
        return false;
    }

    uint8_t high = mode | (bits & 0xF0) | LCD_BACKLIGHT;
    uint8_t low  = mode | (uint8_t)((bits << 4) & 0xF0) | LCD_BACKLIGHT;

    uint8_t buffer[4] = {
        (uint8_t)(high | LCD_ENABLE), (uint8_t)(high & ~LCD_ENABLE),
        (uint8_t)(low  | LCD_ENABLE), (uint8_t)(low  & ~LCD_ENABLE),
    };

    if (write(g_fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer)) {
        /* Um display que sumiu no meio da partida não é motivo para
         * derrubar o processo: o jogo continua pelo eco em stderr. */
        fprintf(stderr, "[lcd] Escrita I2C falhou (%s) — display desligado. "
                        "O eco continua no terminal.\n", strerror(errno));
        close(g_fd);
        g_fd = -1;
        return false;
    }
    usleep(3000);  /* tempo de processamento do controlador */
    return true;
}

bool lcd_init(const char *bus, int addr)
{
#ifdef __linux__
    g_fd = open(bus, O_RDWR);
    if (g_fd < 0) {
        fprintf(stderr, "[lcd] Barramento '%s' indisponível (%s) — "
                        "seguindo sem display.\n", bus, strerror(errno));
        return false;
    }
    if (ioctl(g_fd, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "[lcd] Endereço 0x%02X não respondeu (%s) — "
                        "seguindo sem display.\n", addr, strerror(errno));
        close(g_fd);
        g_fd = -1;
        return false;
    }

    lcd_write_byte(0x33, LCD_MODE_CMD);  /* inicialização em 8 bits... */
    lcd_write_byte(0x32, LCD_MODE_CMD);  /* ...e troca para 4 bits */
    lcd_write_byte(0x28, LCD_MODE_CMD);  /* 4 bits, 2 linhas, 5x8 */
    lcd_write_byte(0x0C, LCD_MODE_CMD);  /* display ligado, sem cursor */
    lcd_write_byte(0x06, LCD_MODE_CMD);  /* avanço automático do cursor */
    lcd_write_byte(0x01, LCD_MODE_CMD);  /* limpa */
    usleep(3000);

    /* A sequência acima é a primeira conversa de verdade com o display: se
     * ele não estiver no endereço, ela falhou e já se desligou sozinha. */
    if (!lcd_available()) {
        fprintf(stderr, "[lcd] Nenhum display em 0x%02X — seguindo sem ele.\n",
                addr);
        return false;
    }
    return true;
#else
    (void)bus;
    (void)addr;
    fprintf(stderr, "[lcd] I2C disponível apenas no Linux — "
                    "seguindo sem display.\n");
    return false;
#endif
}

bool lcd_available(void)
{
    return g_fd >= 0;
}

void lcd_clear(void)
{
    if (g_fd < 0) {
        return;
    }
    lcd_write_byte(0x01, LCD_MODE_CMD);
    usleep(3000);
}

void lcd_line(int row, const char *text)
{
    if (g_fd < 0 || row < 0 || row > 1) {
        return;
    }

    lcd_write_byte(LCD_ROW_ADDR[row], LCD_MODE_CMD);

    size_t length = (text != NULL) ? strlen(text) : 0;
    for (int col = 0; col < LCD_COLUMNS; col++) {
        char c = ((size_t)col < length) ? text[col] : ' ';
        lcd_write_byte((uint8_t)c, LCD_MODE_CHR);
    }
}

void lcd_close(void)
{
    if (g_fd >= 0) {
        lcd_clear();
        close(g_fd);
        g_fd = -1;
    }
}
