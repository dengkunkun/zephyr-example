/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * HC32F4A0 DDL configuration header for Zephyr build.
 * The DDL library expects hc32f4xx_conf.h to be on the include path.
 */
#ifndef __HC32F4XX_CONF_H__
#define __HC32F4XX_CONF_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Board-specific oscillator values */
#define XTAL_VALUE                                  (8000000UL)
#define XTAL32_VALUE                                (32768UL)

/* Enable only the DDL modules we need */
#define LL_ICG_ENABLE                               (DDL_ON)
#define LL_UTILITY_ENABLE                           (DDL_ON)
#define LL_PRINT_ENABLE                             (DDL_OFF)

#define LL_ADC_ENABLE                               (DDL_OFF)
#define LL_AES_ENABLE                               (DDL_OFF)
#define LL_AOS_ENABLE                               (DDL_OFF)
#define LL_CAN_ENABLE                               (DDL_OFF)
#define LL_CLK_ENABLE                               (DDL_ON)
#define LL_CMP_ENABLE                               (DDL_OFF)
#define LL_CRC_ENABLE                               (DDL_OFF)
#define LL_CTC_ENABLE                               (DDL_OFF)
#define LL_DAC_ENABLE                               (DDL_OFF)
#define LL_DBGC_ENABLE                              (DDL_OFF)
#define LL_DCU_ENABLE                               (DDL_OFF)
#define LL_DMA_ENABLE                               (DDL_OFF)
#define LL_DMC_ENABLE                               (DDL_OFF)
#define LL_DVP_ENABLE                               (DDL_OFF)
#define LL_EFM_ENABLE                               (DDL_ON)
#define LL_EMB_ENABLE                               (DDL_OFF)
#define LL_ETH_ENABLE                               (DDL_OFF)
#define LL_EVENT_PORT_ENABLE                        (DDL_OFF)
#define LL_FCG_ENABLE                               (DDL_ON)
#define LL_FCM_ENABLE                               (DDL_OFF)
#define LL_FMAC_ENABLE                              (DDL_OFF)
#define LL_GPIO_ENABLE                              (DDL_ON)
#define LL_HASH_ENABLE                              (DDL_OFF)
#define LL_HRPWM_ENABLE                             (DDL_OFF)
#define LL_I2C_ENABLE                               (DDL_OFF)
#define LL_I2S_ENABLE                               (DDL_OFF)
#define LL_INTERRUPTS_ENABLE                        (DDL_ON)
#define LL_INTERRUPTS_SHARE_ENABLE                  (DDL_ON)
#define LL_KEYSCAN_ENABLE                           (DDL_OFF)
#define LL_MAU_ENABLE                               (DDL_OFF)
#define LL_MPU_ENABLE                               (DDL_OFF)
#define LL_NFC_ENABLE                               (DDL_OFF)
#define LL_OTS_ENABLE                               (DDL_OFF)
#define LL_PWC_ENABLE                               (DDL_ON)
#define LL_QSPI_ENABLE                              (DDL_OFF)
#define LL_RMU_ENABLE                               (DDL_ON)
#define LL_RTC_ENABLE                               (DDL_OFF)
#define LL_SDIOC_ENABLE                             (DDL_OFF)
#define LL_SPI_ENABLE                               (DDL_OFF)
#define LL_SRAM_ENABLE                              (DDL_ON)
#define LL_SWDT_ENABLE                              (DDL_ON)
#define LL_TMR0_ENABLE                              (DDL_OFF)
#define LL_TMR4_ENABLE                              (DDL_OFF)
#define LL_TMR6_ENABLE                              (DDL_OFF)
#define LL_TMRA_ENABLE                              (DDL_ON)
#define LL_TRNG_ENABLE                              (DDL_OFF)
#define LL_USART_ENABLE                             (DDL_ON)
#define LL_USB_ENABLE                               (DDL_OFF)
#define LL_WDT_ENABLE                               (DDL_ON)

/* No BSP board */
#define BSP_EV_HC32F4XX                             (0U)

#ifdef __cplusplus
}
#endif

#endif /* __HC32F4XX_CONF_H__ */
