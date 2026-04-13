/**
 *******************************************************************************
 * @file  tests/test_gpio.c
 * @brief GPIO test — LED toggle + button read for UYUP board.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

void test_gpio(void)
{
    uint8_t i;
    uint8_t btn0, btn1;

    BSP_UART_Printf("\r\n===== TEST: GPIO (LED + KEY) =====\r\n");

    /* Toggle LED3 and LED4 alternately 5 times */
    for (i = 0U; i < 5U; i++) {
        BSP_LED_On(BSP_LED3);
        BSP_LED_Off(BSP_LED4);
        BSP_UART_Printf("  LED3=ON  LED4=OFF  [%u/5]\r\n", (unsigned)(i + 1U));
        BSP_DelayMS(200U);

        BSP_LED_Off(BSP_LED3);
        BSP_LED_On(BSP_LED4);
        BSP_UART_Printf("  LED3=OFF LED4=ON   [%u/5]\r\n", (unsigned)(i + 1U));
        BSP_DelayMS(200U);
    }

    /* Leave both LEDs off */
    BSP_LED_Off(BSP_LED3);
    BSP_LED_Off(BSP_LED4);

    /* Read button states */
    btn0 = BSP_KEY_GetStatus(BSP_KEY_BTN0);
    btn1 = BSP_KEY_GetStatus(BSP_KEY_BTN1);
    BSP_UART_Printf("  BTN0=%s  BTN1=%s\r\n",
                     btn0 ? "PRESSED" : "RELEASED",
                     btn1 ? "PRESSED" : "RELEASED");

    BSP_UART_Printf("===== TEST GPIO: PASS =====\r\n");
}
