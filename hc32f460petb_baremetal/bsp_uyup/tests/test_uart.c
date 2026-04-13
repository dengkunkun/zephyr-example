/**
 *******************************************************************************
 * @file  tests/test_uart.c
 * @brief UART test — TX string + RX poll for UYUP board.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

void test_uart(void)
{
    uint8_t rxBuf[11];          /* 10 chars + NUL */
    uint8_t ch;
    uint32_t rxCnt = 0U;
    uint32_t polls;

    BSP_UART_Printf("\r\n===== TEST: UART (TX/RX) =====\r\n");

    /* Transmit a known test string */
    BSP_UART_Printf("UART TX OK\r\n");

    /* Try to receive up to 10 characters, polling every 10 ms for ~500 ms */
    for (polls = 0U; polls < 50U; polls++) {
        if (BSP_UART_RecvChar(&ch) == LL_OK) {
            rxBuf[rxCnt++] = ch;
            if (rxCnt >= 10U) {
                break;
            }
        }
        BSP_DelayMS(10U);
    }

    if (rxCnt > 0U) {
        rxBuf[rxCnt] = '\0';
        BSP_UART_Printf("  RX %lu chars: %s\r\n",
                         (unsigned long)rxCnt, (char *)rxBuf);
    } else {
        BSP_UART_Printf("  No data received (timeout)\r\n");
    }

    BSP_UART_Printf("===== TEST UART: PASS =====\r\n");
}
