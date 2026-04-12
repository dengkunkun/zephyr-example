/**
 *******************************************************************************
 * @file  tests/test_dma.c
 * @brief DMA test — memory-to-memory transfer for UYUP board.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_dma.h"
#include "hc32_ll_aos.h"

static uint32_t s_au32SrcBuf[16];
static uint32_t s_au32DstBuf[16];

void test_dma(void)
{
    stc_dma_init_t stcInit;
    uint8_t i;
    uint8_t u8Pass = 1U;
    uint32_t u32Timeout;

    BSP_UART_Printf("\r\n===== TEST: DMA (mem-to-mem) =====\r\n");

    /* Fill source, clear destination */
    for (i = 0U; i < 16U; i++) {
        s_au32SrcBuf[i] = 0xDEAD0000UL | (uint32_t)i;
        s_au32DstBuf[i] = 0UL;
    }

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_DMA1, ENABLE);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);

    (void)DMA_StructInit(&stcInit);
    stcInit.u32BlockSize   = 16UL;         /* 16 data units per block */
    stcInit.u32TransCount  = 1UL;          /* 1 block */
    stcInit.u32DataWidth   = DMA_DATAWIDTH_32BIT;
    stcInit.u32SrcAddr     = (uint32_t)(uintptr_t)s_au32SrcBuf;
    stcInit.u32DestAddr    = (uint32_t)(uintptr_t)s_au32DstBuf;
    stcInit.u32SrcAddrInc  = DMA_SRC_ADDR_INC;
    stcInit.u32DestAddrInc = DMA_DEST_ADDR_INC;
    (void)DMA_Init(CM_DMA1, DMA_CH0, &stcInit);

    /* Use AOS software trigger for DMA1 channel 0 */
    AOS_SetTriggerEventSrc(AOS_DMA1_0, EVT_SRC_AOS_STRG);

    DMA_Cmd(CM_DMA1, ENABLE);
    DMA_ChCmd(CM_DMA1, DMA_CH0, ENABLE);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    /* Software trigger */
    AOS_SW_Trigger();

    /* Wait for transfer-complete flag */
    u32Timeout = 100000UL;
    while ((RESET == DMA_GetTransCompleteStatus(CM_DMA1, DMA_FLAG_TC_CH0)) &&
           (u32Timeout > 0UL)) {
        u32Timeout--;
    }
    DMA_ClearTransCompleteStatus(CM_DMA1, DMA_FLAG_TC_CH0 | DMA_FLAG_BTC_CH0);

    /* Verify */
    for (i = 0U; i < 16U; i++) {
        if (s_au32DstBuf[i] != s_au32SrcBuf[i]) {
            u8Pass = 0U;
            BSP_UART_Printf("  FAIL at [%u]: exp=0x%08lX got=0x%08lX\r\n",
                             (unsigned)i,
                             (unsigned long)s_au32SrcBuf[i],
                             (unsigned long)s_au32DstBuf[i]);
            break;
        }
    }

    DMA_ChCmd(CM_DMA1, DMA_CH0, DISABLE);
    DMA_Cmd(CM_DMA1, DISABLE);

    if (0U != u8Pass) {
        BSP_UART_Printf("  16 words transferred OK\r\n");
        BSP_UART_Printf("===== TEST DMA: PASS =====\r\n");
    } else {
        BSP_UART_Printf("===== TEST DMA: FAIL =====\r\n");
    }
}
