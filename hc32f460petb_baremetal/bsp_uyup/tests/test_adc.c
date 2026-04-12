/**
 *******************************************************************************
 * @file  tests/test_adc.c
 * @brief ADC test — on-chip temperature sensor (OTS) for UYUP board.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_ots.h"

void test_adc(void)
{
    stc_ots_init_t stcOtsInit;
    float32_t f32Temp;
    int32_t i32Ret;

    BSP_UART_Printf("\r\n===== TEST: ADC / OTS =====\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    FCG_Fcg3PeriphClockCmd(FCG3_PERIPH_ADC1, ENABLE);
    FCG_Fcg3PeriphClockCmd(FCG3_PERIPH_OTS, ENABLE);

    (void)OTS_StructInit(&stcOtsInit);
    stcOtsInit.u16ClockSrc = OTS_CLK_XTAL;
    (void)OTS_Init(&stcOtsInit);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    i32Ret = OTS_Polling(&f32Temp, 1000000UL);
    if (LL_OK == i32Ret) {
        int16_t i16TempInt = (int16_t)f32Temp;
        BSP_UART_Printf("  Chip temperature: ~%d C\r\n", (int)i16TempInt);
    } else {
        BSP_UART_Printf("  OTS read failed (ret=%ld)\r\n", (long)i32Ret);
    }

    (void)OTS_DeInit();

    BSP_UART_Printf("===== TEST ADC / OTS: PASS =====\r\n");
}
