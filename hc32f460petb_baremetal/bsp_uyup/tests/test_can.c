/**
 *******************************************************************************
 * @file  test_can.c
 * @brief CAN1 loopback self-test for UYUP board.
 *        CAN1: PE5(TX) / PE6(RX). Uses internal loopback mode.
 *******************************************************************************
 */
#include <string.h>
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_can.h"

void test_can(void)
{
    stc_can_init_t stcCanInit;
    stc_can_tx_frame_t stcTx;
    stc_can_rx_frame_t stcRx;
    int32_t i32Ret;
    uint8_t i, pass = 1U;

    BSP_UART_Printf("[TEST] CAN (internal loopback) ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* CAN GPIO */
    GPIO_SetFunc(BSP_CAN_TX_PORT, BSP_CAN_TX_PIN, BSP_CAN_TX_FUNC);
    GPIO_SetFunc(BSP_CAN_RX_PORT, BSP_CAN_RX_PIN, BSP_CAN_RX_FUNC);

    /* CAN clock */
    FCG_Fcg1PeriphClockCmd(BSP_CAN_FCG, ENABLE);

    /* CAN init — internal loopback mode */
    (void)CAN_StructInit(&stcCanInit);
    stcCanInit.stcBitCfg.u32Prescaler = 5UL;       /* 200MHz / 5 = 40MHz CAN clock */
    stcCanInit.stcBitCfg.u32TimeSeg1  = 15UL;      /* Tq: 1+15+4 = 20 → 40MHz/20 = 2Mbps */
    stcCanInit.stcBitCfg.u32TimeSeg2  = 4UL;
    stcCanInit.stcBitCfg.u32SJW       = 4UL;
    stcCanInit.u8WorkMode = CAN_WORK_MD_ILB;        /* Internal loopback */

    i32Ret = CAN_Init(BSP_CAN_UNIT, &stcCanInit);
    if (LL_OK != i32Ret) {
        BSP_UART_Printf("  CAN init failed (%ld)\r\n", (long)i32Ret);
        BSP_UART_Printf("[FAIL] CAN\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    /* Prepare TX frame */
    (void)memset(&stcTx, 0, sizeof(stcTx));
    stcTx.u32ID   = 0x321UL;
    stcTx.IDE     = 0U;        /* Standard frame */
    stcTx.RTR     = 0U;        /* Data frame */
    stcTx.DLC     = 8U;
    for (i = 0U; i < 8U; i++) {
        stcTx.au8Data[i] = 0x10U + i;
    }

    /* Send via PTB */
    i32Ret = CAN_FillTxFrame(BSP_CAN_UNIT, CAN_TX_BUF_PTB, &stcTx);
    if (LL_OK == i32Ret) {
        CAN_StartTx(BSP_CAN_UNIT, CAN_TX_REQ_PTB);
    }

    /* Wait for PTB TX complete */
    uint32_t timeout = 100000UL;
    while (timeout > 0UL) {
        if (SET == CAN_GetStatus(BSP_CAN_UNIT, CAN_FLAG_PTB_TX)) {
            CAN_ClearStatus(BSP_CAN_UNIT, CAN_FLAG_PTB_TX);
            break;
        }
        timeout--;
    }
    if (0UL == timeout) {
        BSP_UART_Printf("  TX timeout\r\n");
        pass = 0U;
    }

    /* Wait for RX */
    if (pass) {
        timeout = 100000UL;
        while (timeout > 0UL) {
            if (SET == CAN_GetStatus(BSP_CAN_UNIT, CAN_FLAG_RX)) {
                CAN_ClearStatus(BSP_CAN_UNIT, CAN_FLAG_RX);
                break;
            }
            timeout--;
        }
        if (0UL == timeout) {
            BSP_UART_Printf("  RX timeout\r\n");
            pass = 0U;
        }
    }

    /* Read RX frame */
    if (pass) {
        (void)memset(&stcRx, 0, sizeof(stcRx));
        i32Ret = CAN_GetRxFrame(BSP_CAN_UNIT, &stcRx);
        if (LL_OK == i32Ret) {
            BSP_UART_Printf("  RX ID=0x%03lX DLC=%u Data:", (unsigned long)stcRx.u32ID, stcRx.DLC);
            for (i = 0U; i < stcRx.DLC; i++) {
                BSP_UART_Printf(" %02X", stcRx.au8Data[i]);
            }
            BSP_UART_Printf("\r\n");

            /* Verify data */
            for (i = 0U; i < 8U; i++) {
                if (stcRx.au8Data[i] != stcTx.au8Data[i]) {
                    pass = 0U;
                    break;
                }
            }
        } else {
            BSP_UART_Printf("  GetRxFrame failed\r\n");
            pass = 0U;
        }
    }

    CAN_DeInit(BSP_CAN_UNIT);

    if (pass) {
        BSP_UART_Printf("[PASS] CAN\r\n");
    } else {
        BSP_UART_Printf("[FAIL] CAN\r\n");
    }
}
