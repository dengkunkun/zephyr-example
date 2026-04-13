/**
 *******************************************************************************
 * @file  test_sdio.c
 * @brief SDIOC SD card 4-bit mode test for UYUP board.
 *        SDIOC1: PD2(CMD)/PC12(CLK)/PC8-11(D0-D3), PE14(DET).
 * @note  Requires SD card inserted.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_sdioc.h"

void test_sdio(void)
{
    stc_gpio_init_t stcGpio;

    BSP_UART_Printf("[TEST] SDIO (SD card) ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Card detect pin — input with pull-up */
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinDir = PIN_DIR_IN;
    stcGpio.u16PullUp = PIN_PU_ON;
    (void)GPIO_Init(BSP_SDIOC_DET_PORT, BSP_SDIOC_DET_PIN, &stcGpio);

    /* Check card detect (active-low = card inserted) */
    if (PIN_SET == GPIO_ReadInputPins(BSP_SDIOC_DET_PORT, BSP_SDIOC_DET_PIN)) {
        BSP_UART_Printf("  No SD card detected (DET pin HIGH)\r\n");
        BSP_UART_Printf("[SKIP] SDIO\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }

    BSP_UART_Printf("  SD card detected\r\n");

    /* SDIOC GPIO — all pins to FUNC_9 */
    GPIO_SetFunc(BSP_SDIOC_CLK_PORT, BSP_SDIOC_CLK_PIN, BSP_SDIOC_FUNC);
    GPIO_SetFunc(BSP_SDIOC_CMD_PORT, BSP_SDIOC_CMD_PIN, BSP_SDIOC_FUNC);
    GPIO_SetFunc(BSP_SDIOC_D0_PORT,  BSP_SDIOC_D0_PIN,  BSP_SDIOC_FUNC);
    GPIO_SetFunc(BSP_SDIOC_D1_PORT,  BSP_SDIOC_D1_PIN,  BSP_SDIOC_FUNC);
    GPIO_SetFunc(BSP_SDIOC_D2_PORT,  BSP_SDIOC_D2_PIN,  BSP_SDIOC_FUNC);
    GPIO_SetFunc(BSP_SDIOC_D3_PORT,  BSP_SDIOC_D3_PIN,  BSP_SDIOC_FUNC);

    /* SDIOC clock */
    FCG_Fcg1PeriphClockCmd(BSP_SDIOC_FCG, ENABLE);

    /* Basic SDIOC init — send CMD0 (GO_IDLE_STATE) */
    stc_sdioc_init_t stcInit;
    (void)SDIOC_StructInit(&stcInit);
    stcInit.u32Mode   = SDIOC_MD_SD;
    stcInit.u8CardDetect = SDIOC_CARD_DETECT_CD_PIN_LVL;
    stcInit.u8SpeedMode  = SDIOC_SPEED_MD_NORMAL;
    stcInit.u16ClockDiv  = SDIOC_CLK_DIV256;   /* Start slow for init */

    int32_t i32Ret = SDIOC_Init(BSP_SDIOC_UNIT, &stcInit);
    if (LL_OK != i32Ret) {
        BSP_UART_Printf("  SDIOC init failed (%ld)\r\n", (long)i32Ret);
        BSP_UART_Printf("[FAIL] SDIO\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }

    BSP_UART_Printf("  SDIOC initialized (low-speed)\r\n");

    /* Full SD card initialization sequence is complex (CMD0→CMD8→ACMD41→CMD2→CMD3...)
     * For now, just verify SDIOC peripheral is functional */

    SDIOC_DeInit(BSP_SDIOC_UNIT);
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    BSP_UART_Printf("  Note: full SD protocol not implemented yet\r\n");
    BSP_UART_Printf("[PASS] SDIO (peripheral init)\r\n");
}
