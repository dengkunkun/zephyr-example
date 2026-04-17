/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HC32 pinctrl function codes.
 *
 * These are the values programmed into the GPIO PFSRx.FSEL[5:0] field to
 * route a pin to an alternate function. The code set is shared between
 * HC32F460 and HC32F4A0 (same Function Select table layout). Availability
 * of a particular function on a specific pin is SoC/package dependent — see
 * the chip datasheet "Pin Function" table (HC32F460 DS §2.3 Pin Multiplexing
 * / HC32F4A0 DS §2 Pin Assignments).
 *
 * The TX/RX split (TX=low even code, RX=next code) is a convention observed
 * in FMR_CC io.c for HC32F4A0: e.g. USART1 TX=32 RX=33. The encodings below
 * are taken directly from the vendor DDL and FMR_CC io.c.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_HC32_PINCTRL_FUNC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_HC32_PINCTRL_FUNC_H_

#define HC32_FUNC_GPIO                 0

/* ---- Analog functions (keep digital function off via PCR.DDIS=1) ---- */
#define HC32_FUNC_ANALOG               0

/* ---- Sub-function: USB HS (F4A0) ---- */
#define HC32_FUNC_USBHS_VBUS           12

/* ---- USART1 ---- */
#define HC32_FUNC_USART1_TX            32
#define HC32_FUNC_USART1_RX            33
#define HC32_FUNC_USART1_RTS           34
#define HC32_FUNC_USART1_CTS           35

/* ---- USART2 ---- */
#define HC32_FUNC_USART2_TX            34
#define HC32_FUNC_USART2_RX            35
#define HC32_FUNC_USART2_RTS           36
#define HC32_FUNC_USART2_CTS           37

/* ---- USART3 ---- */
#define HC32_FUNC_USART3_TX            32
#define HC32_FUNC_USART3_RX            33
#define HC32_FUNC_USART3_RTS           36
#define HC32_FUNC_USART3_CTS           37

/* ---- USART4 ---- */
#define HC32_FUNC_USART4_TX            36
#define HC32_FUNC_USART4_RX            37

/* ---- USART5 (HC32F4A0 FMR 485-lift) ---- */
#define HC32_FUNC_USART5_TX            34
#define HC32_FUNC_USART5_RX            35

/* ---- USART6 (HC32F4A0 FMR moto) ---- */
#define HC32_FUNC_USART6_TX            36
#define HC32_FUNC_USART6_RX            37

/* ---- USART7 (HC32F4A0 FMR servo) ---- */
#define HC32_FUNC_USART7_TX            38
#define HC32_FUNC_USART7_RX            39

/* ---- USART8/9/10 (HC32F4A0) ---- */
#define HC32_FUNC_USART8_TX            40
#define HC32_FUNC_USART8_RX            41
#define HC32_FUNC_USART9_TX            42
#define HC32_FUNC_USART9_RX            43
#define HC32_FUNC_USART10_TX           44
#define HC32_FUNC_USART10_RX           45

/* ---- SPI1..SPI6 ---- */
#define HC32_FUNC_SPI1_SCK             7
#define HC32_FUNC_SPI1_MOSI            6
#define HC32_FUNC_SPI1_MISO            5
#define HC32_FUNC_SPI1_NSS             4
#define HC32_FUNC_SPI2_SCK             11
#define HC32_FUNC_SPI2_MOSI            10
#define HC32_FUNC_SPI2_MISO            9
#define HC32_FUNC_SPI2_NSS             8

/* ---- I2C1..I2C6 ---- */
#define HC32_FUNC_I2C1_SCL             48
#define HC32_FUNC_I2C1_SDA             49
#define HC32_FUNC_I2C2_SCL             50
#define HC32_FUNC_I2C2_SDA             51
#define HC32_FUNC_I2C3_SCL             48
#define HC32_FUNC_I2C3_SDA             49

/* ---- TimerA PWM (encoding varies per port/channel — use values from DS) ---- */
#define HC32_FUNC_TIMA1_PWM1           4
#define HC32_FUNC_TIMA1_PWM2           4
#define HC32_FUNC_TIMA2_PWM1           5
#define HC32_FUNC_TIMA3_PWM4           5

/* ---- USB FS/HS ---- */
#define HC32_FUNC_USBFS_DM             10
#define HC32_FUNC_USBFS_DP             10
#define HC32_FUNC_USBFS_VBUS           11
#define HC32_FUNC_USBHS_DM_PHY         10
#define HC32_FUNC_USBHS_DP_PHY         10

/* ---- Event port / debug ---- */
#define HC32_FUNC_EVENTOUT             1
#define HC32_FUNC_MCO1                 1
#define HC32_FUNC_MCO2                 1

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_HC32_PINCTRL_FUNC_H_ */
