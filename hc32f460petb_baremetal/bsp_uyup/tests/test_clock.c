/**
 *******************************************************************************
 * @file  tests/test_clock.c
 * @brief Clock test — read and verify MPLL / bus clocks for UYUP board.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

#define HCLK_MIN    (190000000UL)   /* 190 MHz */
#define HCLK_MAX    (210000000UL)   /* 210 MHz */

void test_clock(void)
{
    stc_clock_freq_t stcFreq;

    BSP_UART_Printf("\r\n===== TEST: CLOCK =====\r\n");

    CLK_GetClockFreq(&stcFreq);

    BSP_UART_Printf("  HCLK  = %lu Hz\r\n", (unsigned long)stcFreq.u32HclkFreq);
    BSP_UART_Printf("  PCLK0 = %lu Hz\r\n", (unsigned long)stcFreq.u32Pclk0Freq);
    BSP_UART_Printf("  PCLK1 = %lu Hz\r\n", (unsigned long)stcFreq.u32Pclk1Freq);
    BSP_UART_Printf("  PCLK2 = %lu Hz\r\n", (unsigned long)stcFreq.u32Pclk2Freq);
    BSP_UART_Printf("  PCLK3 = %lu Hz\r\n", (unsigned long)stcFreq.u32Pclk3Freq);
    BSP_UART_Printf("  PCLK4 = %lu Hz\r\n", (unsigned long)stcFreq.u32Pclk4Freq);
    BSP_UART_Printf("  EXCLK = %lu Hz\r\n", (unsigned long)stcFreq.u32ExclkFreq);

    if ((stcFreq.u32HclkFreq >= HCLK_MIN) &&
        (stcFreq.u32HclkFreq <= HCLK_MAX)) {
        BSP_UART_Printf("===== TEST CLOCK: PASS =====\r\n");
    } else {
        BSP_UART_Printf("===== TEST CLOCK: FAIL (HCLK out of range) =====\r\n");
    }
}
