/**
 *******************************************************************************
 * @file  test_crypto.c
 * @brief AES, CRC, TRNG test for HC32F460.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_aes.h"
#include "hc32_ll_crc.h"
#include "hc32_ll_trng.h"

void test_crypto(void)
{
    uint8_t pass = 1U;

    BSP_UART_Printf("[TEST] Crypto (AES/CRC/TRNG) ...\r\n");
    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* ---- CRC32 test ---- */
    {
        stc_crc_init_t stcCrcInit;
        uint32_t testData[4] = { 0x12345678UL, 0x9ABCDEF0UL, 0x11223344UL, 0x55667788UL };
        uint32_t crc1 = 0UL, crc2 = 0UL;

        FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_CRC, ENABLE);

        (void)CRC_StructInit(&stcCrcInit);
        (void)CRC_Init(&stcCrcInit);

        (void)CRC_CRC32_Calculate(0xFFFFFFFFUL, CRC_DATA_WIDTH_32BIT,
                                  testData, 4UL, &crc1);
        (void)CRC_CRC32_Calculate(0xFFFFFFFFUL, CRC_DATA_WIDTH_32BIT,
                                  testData, 4UL, &crc2);

        if (crc1 == crc2 && crc1 != 0UL) {
            BSP_UART_Printf("  CRC32: 0x%08lX (deterministic OK)\r\n", (unsigned long)crc1);
        } else {
            BSP_UART_Printf("  CRC32: FAIL (non-deterministic)\r\n");
            pass = 0U;
        }

        (void)CRC_DeInit();
    }

    /* ---- TRNG test ---- */
    {
        uint32_t rng1 = 0UL, rng2 = 0UL;
        int32_t ret;

        FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_TRNG, ENABLE);
        TRNG_Init(TRNG_SHIFT_CNT64, TRNG_RELOAD_INIT_VAL_ENABLE);

        ret = TRNG_GenerateRandom(&rng1, 1UL);
        if (LL_OK == ret) {
            (void)TRNG_GenerateRandom(&rng2, 1UL);
            BSP_UART_Printf("  TRNG: 0x%08lX, 0x%08lX", (unsigned long)rng1, (unsigned long)rng2);
            if (rng1 != rng2) {
                BSP_UART_Printf(" (random OK)\r\n");
            } else {
                BSP_UART_Printf(" (WARNING: same values)\r\n");
            }
        } else {
            BSP_UART_Printf("  TRNG: generate failed\r\n");
            pass = 0U;
        }
    }

    /* ---- AES-128 encrypt/decrypt test ---- */
    {
        FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AES, ENABLE);

        static const uint8_t plaintext[16] = "Hello HC32F460!";
        static const uint8_t key[16] = "0123456789ABCDEF";
        uint8_t cipher[16] = {0};
        uint8_t decrypted[16] = {0};
        int32_t ret;
        uint8_t i;

        ret = AES_Encrypt(plaintext, 16UL, key, AES_KEY_SIZE_16BYTE, cipher);
        if (LL_OK == ret) {
            ret = AES_Decrypt(cipher, 16UL, key, AES_KEY_SIZE_16BYTE, decrypted);
            if (LL_OK == ret) {
                uint8_t match = 1U;
                for (i = 0U; i < 16U; i++) {
                    if (decrypted[i] != plaintext[i]) {
                        match = 0U;
                        break;
                    }
                }
                if (match) {
                    BSP_UART_Printf("  AES-128: encrypt/decrypt roundtrip OK\r\n");
                } else {
                    BSP_UART_Printf("  AES-128: decrypt mismatch\r\n");
                    pass = 0U;
                }
            } else {
                BSP_UART_Printf("  AES decrypt failed\r\n");
                pass = 0U;
            }
        } else {
            BSP_UART_Printf("  AES encrypt failed\r\n");
            pass = 0U;
        }
    }

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    if (pass) {
        BSP_UART_Printf("[PASS] Crypto\r\n");
    } else {
        BSP_UART_Printf("[FAIL] Crypto\r\n");
    }
}
