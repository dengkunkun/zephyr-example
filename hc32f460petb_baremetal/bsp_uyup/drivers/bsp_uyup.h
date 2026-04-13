/**
 *******************************************************************************
 * @file  bsp_uyup.h
 * @brief BSP header for UYUP-RPI-A-2.3 board (HC32F460PETB).
 *        Pin definitions, peripheral configuration, and API declarations.
 *******************************************************************************
 */
#ifndef __BSP_UYUP_H__
#define __BSP_UYUP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "hc32_ll.h"

/*============================================================================
 *  Crystal oscillator
 *==========================================================================*/
#define BSP_XTAL_FREQ               (24000000UL)    /* 24 MHz */
#define BSP_XTAL32_FREQ             (32768UL)       /* 32.768 kHz */

#define BSP_XTAL_PORT               (GPIO_PORT_H)
#define BSP_XTAL_PIN                (GPIO_PIN_00 | GPIO_PIN_01)
#define BSP_XTAL32_PORT             (GPIO_PORT_C)
#define BSP_XTAL32_PIN              (GPIO_PIN_14 | GPIO_PIN_15)

/*============================================================================
 *  System clock target: HCLK = 200 MHz via MPLL
 *  24 MHz / PLLM(3) * PLLN(50) / PLLP(2) = 200 MHz
 *==========================================================================*/
#define BSP_MPLL_M                  (3UL)
#define BSP_MPLL_N                  (50UL)
#define BSP_MPLL_P                  (2UL)
#define BSP_MPLL_Q                  (2UL)
#define BSP_MPLL_R                  (2UL)
#define BSP_HCLK_FREQ               (200000000UL)

/*============================================================================
 *  LEDs — active-low (VDD → 1K → LED → pin)
 *==========================================================================*/
#define BSP_LED_NUM                 (2U)

#define BSP_LED3_PORT               (GPIO_PORT_D)
#define BSP_LED3_PIN                (GPIO_PIN_10)
#define BSP_LED4_PORT               (GPIO_PORT_E)
#define BSP_LED4_PIN                (GPIO_PIN_15)

#define BSP_LED3                    (0U)
#define BSP_LED4                    (1U)

#define BSP_LED_ON(port, pin)       GPIO_ResetPins((port), (pin))
#define BSP_LED_OFF(port, pin)      GPIO_SetPins((port), (pin))
#define BSP_LED_TOGGLE(port, pin)   GPIO_TogglePins((port), (pin))

/*============================================================================
 *  Buttons — active-low (GND → pin, need internal pull-up)
 *==========================================================================*/
#define BSP_KEY_NUM                 (2U)

#define BSP_BTN0_PORT               (GPIO_PORT_A)
#define BSP_BTN0_PIN                (GPIO_PIN_03)
#define BSP_BTN1_PORT               (GPIO_PORT_E)
#define BSP_BTN1_PIN                (GPIO_PIN_02)

#define BSP_KEY_BTN0                (0x01UL)
#define BSP_KEY_BTN1                (0x02UL)

/*============================================================================
 *  WS2812B RGB LEDs — PB1 (Timer3 CH4), chain of 2
 *==========================================================================*/
#define BSP_WS2812_PORT             (GPIO_PORT_B)
#define BSP_WS2812_PIN              (GPIO_PIN_01)
#define BSP_WS2812_TMR_FUNC         (GPIO_FUNC_5)
#define BSP_WS2812_LED_COUNT        (2U)

/*============================================================================
 *  USART1 — VCP debug console (DAPLink bridge on PA9/PA10)
 *==========================================================================*/
#define BSP_USART1_UNIT             (CM_USART1)
#define BSP_USART1_FCG              (FCG1_PERIPH_USART1)
#define BSP_USART1_BAUDRATE         (115200UL)

#define BSP_USART1_TX_PORT          (GPIO_PORT_A)
#define BSP_USART1_TX_PIN           (GPIO_PIN_09)
#define BSP_USART1_TX_FUNC          (GPIO_FUNC_32)

#define BSP_USART1_RX_PORT          (GPIO_PORT_A)
#define BSP_USART1_RX_PIN           (GPIO_PIN_10)
#define BSP_USART1_RX_FUNC          (GPIO_FUNC_33)

/* Interrupt sources for USART1 */
#define BSP_USART1_RI_SRC           (INT_SRC_USART1_RI)
#define BSP_USART1_EI_SRC           (INT_SRC_USART1_EI)
#define BSP_USART1_TI_SRC           (INT_SRC_USART1_TI)
#define BSP_USART1_TCI_SRC          (INT_SRC_USART1_TCI)
#define BSP_USART1_RI_IRQn          (INT000_IRQn)
#define BSP_USART1_EI_IRQn          (INT001_IRQn)

/*============================================================================
 *  USART3 — RS485 via SP3485EN (PD8/PD9 + PA0 DE/RE)
 *==========================================================================*/
#define BSP_USART3_UNIT             (CM_USART3)
#define BSP_USART3_FCG              (FCG1_PERIPH_USART3)
#define BSP_USART3_BAUDRATE         (115200UL)

#define BSP_USART3_TX_PORT          (GPIO_PORT_D)
#define BSP_USART3_TX_PIN           (GPIO_PIN_08)
#define BSP_USART3_TX_FUNC          (GPIO_FUNC_32)

#define BSP_USART3_RX_PORT          (GPIO_PORT_D)
#define BSP_USART3_RX_PIN           (GPIO_PIN_09)
#define BSP_USART3_RX_FUNC          (GPIO_FUNC_33)

/* RS485 direction control: PA0, HIGH = transmit, LOW = receive */
#define BSP_RS485_DE_PORT           (GPIO_PORT_A)
#define BSP_RS485_DE_PIN            (GPIO_PIN_00)
#define BSP_RS485_TX_EN()           GPIO_SetPins(BSP_RS485_DE_PORT, BSP_RS485_DE_PIN)
#define BSP_RS485_RX_EN()           GPIO_ResetPins(BSP_RS485_DE_PORT, BSP_RS485_DE_PIN)

/*============================================================================
 *  I2C3 — AT24C64D EEPROM (PB8 SCL / PB9 SDA)
 *  NOTE: PB8/PB9 are Func_Grp2, so FUNC_48/49 maps to I2C3 (not I2C1).
 *==========================================================================*/
#define BSP_I2C_UNIT                (CM_I2C3)
#define BSP_I2C_FCG                 (FCG1_PERIPH_I2C3)
#define BSP_I2C_BAUDRATE            (100000UL)
#define BSP_I2C_TIMEOUT             (0x40000UL)

#define BSP_I2C_SCL_PORT            (GPIO_PORT_B)
#define BSP_I2C_SCL_PIN             (GPIO_PIN_08)
#define BSP_I2C_SCL_FUNC            (GPIO_FUNC_49)

#define BSP_I2C_SDA_PORT            (GPIO_PORT_B)
#define BSP_I2C_SDA_PIN             (GPIO_PIN_09)
#define BSP_I2C_SDA_FUNC            (GPIO_FUNC_48)

/* AT24C64D EEPROM */
#define BSP_EEPROM_ADDR             (0x50U)
#define BSP_EEPROM_PAGE_SIZE        (32U)
#define BSP_EEPROM_SIZE             (8192U)     /* 64Kbit = 8KB */
#define BSP_EEPROM_ADDR_LEN         (2U)        /* 2-byte address for 24C64 */

/*============================================================================
 *  SPI3 — W25Q32 SPI Flash (PB3/PB4/PB5/PE3)
 *  NOTE: PB3/PB4/PB5 are Func_Grp2, so FUNC_40-43 maps to SPI3 (not SPI1).
 *        PB3=JTDO, PB4=NJTRST — must clear PSPCR bits to release from JTAG.
 *==========================================================================*/
#define BSP_SPI_UNIT                (CM_SPI3)
#define BSP_SPI_FCG                 (FCG1_PERIPH_SPI3)

#define BSP_SPI_SCK_PORT            (GPIO_PORT_B)
#define BSP_SPI_SCK_PIN             (GPIO_PIN_03)
#define BSP_SPI_SCK_FUNC            (GPIO_FUNC_43)

#define BSP_SPI_MISO_PORT           (GPIO_PORT_B)
#define BSP_SPI_MISO_PIN            (GPIO_PIN_04)
#define BSP_SPI_MISO_FUNC           (GPIO_FUNC_41)

#define BSP_SPI_MOSI_PORT           (GPIO_PORT_B)
#define BSP_SPI_MOSI_PIN            (GPIO_PIN_05)
#define BSP_SPI_MOSI_FUNC           (GPIO_FUNC_40)

/* W25Q32 CS — software controlled */
#define BSP_SPI_CS_PORT             (GPIO_PORT_E)
#define BSP_SPI_CS_PIN              (GPIO_PIN_03)
#define BSP_SPI_CS_LOW()            GPIO_ResetPins(BSP_SPI_CS_PORT, BSP_SPI_CS_PIN)
#define BSP_SPI_CS_HIGH()           GPIO_SetPins(BSP_SPI_CS_PORT, BSP_SPI_CS_PIN)

/* W25Q32 commands */
#define W25Q_CMD_WRITE_EN           (0x06U)
#define W25Q_CMD_WRITE_DIS          (0x04U)
#define W25Q_CMD_READ_SR1           (0x05U)
#define W25Q_CMD_READ_DATA          (0x03U)
#define W25Q_CMD_PAGE_PROG          (0x02U)
#define W25Q_CMD_SECTOR_ERASE       (0x20U)
#define W25Q_CMD_CHIP_ERASE         (0xC7U)
#define W25Q_CMD_READ_ID            (0x9FU)
#define W25Q_CMD_POWER_DOWN         (0xB9U)
#define W25Q_CMD_RELEASE_PD         (0xABU)
#define W25Q_PAGE_SIZE              (256U)
#define W25Q_SECTOR_SIZE            (4096U)
#define W25Q_SR1_BUSY               (0x01U)

/*============================================================================
 *  CAN1 — PE5(TX) / PE6(RX)
 *==========================================================================*/
#define BSP_CAN_UNIT                (CM_CAN)
#define BSP_CAN_FCG                 (FCG1_PERIPH_CAN)

#define BSP_CAN_TX_PORT             (GPIO_PORT_E)
#define BSP_CAN_TX_PIN              (GPIO_PIN_05)
#define BSP_CAN_TX_FUNC             (GPIO_FUNC_50)

#define BSP_CAN_RX_PORT             (GPIO_PORT_E)
#define BSP_CAN_RX_PIN              (GPIO_PIN_06)
#define BSP_CAN_RX_FUNC             (GPIO_FUNC_51)

/*============================================================================
 *  SDIOC — SD Card 4-bit mode
 *==========================================================================*/
#define BSP_SDIOC_UNIT              (CM_SDIOC1)
#define BSP_SDIOC_FCG               (FCG1_PERIPH_SDIOC1)

#define BSP_SDIOC_CLK_PORT          (GPIO_PORT_C)
#define BSP_SDIOC_CLK_PIN           (GPIO_PIN_12)
#define BSP_SDIOC_CMD_PORT          (GPIO_PORT_D)
#define BSP_SDIOC_CMD_PIN           (GPIO_PIN_02)
#define BSP_SDIOC_D0_PORT           (GPIO_PORT_C)
#define BSP_SDIOC_D0_PIN            (GPIO_PIN_08)
#define BSP_SDIOC_D1_PORT           (GPIO_PORT_C)
#define BSP_SDIOC_D1_PIN            (GPIO_PIN_09)
#define BSP_SDIOC_D2_PORT           (GPIO_PORT_C)
#define BSP_SDIOC_D2_PIN            (GPIO_PIN_10)
#define BSP_SDIOC_D3_PORT           (GPIO_PORT_C)
#define BSP_SDIOC_D3_PIN            (GPIO_PIN_11)
#define BSP_SDIOC_FUNC              (GPIO_FUNC_9)

/* SD card detect pin */
#define BSP_SDIOC_DET_PORT          (GPIO_PORT_E)
#define BSP_SDIOC_DET_PIN           (GPIO_PIN_14)

/*============================================================================
 *  Timer4 PWM — PD12(CH1) / PD13(CH2) / PD14(CH3) / PD15(CH4)
 *==========================================================================*/
#define BSP_TMR4_UNIT               (CM_TMR4_1)
#define BSP_TMR4_FCG                (FCG2_PERIPH_TMR4_1)
#define BSP_TMR4_PWM_FUNC           (GPIO_FUNC_2)

#define BSP_TMR4_CH1_PORT           (GPIO_PORT_D)
#define BSP_TMR4_CH1_PIN            (GPIO_PIN_12)
#define BSP_TMR4_CH2_PORT           (GPIO_PORT_D)
#define BSP_TMR4_CH2_PIN            (GPIO_PIN_13)
#define BSP_TMR4_CH3_PORT           (GPIO_PORT_D)
#define BSP_TMR4_CH3_PIN            (GPIO_PIN_14)
#define BSP_TMR4_CH4_PORT           (GPIO_PORT_D)
#define BSP_TMR4_CH4_PIN            (GPIO_PIN_15)

/*============================================================================
 *  USB — PA11(D-) / PA12(D+) / PB15(USB_ON)
 *==========================================================================*/
#define BSP_USB_DM_PORT             (GPIO_PORT_A)
#define BSP_USB_DM_PIN              (GPIO_PIN_11)
#define BSP_USB_DP_PORT             (GPIO_PORT_A)
#define BSP_USB_DP_PIN              (GPIO_PIN_12)
#define BSP_USB_ON_PORT             (GPIO_PORT_B)
#define BSP_USB_ON_PIN              (GPIO_PIN_15)

/*============================================================================
 *  SWD — PA13(SWDIO) / PA14(SWCLK)
 *==========================================================================*/
#define BSP_SWD_IO_PORT             (GPIO_PORT_A)
#define BSP_SWD_IO_PIN              (GPIO_PIN_13)
#define BSP_SWD_CK_PORT             (GPIO_PORT_A)
#define BSP_SWD_CK_PIN              (GPIO_PIN_14)

/*============================================================================
 *  Peripheral write-protection selection
 *==========================================================================*/
#define BSP_LL_PERIPH_SEL   \
    (LL_PERIPH_GPIO | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | \
     LL_PERIPH_EFM | LL_PERIPH_SRAM)

/*============================================================================
 *  BSP API
 *==========================================================================*/

/* Core init: clock → GPIO → USART1 VCP */
void BSP_CLK_Init(void);
int32_t BSP_XTAL32_Init(void);
void BSP_LED_Init(void);
void BSP_KEY_Init(void);
void BSP_UART_Init(void);
void BSP_Init(void);

/* LED control */
void BSP_LED_On(uint8_t u8Led);
void BSP_LED_Off(uint8_t u8Led);
void BSP_LED_Toggle(uint8_t u8Led);

/* Key status (returns 1 if pressed) */
uint8_t BSP_KEY_GetStatus(uint32_t u32Key);

/* UART VCP send/receive */
void BSP_UART_SendChar(uint8_t ch);
void BSP_UART_SendStr(const char *s);
void BSP_UART_Printf(const char *fmt, ...);
int32_t BSP_UART_RecvChar(uint8_t *ch);

/* Simple delay */
void BSP_DelayMS(uint32_t ms);
void BSP_DelayUS(uint32_t us);

/* WDT helper — call after starting WDT so BSP_DelayMS auto-feeds */
void BSP_WDT_SetRunning(void);
void BSP_WDT_Feed(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UYUP_H__ */
