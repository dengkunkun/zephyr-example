/**
 *******************************************************************************
 * @file  test_i2c.c
 * @brief I2C1 EEPROM (AT24C64D) read/write test for UYUP board.
 *        I2C1: PB8(SCL) / PB9(SDA), device addr 0x50, 2-byte address.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_i2c.h"

static int32_t EEPROM_Write(uint16_t u16Addr, const uint8_t *pu8Buf, uint32_t u32Len)
{
    int32_t i32Ret;
    uint8_t addrBuf[2];

    addrBuf[0] = (uint8_t)(u16Addr >> 8U);
    addrBuf[1] = (uint8_t)(u16Addr & 0xFFU);

    i32Ret = I2C_Start(BSP_I2C_UNIT, BSP_I2C_TIMEOUT);
    if (LL_OK == i32Ret) {
        i32Ret = I2C_TransAddr(BSP_I2C_UNIT, BSP_EEPROM_ADDR, I2C_DIR_TX, BSP_I2C_TIMEOUT);
    }
    if (LL_OK == i32Ret) {
        i32Ret = I2C_TransData(BSP_I2C_UNIT, addrBuf, BSP_EEPROM_ADDR_LEN, BSP_I2C_TIMEOUT);
    }
    if (LL_OK == i32Ret) {
        i32Ret = I2C_TransData(BSP_I2C_UNIT, pu8Buf, u32Len, BSP_I2C_TIMEOUT);
    }
    (void)I2C_Stop(BSP_I2C_UNIT, BSP_I2C_TIMEOUT);

    /* Wait for EEPROM internal write cycle (~5ms) */
    BSP_DelayMS(10UL);
    return i32Ret;
}

static int32_t EEPROM_Read(uint16_t u16Addr, uint8_t *pu8Buf, uint32_t u32Len)
{
    int32_t i32Ret;
    uint8_t addrBuf[2];

    addrBuf[0] = (uint8_t)(u16Addr >> 8U);
    addrBuf[1] = (uint8_t)(u16Addr & 0xFFU);

    i32Ret = I2C_Start(BSP_I2C_UNIT, BSP_I2C_TIMEOUT);
    if (LL_OK == i32Ret) {
        i32Ret = I2C_TransAddr(BSP_I2C_UNIT, BSP_EEPROM_ADDR, I2C_DIR_TX, BSP_I2C_TIMEOUT);
    }
    if (LL_OK == i32Ret) {
        i32Ret = I2C_TransData(BSP_I2C_UNIT, addrBuf, BSP_EEPROM_ADDR_LEN, BSP_I2C_TIMEOUT);
    }
    if (LL_OK == i32Ret) {
        i32Ret = I2C_Restart(BSP_I2C_UNIT, BSP_I2C_TIMEOUT);
    }
    if (LL_OK == i32Ret) {
        if (1UL == u32Len) {
            I2C_AckConfig(BSP_I2C_UNIT, I2C_NACK);
        }
        i32Ret = I2C_TransAddr(BSP_I2C_UNIT, BSP_EEPROM_ADDR, I2C_DIR_RX, BSP_I2C_TIMEOUT);
    }
    if (LL_OK == i32Ret) {
        i32Ret = I2C_MasterReceiveDataAndStop(BSP_I2C_UNIT, pu8Buf, u32Len, BSP_I2C_TIMEOUT);
    }
    if (LL_OK != i32Ret) {
        (void)I2C_Stop(BSP_I2C_UNIT, BSP_I2C_TIMEOUT);
    }
    I2C_AckConfig(BSP_I2C_UNIT, I2C_ACK);

    return i32Ret;
}

/* Toggle SCL manually to recover a stuck I2C bus */
static void I2C_BusRecover(void)
{
    uint8_t i;
    /* Temporarily set SCL as GPIO output */
    GPIO_SetFunc(BSP_I2C_SCL_PORT, BSP_I2C_SCL_PIN, GPIO_FUNC_0);
    GPIO_OutputCmd(BSP_I2C_SCL_PORT, BSP_I2C_SCL_PIN, ENABLE);
    for (i = 0U; i < 9U; i++) {
        GPIO_ResetPins(BSP_I2C_SCL_PORT, BSP_I2C_SCL_PIN);
        DDL_DelayUS(5U);
        GPIO_SetPins(BSP_I2C_SCL_PORT, BSP_I2C_SCL_PIN);
        DDL_DelayUS(5U);
    }
    /* Restore alternate function */
    GPIO_SetFunc(BSP_I2C_SCL_PORT, BSP_I2C_SCL_PIN, BSP_I2C_SCL_FUNC);
}

void test_i2c_eeprom(void)
{
    stc_i2c_init_t stcI2cInit;
    float32_t fErr;
    int32_t i32Ret;
    uint8_t wrBuf[8] = { 0xAA, 0x55, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };
    uint8_t rdBuf[8] = {0};
    uint32_t I2cSrcClk, I2cClkDiv, I2cClkDivReg;
    uint8_t i, pass = 1U;

    BSP_UART_Printf("[TEST] I2C EEPROM (AT24C64D @ 0x%02X) ...\r\n", BSP_EEPROM_ADDR);
    BSP_WDT_Feed();

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* I2C1 GPIO — open-drain with internal pull-up */
    stc_gpio_init_t stcGpio;
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinDir = PIN_DIR_OUT;
    stcGpio.u16PinOutputType = PIN_OUT_TYPE_NMOS;
    stcGpio.u16PullUp = PIN_PU_ON;
    (void)GPIO_Init(BSP_I2C_SCL_PORT, BSP_I2C_SCL_PIN, &stcGpio);
    (void)GPIO_Init(BSP_I2C_SDA_PORT, BSP_I2C_SDA_PIN, &stcGpio);
    GPIO_SetFunc(BSP_I2C_SCL_PORT, BSP_I2C_SCL_PIN, BSP_I2C_SCL_FUNC);
    GPIO_SetFunc(BSP_I2C_SDA_PORT, BSP_I2C_SDA_PIN, BSP_I2C_SDA_FUNC);

    /* I2C1 clock */
    FCG_Fcg1PeriphClockCmd(BSP_I2C_FCG, ENABLE);

    /* Bus recovery before init */
    I2C_BusRecover();
    BSP_WDT_Feed();

    /* I2C1 init */
    I2cSrcClk = I2C_SRC_CLK;
    I2cClkDiv = I2cSrcClk / BSP_I2C_BAUDRATE / I2C_WIDTH_MAX_IMME;
    for (I2cClkDivReg = I2C_CLK_DIV1; I2cClkDivReg <= I2C_CLK_DIV128; I2cClkDivReg++) {
        if (I2cClkDiv < (1UL << I2cClkDivReg)) {
            break;
        }
    }

    (void)I2C_DeInit(BSP_I2C_UNIT);
    (void)I2C_StructInit(&stcI2cInit);
    stcI2cInit.u32Baudrate = BSP_I2C_BAUDRATE;
    stcI2cInit.u32SclTime  = (uint32_t)((uint64_t)250UL *
        ((uint64_t)I2cSrcClk / ((uint64_t)1UL << I2cClkDivReg)) / (uint64_t)1000000000UL);
    stcI2cInit.u32ClockDiv = I2cClkDivReg;
    i32Ret = I2C_Init(BSP_I2C_UNIT, &stcI2cInit, &fErr);
    if (LL_OK != i32Ret) {
        BSP_UART_Printf("  I2C init failed (%ld)\r\n", (long)i32Ret);
        BSP_UART_Printf("[FAIL] I2C EEPROM\r\n");
        LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
        return;
    }

    BSP_UART_Printf("  I2C init OK (clkdiv=%lu, err=%.1f%%)\r\n",
                     (unsigned long)I2cClkDivReg, (double)fErr);

    I2C_Cmd(BSP_I2C_UNIT, ENABLE);
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    BSP_WDT_Feed();

    /* Write 8 bytes at address 0x0100 */
    i32Ret = EEPROM_Write(0x0100U, wrBuf, 8UL);
    BSP_WDT_Feed();
    if (LL_OK != i32Ret) {
        BSP_UART_Printf("  EEPROM write failed (%ld)\r\n", (long)i32Ret);
        pass = 0U;
    }

    /* Read back */
    if (pass) {
        i32Ret = EEPROM_Read(0x0100U, rdBuf, 8UL);
        BSP_WDT_Feed();
        if (LL_OK != i32Ret) {
            BSP_UART_Printf("  EEPROM read failed (%ld)\r\n", (long)i32Ret);
            pass = 0U;
        }
    }

    /* Verify */
    if (pass) {
        for (i = 0U; i < 8U; i++) {
            if (rdBuf[i] != wrBuf[i]) {
                BSP_UART_Printf("  Mismatch at [%u]: wr=0x%02X rd=0x%02X\r\n",
                                i, wrBuf[i], rdBuf[i]);
                pass = 0U;
                break;
            }
        }
    }

    if (pass) {
        BSP_UART_Printf("  EEPROM write/read 8 bytes OK\r\n");
        BSP_UART_Printf("[PASS] I2C EEPROM\r\n");
    } else {
        BSP_UART_Printf("[FAIL] I2C EEPROM\r\n");
    }
}
