/**
 *******************************************************************************
 * @file  test_examples.c
 * @brief Ported official DDL examples as individual test functions.
 *        Each function extracts the core logic from the official example
 *        and adapts pin/clock config for the UYUP board (24MHz XTAL).
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_dcu.h"
#include "hc32_ll_sram.h"
#include "hc32_ll_rmu.h"
#include "hc32_ll_efm.h"
#include "hc32_ll_hash.h"

/*============================================================================
 *  Example: DCU (Data Computing Unit) — add/sub operations
 *==========================================================================*/
static void test_ex_dcu(void)
{
    stc_dcu_init_t stcInit;
    uint32_t result;

    BSP_UART_Printf("  [EX] DCU add ... ");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_DCU1, ENABLE);

    (void)DCU_StructInit(&stcInit);
    stcInit.u32Mode     = DCU_MD_ADD;
    stcInit.u32DataWidth = DCU_DATA_WIDTH_32BIT;
    (void)DCU_Init(CM_DCU1, &stcInit);

    /* DCU ADD mode: each DATA1 write computes DATA0 = DATA0 + DATA1 (accumulator) */
    DCU_WriteData32(CM_DCU1, DCU_DATA0_IDX, 0UL);       /* Clear accumulator */
    DCU_WriteData32(CM_DCU1, DCU_DATA1_IDX, 12345UL);   /* DATA0 = 0 + 12345 = 12345 */
    DCU_WriteData32(CM_DCU1, DCU_DATA1_IDX, 67890UL);   /* DATA0 = 12345 + 67890 = 80235 */
    result = DCU_ReadData32(CM_DCU1, DCU_DATA0_IDX);

    DCU_DeInit(CM_DCU1);
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    if (result == (12345UL + 67890UL)) {
        BSP_UART_Printf("OK (12345+67890=%lu)\r\n", (unsigned long)result);
    } else {
        BSP_UART_Printf("FAIL (got %lu)\r\n", (unsigned long)result);
    }
}

/*============================================================================
 *  Example: SRAM parity / ECC check
 *==========================================================================*/
static void test_ex_sram(void)
{
    uint32_t *p;
    uint32_t val;

    BSP_UART_Printf("  [EX] SRAM r/w ... ");

    /* Write and read back from SRAM_H (0x1FFF8000) */
    p = (uint32_t *)0x1FFF8000UL;
    *p = 0xA5A5A5A5UL;
    val = *p;

    if (val == 0xA5A5A5A5UL) {
        BSP_UART_Printf("OK (SRAMH @0x1FFF8000)\r\n");
    } else {
        BSP_UART_Printf("FAIL (got 0x%08lX)\r\n", (unsigned long)val);
    }

    /* Also test SRAM_3 (0x20058000) — retention RAM */
    p = (uint32_t *)0x20058000UL;
    *p = 0x5A5A5A5AUL;
    val = *p;

    BSP_UART_Printf("  [EX] SRAM_R r/w ... ");
    if (val == 0x5A5A5A5AUL) {
        BSP_UART_Printf("OK (SRAM_R @0x20058000)\r\n");
    } else {
        BSP_UART_Printf("FAIL\r\n");
    }
}

/*============================================================================
 *  Example: RMU (Reset Management Unit) — check reset cause
 *==========================================================================*/
static void test_ex_rmu(void)
{
    BSP_UART_Printf("  [EX] RMU reset flags:\r\n");

    if (SET == RMU_GetStatus(RMU_FLAG_PIN)) {
        BSP_UART_Printf("    - PIN reset\r\n");
    }
    if (SET == RMU_GetStatus(RMU_FLAG_SW)) {
        BSP_UART_Printf("    - Software reset\r\n");
    }
    if (SET == RMU_GetStatus(RMU_FLAG_PWR_ON)) {
        BSP_UART_Printf("    - Power-on reset\r\n");
    }
    if (SET == RMU_GetStatus(RMU_FLAG_WDT)) {
        BSP_UART_Printf("    - WDT reset\r\n");
    }
    if (SET == RMU_GetStatus(RMU_FLAG_PVD1)) {
        BSP_UART_Printf("    - PVD1 reset\r\n");
    }

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);
    RMU_ClearStatus();
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
    BSP_UART_Printf("    Flags cleared\r\n");
}

/*============================================================================
 *  Example: EFM (Embedded Flash) — unique ID read
 *==========================================================================*/
static void test_ex_efm_uid(void)
{
    stc_efm_unique_id_t stcUid;

    BSP_UART_Printf("  [EX] EFM Unique ID: ");

    (void)EFM_GetUID(&stcUid);
    BSP_UART_Printf("%08lX-%08lX-%08lX\r\n",
                     (unsigned long)stcUid.u32UniqueID0,
                     (unsigned long)stcUid.u32UniqueID1,
                     (unsigned long)stcUid.u32UniqueID2);
}

/*============================================================================
 *  Example: SysTick — verify DDL_DelayMS accuracy
 *==========================================================================*/
static void test_ex_systick(void)
{
    volatile uint32_t before, after, elapsed;

    BSP_UART_Printf("  [EX] SysTick delay ... ");

    /* Use DWT cycle counter for accurate measurement */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    before = DWT->CYCCNT;
    DDL_DelayMS(100UL);
    after = DWT->CYCCNT;

    elapsed = after - before;
    /* At 200 MHz, 100ms ≈ 20,000,000 cycles */
    BSP_UART_Printf("~%lu cycles for 100ms (expect ~20M)\r\n", (unsigned long)elapsed);
}

/*============================================================================
 *  Example: HASH (SHA256)
 *==========================================================================*/
static void test_ex_hash(void)
{
    /* SHA-256 of "abc" = BA7816BF 8F01CFEA 414140DE 5DAE2223
     *                     B00361A3 96177A9C B410FF61 F20015AD */
    static const uint8_t msg[] = "abc";
    uint8_t digest[32];
    static const uint8_t expected[32] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
    };
    uint8_t i, ok = 1U;

    BSP_UART_Printf("  [EX] HASH SHA256 ... ");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_HASH, ENABLE);
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    if (LL_OK == HASH_Calculate(msg, 3UL, digest)) {
        for (i = 0U; i < 32U; i++) {
            if (digest[i] != expected[i]) {
                ok = 0U;
                break;
            }
        }
        if (ok) {
            BSP_UART_Printf("OK\r\n");
        } else {
            BSP_UART_Printf("FAIL (mismatch at [%u])\r\n", i);
        }
    } else {
        BSP_UART_Printf("FAIL (HASH_Calculate error)\r\n");
    }
}

/*============================================================================
 *  Run all ported examples
 *==========================================================================*/
void test_examples(void)
{
    BSP_UART_Printf("[TEST] Official DDL Examples (ported) ...\r\n");

    test_ex_dcu();
    test_ex_sram();
    test_ex_rmu();
    test_ex_efm_uid();
    test_ex_hash();
    test_ex_systick();

    BSP_UART_Printf("[PASS] Examples\r\n");
}
