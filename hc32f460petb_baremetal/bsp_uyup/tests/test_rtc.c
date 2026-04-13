/**
 *******************************************************************************
 * @file  test_rtc.c
 * @brief RTC calendar test for UYUP board (32.768 kHz XTAL32).
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_rtc.h"
#include "hc32_ll_clk.h"

void test_rtc(void)
{
    stc_rtc_init_t stcInit;
    stc_rtc_date_t stcDate;
    stc_rtc_time_t stcTime;
    int32_t i32Ret;

    BSP_UART_Printf("[TEST] RTC ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Enable XTAL32 for RTC */
    (void)BSP_XTAL32_Init();

    /* RTC init */
    (void)RTC_DeInit();
    (void)RTC_StructInit(&stcInit);
    stcInit.u8ClockSrc  = RTC_CLK_SRC_XTAL32;
    stcInit.u8HourFormat = RTC_HOUR_FMT_24H;
    i32Ret = RTC_Init(&stcInit);
    if (LL_OK != i32Ret) {
        BSP_UART_Printf("  RTC init failed (%ld)\r\n", (long)i32Ret);
        BSP_UART_Printf("[FAIL] RTC\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }

    /* Set date/time: 2025-01-01 12:00:00 (Wednesday) */
    stcDate.u8Year    = 25U;
    stcDate.u8Month   = 1U;
    stcDate.u8Day     = 1U;
    stcDate.u8Weekday = RTC_WEEKDAY_WEDNESDAY;
    (void)RTC_SetDate(RTC_DATA_FMT_DEC, &stcDate);

    stcTime.u8Hour   = 12U;
    stcTime.u8Minute = 0U;
    stcTime.u8Second = 0U;
    stcTime.u8AmPm   = RTC_HOUR_24H;
    (void)RTC_SetTime(RTC_DATA_FMT_DEC, &stcTime);

    /* Start RTC */
    RTC_Cmd(ENABLE);
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    /* Wait ~2 seconds */
    BSP_DelayMS(2000UL);

    /* Read back */
    (void)RTC_GetDate(RTC_DATA_FMT_DEC, &stcDate);
    (void)RTC_GetTime(RTC_DATA_FMT_DEC, &stcTime);

    BSP_UART_Printf("  RTC: 20%02u-%02u-%02u %02u:%02u:%02u\r\n",
                     stcDate.u8Year, stcDate.u8Month, stcDate.u8Day,
                     stcTime.u8Hour, stcTime.u8Minute, stcTime.u8Second);

    /* Verify seconds advanced */
    if (stcTime.u8Second >= 1U) {
        BSP_UART_Printf("[PASS] RTC\r\n");
    } else {
        BSP_UART_Printf("[FAIL] RTC (time not advancing)\r\n");
    }
}
