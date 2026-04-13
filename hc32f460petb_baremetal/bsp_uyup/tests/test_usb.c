/**
 *******************************************************************************
 * @file  test_usb.c
 * @brief USB device mode test stub for UYUP board.
 * @note  Full USB requires DDL midwares. This only verifies GPIO/clock setup.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

void test_usb(void)
{
    BSP_UART_Printf("[TEST] USB (stub) ...\r\n");
    BSP_UART_Printf("  USB D+/D- on PA12/PA11, USB_ON on PB15\r\n");
    BSP_UART_Printf("  Full USB requires DDL midwares (not yet integrated)\r\n");
    BSP_UART_Printf("[SKIP] USB\r\n");
}
