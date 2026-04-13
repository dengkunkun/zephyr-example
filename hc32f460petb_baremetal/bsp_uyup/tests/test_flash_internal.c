/**
 *******************************************************************************
 * @file  test_flash_internal.c
 * @brief Internal EFM (Embedded Flash Memory) read/write test.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_efm.h"

/* Use a safe test address in upper flash (far from code region) */
#define EFM_TEST_ADDR   (0x0007F000UL)   /* Near end of 512KB flash */

void test_flash_internal(void)
{
    uint32_t origVal, testVal;
    int32_t i32Ret;

    BSP_UART_Printf("[TEST] Internal Flash (EFM) ...\r\n");

    /* Read original value */
    origVal = *(volatile uint32_t *)EFM_TEST_ADDR;
    BSP_UART_Printf("  Addr 0x%08lX: 0x%08lX\r\n",
                     (unsigned long)EFM_TEST_ADDR, (unsigned long)origVal);

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Unlock EFM */
    EFM_FWMC_Cmd(ENABLE);
    EFM_Cmd(EFM_CHIP_ALL, ENABLE);

    /* Erase the sector */
    i32Ret = EFM_SectorErase(EFM_TEST_ADDR);
    if (LL_OK != i32Ret) {
        BSP_UART_Printf("  Erase failed (%ld)\r\n", (long)i32Ret);
        BSP_UART_Printf("[FAIL] Internal Flash\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }

    /* Verify erased (should be 0xFFFFFFFF) */
    testVal = *(volatile uint32_t *)EFM_TEST_ADDR;
    if (testVal != 0xFFFFFFFFUL) {
        BSP_UART_Printf("  Erase verify fail: 0x%08lX\r\n", (unsigned long)testVal);
        BSP_UART_Printf("[FAIL] Internal Flash\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }
    BSP_UART_Printf("  Erase OK (0xFFFFFFFF)\r\n");

    /* Program a test pattern */
    i32Ret = EFM_ProgramWord(EFM_TEST_ADDR, 0xDEADBEEFUL);
    if (LL_OK != i32Ret) {
        BSP_UART_Printf("  Program failed (%ld)\r\n", (long)i32Ret);
        BSP_UART_Printf("[FAIL] Internal Flash\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }

    /* Read back */
    testVal = *(volatile uint32_t *)EFM_TEST_ADDR;
    BSP_UART_Printf("  Programmed: 0x%08lX\r\n", (unsigned long)testVal);

    if (testVal == 0xDEADBEEFUL) {
        BSP_UART_Printf("[PASS] Internal Flash\r\n");
    } else {
        BSP_UART_Printf("[FAIL] Internal Flash (readback mismatch)\r\n");
    }

    /* Erase sector to leave it clean */
    (void)EFM_SectorErase(EFM_TEST_ADDR);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
}
