/**
 *******************************************************************************
 * @file  source/main.c
 * @brief Comprehensive BSP test demo for UYUP-RPI-A-2.3 (HC32F460PETB).
 *
 *        Runs all test functions sequentially.
 *        Tests requiring external devices are commented out by default.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

int32_t main(void) {
  /* Initialize board: system clock 200 MHz, LEDs, buttons, USART1 VCP */
  BSP_Init();

  BSP_UART_Printf("\r\n");
  BSP_UART_Printf("========================================\r\n");
  BSP_UART_Printf("  HC32F460 UYUP Board Test Suite\r\n");
  BSP_UART_Printf("  HCLK = %lu MHz\r\n", BSP_HCLK_FREQ / 1000000UL);
  BSP_UART_Printf("========================================\r\n\r\n");

  /* ---- Phase 1: Auto tests (no external hardware needed) ---- */
  BSP_UART_Printf("--- Auto Tests ---\r\n");
  test_gpio();
  test_clock();
  test_timer();
  test_dma();
  test_adc();
  test_rtc();
  test_crypto();
  test_wdt(); /* Starts WDT feeding — BSP_DelayMS now auto-feeds */
  test_flash_internal();

  /* ---- Phase 2: Board peripheral tests (on-board devices) ---- */
  BSP_UART_Printf("\r\n--- Board Peripheral Tests ---\r\n");
  BSP_WDT_Feed();
  test_i2c_eeprom();
  BSP_WDT_Feed();
  test_spi_flash();
  BSP_WDT_Feed();
  test_sdio();
  BSP_WDT_Feed();
  //   test_ws2812b();

  /* ---- Phase 3: Official DDL examples (ported) ---- */
  BSP_UART_Printf("\r\n--- Ported Examples ---\r\n");
  BSP_WDT_Feed();
  test_examples();

  /* ---- Phase 4: Tests requiring external devices (uncomment to run) ---- */
  BSP_UART_Printf("\r\n--- External Device Tests (disabled) ---\r\n");
  BSP_WDT_Feed();
  // test_can();       /* Needs CAN bus slave device */
  // test_rs485();     /* Needs RS485 slave device */
  // test_usb();       /* Needs USB host connection + DDL midwares */
  BSP_UART_Printf("  (CAN, RS485, USB — uncomment in main.c to run)\r\n");

  /* ---- UART echo test runs last (interactive, blocks forever) ---- */
  BSP_UART_Printf("\r\n--- Interactive UART Echo ---\r\n");
  test_uart();

  /* Should not reach here (test_uart loops forever) */
  BSP_UART_Printf("\r\n========================================\r\n");
  BSP_UART_Printf("  All tests completed\r\n");
  BSP_UART_Printf("========================================\r\n");

  for (;;) {
    BSP_WDT_Feed();
    __WFI();
  }
}
