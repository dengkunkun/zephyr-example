/**
 *******************************************************************************
 * @file  test_spi.c
 * @brief SPI3 W25Q32 Flash read/write test for UYUP board.
 *        SPI3: PB3(SCK)/PB4(MISO)/PB5(MOSI)/PE3(CS), standard SPI mode.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"
#include "hc32_ll_spi.h"

static uint8_t SPI_ReadWriteByte(uint8_t tx)
{
    while (RESET == SPI_GetStatus(BSP_SPI_UNIT, SPI_FLAG_TX_BUF_EMPTY)) {
    }
    SPI_WriteData(BSP_SPI_UNIT, (uint32_t)tx);
    while (RESET == SPI_GetStatus(BSP_SPI_UNIT, SPI_FLAG_RX_BUF_FULL)) {
    }
    return (uint8_t)SPI_ReadData(BSP_SPI_UNIT);
}

static void W25Q_ReadID(uint8_t *mfr, uint8_t *memType, uint8_t *cap)
{
    BSP_SPI_CS_LOW();
    SPI_ReadWriteByte(W25Q_CMD_READ_ID);
    *mfr     = SPI_ReadWriteByte(0xFFU);
    *memType = SPI_ReadWriteByte(0xFFU);
    *cap     = SPI_ReadWriteByte(0xFFU);
    BSP_SPI_CS_HIGH();
}

static void W25Q_WriteEnable(void)
{
    BSP_SPI_CS_LOW();
    SPI_ReadWriteByte(W25Q_CMD_WRITE_EN);
    BSP_SPI_CS_HIGH();
}

static void W25Q_WaitBusy(void)
{
    uint8_t sr;
    BSP_SPI_CS_LOW();
    SPI_ReadWriteByte(W25Q_CMD_READ_SR1);
    do {
        sr = SPI_ReadWriteByte(0xFFU);
    } while (sr & W25Q_SR1_BUSY);
    BSP_SPI_CS_HIGH();
}

static void W25Q_SectorErase(uint32_t addr)
{
    W25Q_WriteEnable();
    BSP_SPI_CS_LOW();
    SPI_ReadWriteByte(W25Q_CMD_SECTOR_ERASE);
    SPI_ReadWriteByte((uint8_t)(addr >> 16U));
    SPI_ReadWriteByte((uint8_t)(addr >> 8U));
    SPI_ReadWriteByte((uint8_t)(addr));
    BSP_SPI_CS_HIGH();
    W25Q_WaitBusy();
}

static void W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    W25Q_WriteEnable();
    BSP_SPI_CS_LOW();
    SPI_ReadWriteByte(W25Q_CMD_PAGE_PROG);
    SPI_ReadWriteByte((uint8_t)(addr >> 16U));
    SPI_ReadWriteByte((uint8_t)(addr >> 8U));
    SPI_ReadWriteByte((uint8_t)(addr));
    for (i = 0U; i < len; i++) {
        SPI_ReadWriteByte(buf[i]);
    }
    BSP_SPI_CS_HIGH();
    W25Q_WaitBusy();
}

static void W25Q_ReadData(uint32_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    BSP_SPI_CS_LOW();
    SPI_ReadWriteByte(W25Q_CMD_READ_DATA);
    SPI_ReadWriteByte((uint8_t)(addr >> 16U));
    SPI_ReadWriteByte((uint8_t)(addr >> 8U));
    SPI_ReadWriteByte((uint8_t)(addr));
    for (i = 0U; i < len; i++) {
        buf[i] = SPI_ReadWriteByte(0xFFU);
    }
    BSP_SPI_CS_HIGH();
}

void test_spi_flash(void)
{
    stc_spi_init_t stcSpiInit;
    stc_gpio_init_t stcGpio;
    uint8_t mfr, memType, cap;
    uint8_t wrBuf[16], rdBuf[16];
    uint8_t i, pass;

    BSP_UART_Printf("[TEST] SPI Flash (W25Q32) ...\r\n");
    BSP_WDT_Feed();

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* CS pin — software GPIO output, default HIGH */
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinState = PIN_STAT_SET;
    stcGpio.u16PinDir   = PIN_DIR_OUT;
    (void)GPIO_Init(BSP_SPI_CS_PORT, BSP_SPI_CS_PIN, &stcGpio);

    /* SPI GPIO: SCK and MOSI as output, MISO as input with pull-up */
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinDir = PIN_DIR_OUT;
    (void)GPIO_Init(BSP_SPI_SCK_PORT,  BSP_SPI_SCK_PIN,  &stcGpio);
    (void)GPIO_Init(BSP_SPI_MOSI_PORT, BSP_SPI_MOSI_PIN, &stcGpio);
    stcGpio.u16PinDir = PIN_DIR_IN;
    stcGpio.u16PullUp = PIN_PU_ON;
    (void)GPIO_Init(BSP_SPI_MISO_PORT, BSP_SPI_MISO_PIN, &stcGpio);
    GPIO_SetFunc(BSP_SPI_SCK_PORT,  BSP_SPI_SCK_PIN,  BSP_SPI_SCK_FUNC);
    GPIO_SetFunc(BSP_SPI_MOSI_PORT, BSP_SPI_MOSI_PIN, BSP_SPI_MOSI_FUNC);
    GPIO_SetFunc(BSP_SPI_MISO_PORT, BSP_SPI_MISO_PIN, BSP_SPI_MISO_FUNC);

    /* Enable SPI3 clock */
    FCG_Fcg1PeriphClockCmd(BSP_SPI_FCG, ENABLE);

    /* SPI3 init — Mode 0, 8-bit, MSB first */
    (void)SPI_StructInit(&stcSpiInit);
    stcSpiInit.u32WireMode          = SPI_4_WIRE;
    stcSpiInit.u32TransMode         = SPI_FULL_DUPLEX;
    stcSpiInit.u32MasterSlave       = SPI_MASTER;
    stcSpiInit.u32Parity            = SPI_PARITY_INVD;
    stcSpiInit.u32SpiMode           = SPI_MD_0;
    stcSpiInit.u32BaudRatePrescaler = SPI_BR_CLK_DIV64;
    stcSpiInit.u32DataBits          = SPI_DATA_SIZE_8BIT;
    stcSpiInit.u32FirstBit          = SPI_FIRST_MSB;
    stcSpiInit.u32FrameLevel        = SPI_1_FRAME;
    (void)SPI_Init(BSP_SPI_UNIT, &stcSpiInit);
    SPI_Cmd(BSP_SPI_UNIT, ENABLE);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    /* Release from deep power-down (0xAB) */
    BSP_SPI_CS_LOW();
    (void)SPI_ReadWriteByte(0xABU);
    BSP_SPI_CS_HIGH();
    DDL_DelayUS(50U);

    /* Read JEDEC ID */
    W25Q_ReadID(&mfr, &memType, &cap);
    BSP_UART_Printf("  JEDEC ID: 0x%02X 0x%02X 0x%02X\r\n", mfr, memType, cap);

    if (mfr == 0x00U || mfr == 0xFFU) {
        BSP_UART_Printf("[FAIL] SPI Flash (no response)\r\n");
        return;
    }

    BSP_WDT_Feed();

    /* Erase sector at 0x010000 */
    BSP_UART_Printf("  Erasing sector 0x010000 ...\r\n");
    W25Q_SectorErase(0x010000UL);

    /* Write 16 bytes */
    for (i = 0U; i < 16U; i++) {
        wrBuf[i] = 0xA0U + i;
    }
    W25Q_PageProgram(0x010000UL, wrBuf, 16U);
    BSP_WDT_Feed();

    /* Read back */
    W25Q_ReadData(0x010000UL, rdBuf, 16U);

    /* Verify */
    pass = 1U;
    for (i = 0U; i < 16U; i++) {
        if (rdBuf[i] != wrBuf[i]) {
            BSP_UART_Printf("  Mismatch [%u]: wr=0x%02X rd=0x%02X\r\n",
                            i, wrBuf[i], rdBuf[i]);
            pass = 0U;
            break;
        }
    }
    if (pass) {
        BSP_UART_Printf("  Write/read 16 bytes OK\r\n");
        BSP_UART_Printf("[PASS] SPI Flash\r\n");
    } else {
        BSP_UART_Printf("[FAIL] SPI Flash\r\n");
    }
}
