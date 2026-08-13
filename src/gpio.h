/*
 * gpio.h — Acesso aos pinos do Raspberry Pi (numeração BCM).
 *
 * Camada fina sobre a wiringPi, com dois objetivos:
 *
 *   1. Isolar as camadas de entrada (reed switches e teclado matricial) da
 *      biblioteca de GPIO — as duas varrem uma matriz do mesmo jeito, só
 *      mudam os pinos e a polaridade.
 *
 *   2. Permitir compilar o programa fora do Raspberry Pi. Sem a wiringPi,
 *      `gpio_setup()` devolve false e sobra o caminho de teste do teclado
 *      (`--keys stdin`), que exercita o parser e o protocolo IPC inteiros
 *      sem hardware nenhum.
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>

typedef enum {
    GPIO_INPUT,
    GPIO_OUTPUT
} gpio_direction;

typedef enum {
    GPIO_PULL_OFF,
    GPIO_PULL_DOWN,
    GPIO_PULL_UP
} gpio_pull;

/* Inicializa a wiringPi em numeração BCM. Falso se não há GPIO nesta
 * máquina (compilado sem wiringPi) ou se a inicialização falhou. */
bool gpio_setup(void);

/* Verdadeiro depois de um `gpio_setup()` bem-sucedido. */
bool gpio_available(void);

void gpio_direction_set(int pin, gpio_direction dir);
void gpio_pull_set(int pin, gpio_pull pull);
void gpio_write(int pin, bool high);
bool gpio_read(int pin);

/* Esperas — disponíveis mesmo sem GPIO (usam nanosleep). */
void gpio_delay_us(unsigned microseconds);
void gpio_delay_ms(unsigned milliseconds);

#endif /* GPIO_H */
