/**
 *******************************************************************************
 * @file  test_wdt.c
 * @brief Watchdog timer test for HC32F460.
 *        Verifies feed (refresh) works without causing reset.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_wdt.h"
#include "hc32_ll_rmu.h"

void test_wdt(void)
{
    en_flag_status_t rstFlag;

    BSP_UART_Printf("[TEST] WDT ...\r\n");

    /* Check if last reset was caused by WDT */
    rstFlag = RMU_GetStatus(RMU_FLAG_WDT);
    if (SET == rstFlag) {
        BSP_UART_Printf("  Note: last reset was WDT timeout\r\n");
        LL_PERIPH_WE(BSP_LL_PERIPH_SEL);
        RMU_ClearStatus();
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
    }

    /* WDT is configured via ICG (option bytes) on HC32F460.
     * By default (ICG not programmed), WDT is disabled.
     * We just verify we can read the WDT counter and feed it.
     * If WDT is running (ICG enabled it), feed to prevent reset.
     */

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Try to feed WDT — safe even if WDT is not running */
    WDT_FeedDog();
    BSP_UART_Printf("  WDT feed OK\r\n");

    /* Read counter */
    uint16_t cnt = WDT_GetCountValue();
    BSP_UART_Printf("  WDT counter: %u\r\n", cnt);

    /* Feed again — and tell BSP that WDT is running so BSP_DelayMS auto-feeds */
    WDT_FeedDog();
    BSP_WDT_SetRunning();

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    BSP_UART_Printf("[PASS] WDT\r\n");
}
