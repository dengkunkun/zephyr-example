/*
 * Minimal picolibc stdout stub for bare-metal HC32F460.
 * Provides the stdout FILE* required by picolibc's printf.
 * The DDL utility (hc32_ll_utility.c) references printf even though
 * our application uses direct register writes for USART output.
 */
#include <stdio.h>

static int _stub_putc(char c, FILE *f)
{
    (void)f;
    (void)c;
    return c;
}

static FILE __stdout = FDEV_SETUP_STREAM(_stub_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &__stdout;
