/**
 *******************************************************************************
 * @file  test_all.h
 * @brief Declarations of all test functions for UYUP board BSP.
 *******************************************************************************
 */
#ifndef __TEST_ALL_H__
#define __TEST_ALL_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Auto tests (no external hardware needed) ---- */
void test_gpio(void);           /* LED blink + button read */
void test_uart(void);           /* USART1 VCP echo test */
void test_clock(void);          /* Verify MPLL 200MHz config */
void test_timer(void);          /* Timer4 PWM breathing LED */
void test_dma(void);            /* DMA memory-to-memory copy */
void test_adc(void);            /* Internal temp sensor + Vref */
void test_rtc(void);            /* RTC calendar read */
void test_crypto(void);         /* AES + HASH + CRC + TRNG */
void test_wdt(void);            /* Watchdog feed test */
void test_flash_internal(void); /* Internal EFM read/write */

/* ---- Board peripheral tests (on-board hardware) ---- */
void test_i2c_eeprom(void);     /* AT24C64D EEPROM read/write */
void test_spi_flash(void);      /* W25Q32 SPI Flash read/write */
void test_sdio(void);           /* SD card block read/write */
void test_ws2812b(void);        /* WS2812B RGB LED chain */

/* ---- External device tests (commented in main by default) ---- */
void test_can(void);            /* CAN1 loopback self-test */
void test_rs485(void);          /* RS485 half-duplex (needs slave) */
void test_usb(void);            /* USB device (needs host) */

/* ---- Official DDL examples port ---- */
void test_examples(void);       /* Run ported official examples */

#ifdef __cplusplus
}
#endif

#endif /* __TEST_ALL_H__ */
