/*
 * gpio.c — Implementação do acesso aos pinos (ver gpio.h).
 *
 * Com HAVE_WIRINGPI definido, tudo é repassado para a wiringPi. Sem ele, as
 * funções viram no-ops e `gpio_setup()` falha: o binário continua compilando
 * e rodando em qualquer máquina, mas só nos modos que não tocam o hardware.
 */

#include "gpio.h"

#include <time.h>

#ifdef HAVE_WIRINGPI
#include <wiringPi.h>
#endif

static bool g_ready = false;

bool gpio_setup(void)
{
#ifdef HAVE_WIRINGPI
    /* Numeração BCM: é a usada no restante do projeto e na bancada. */
    if (wiringPiSetupGpio() < 0) {
        return false;
    }
    g_ready = true;
#endif
    return g_ready;
}

bool gpio_available(void)
{
    return g_ready;
}

void gpio_direction_set(int pin, gpio_direction dir)
{
#ifdef HAVE_WIRINGPI
    if (g_ready) {
        pinMode(pin, dir == GPIO_OUTPUT ? OUTPUT : INPUT);
    }
#else
    (void)pin;
    (void)dir;
#endif
}

void gpio_pull_set(int pin, gpio_pull pull)
{
#ifdef HAVE_WIRINGPI
    if (g_ready) {
        int mode = PUD_OFF;
        if (pull == GPIO_PULL_DOWN) {
            mode = PUD_DOWN;
        } else if (pull == GPIO_PULL_UP) {
            mode = PUD_UP;
        }
        pullUpDnControl(pin, mode);
    }
#else
    (void)pin;
    (void)pull;
#endif
}

void gpio_write(int pin, bool high)
{
#ifdef HAVE_WIRINGPI
    if (g_ready) {
        digitalWrite(pin, high ? HIGH : LOW);
    }
#else
    (void)pin;
    (void)high;
#endif
}

bool gpio_read(int pin)
{
#ifdef HAVE_WIRINGPI
    if (g_ready) {
        return digitalRead(pin) == HIGH;
    }
#else
    (void)pin;
#endif
    return false;
}

/* As esperas não dependem da wiringPi: nanosleep serve nos dois casos e
 * evita que o laço de varredura vire busy-wait numa máquina sem GPIO. */
static void sleep_ns(long nanoseconds)
{
    struct timespec ts;
    ts.tv_sec = nanoseconds / 1000000000L;
    ts.tv_nsec = nanoseconds % 1000000000L;
    nanosleep(&ts, NULL);
}

void gpio_delay_us(unsigned microseconds)
{
    sleep_ns((long)microseconds * 1000L);
}

void gpio_delay_ms(unsigned milliseconds)
{
    sleep_ns((long)milliseconds * 1000000L);
}
