/**
 *******************************************************************************
 * @file  tests/test_timer.c
 * @brief Timer test — TMR0 compare-match timing for UYUP board.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_tmr0.h"

void test_timer(void)
{
    stc_tmr0_init_t stcInit;

    BSP_UART_Printf("\r\n===== TEST: TIMER (TMR0) =====\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMR0_1, ENABLE);

    (void)TMR0_StructInit(&stcInit);
    stcInit.u32ClockSrc     = TMR0_CLK_SRC_INTERN_CLK;
    stcInit.u32ClockDiv     = TMR0_CLK_DIV1024;
    stcInit.u32Func         = TMR0_FUNC_CMP;
    /* PCLK1 = 100 MHz, /1024 = 97656 Hz.  Compare at 48828 => ~500 ms */
    stcInit.u16CompareValue = 48828U;
    (void)TMR0_Init(CM_TMR0_1, TMR0_CH_A, &stcInit);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    TMR0_Start(CM_TMR0_1, TMR0_CH_A);

    /* Wait for compare-match flag */
    while (RESET == TMR0_GetStatus(CM_TMR0_1, TMR0_FLAG_CMP_A)) {
        /* spin */
    }
    TMR0_ClearStatus(CM_TMR0_1, TMR0_FLAG_CMP_A);

    BSP_UART_Printf("  TMR0 compare match after ~500 ms\r\n");

    TMR0_Stop(CM_TMR0_1, TMR0_CH_A);
    (void)TMR0_DeInit(CM_TMR0_1);
    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMR0_1, DISABLE);

    BSP_UART_Printf("===== TEST TIMER: PASS =====\r\n");
}
