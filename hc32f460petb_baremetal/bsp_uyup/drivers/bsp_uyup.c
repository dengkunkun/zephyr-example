/**
 *******************************************************************************
 * @file  bsp_uyup.c
 * @brief BSP implementation for UYUP-RPI-A-2.3 board (HC32F460PETB).
 *        Clock init (24MHz XTAL → 200MHz MPLL), LED, key, UART1 VCP.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "hc32_ll_wdt.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static volatile uint8_t s_wdtRunning = 0U;

/*******************************************************************************
 * Local variables
 ******************************************************************************/
static const struct {
    uint8_t  port;
    uint16_t pin;
} m_astLed[BSP_LED_NUM] = {
    { BSP_LED3_PORT, BSP_LED3_PIN },
    { BSP_LED4_PORT, BSP_LED4_PIN },
};

/*******************************************************************************
 * BSP_CLK_Init — 24 MHz XTAL → 200 MHz MPLL
 ******************************************************************************/
void BSP_CLK_Init(void)
{
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t  stcMpllInit;

    /* XTAL pins → analog */
    GPIO_AnalogCmd(BSP_XTAL_PORT, BSP_XTAL_PIN, ENABLE);

    (void)CLK_XtalStructInit(&stcXtalInit);
    (void)CLK_PLLStructInit(&stcMpllInit);

    /* Bus clock dividers (same as official BSP) */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL,
                    CLK_HCLK_DIV1  | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 |
                    CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 |
                    CLK_PCLK4_DIV2);

    /* XTAL 24 MHz config */
    stcXtalInit.u8Mode       = CLK_XTAL_MD_OSC;
    stcXtalInit.u8Drv        = CLK_XTAL_DRV_ULOW;
    stcXtalInit.u8State      = CLK_XTAL_ON;
    stcXtalInit.u8StableTime = CLK_XTAL_STB_2MS;
    (void)CLK_XtalInit(&stcXtalInit);

    /* MPLL: 24 / 3 * 50 / 2 = 200 MHz */
    stcMpllInit.PLLCFGR = 0UL;
    stcMpllInit.PLLCFGR_f.PLLM = BSP_MPLL_M - 1UL;
    stcMpllInit.PLLCFGR_f.PLLN = BSP_MPLL_N - 1UL;
    stcMpllInit.PLLCFGR_f.PLLP = BSP_MPLL_P - 1UL;
    stcMpllInit.PLLCFGR_f.PLLQ = BSP_MPLL_Q - 1UL;
    stcMpllInit.PLLCFGR_f.PLLR = BSP_MPLL_R - 1UL;
    stcMpllInit.u8PLLState       = CLK_PLL_ON;
    stcMpllInit.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;
    (void)CLK_PLLInit(&stcMpllInit);

    while (SET != CLK_GetStableStatus(CLK_STB_FLAG_PLL)) {
        ;
    }

    /* SRAM wait cycles for 200 MHz */
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    SRAM_SetWaitCycle((SRAM_SRAM12 | SRAM_SRAM3 | SRAM_SRAMR),
                      SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);

    /* Flash wait cycles for 200 MHz */
    (void)EFM_SetWaitCycle(EFM_WAIT_CYCLE5);
    /* GPIO read wait 3 cycles for 126–200 MHz */
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);

    /* Switch to high-performance mode before using PLL as sysclk */
    (void)PWC_HighSpeedToHighPerformance();

    /* Switch system clock to MPLL */
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);

    /* Enable flash cache */
    EFM_CacheRamReset(ENABLE);
    EFM_CacheRamReset(DISABLE);
    EFM_CacheCmd(ENABLE);
}

/*******************************************************************************
 * BSP_XTAL32_Init — 32.768 kHz for RTC
 ******************************************************************************/
int32_t BSP_XTAL32_Init(void)
{
    stc_clock_xtal32_init_t stcXtal32Init;

    GPIO_AnalogCmd(BSP_XTAL32_PORT, BSP_XTAL32_PIN, ENABLE);

    (void)CLK_Xtal32StructInit(&stcXtal32Init);
    stcXtal32Init.u8State  = CLK_XTAL32_ON;
    stcXtal32Init.u8Drv    = CLK_XTAL32_DRV_MID;
    stcXtal32Init.u8Filter = CLK_XTAL32_FILTER_ALL_MD;
    (void)CLK_Xtal32Init(&stcXtal32Init);

    /* Simple stabilization delay */
    DDL_DelayMS(200UL);
    return LL_OK;
}

/*******************************************************************************
 * BSP_LED_Init
 ******************************************************************************/
void BSP_LED_Init(void)
{
    stc_gpio_init_t stcGpio;
    uint8_t i;

    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinState = PIN_STAT_SET;     /* LED off (active-low) */
    stcGpio.u16PinDir   = PIN_DIR_OUT;

    for (i = 0U; i < BSP_LED_NUM; i++) {
        (void)GPIO_Init(m_astLed[i].port, m_astLed[i].pin, &stcGpio);
    }
}

void BSP_LED_On(uint8_t u8Led)
{
    if (u8Led < BSP_LED_NUM) {
        GPIO_ResetPins(m_astLed[u8Led].port, m_astLed[u8Led].pin);
    }
}

void BSP_LED_Off(uint8_t u8Led)
{
    if (u8Led < BSP_LED_NUM) {
        GPIO_SetPins(m_astLed[u8Led].port, m_astLed[u8Led].pin);
    }
}

void BSP_LED_Toggle(uint8_t u8Led)
{
    if (u8Led < BSP_LED_NUM) {
        GPIO_TogglePins(m_astLed[u8Led].port, m_astLed[u8Led].pin);
    }
}

/*******************************************************************************
 * BSP_KEY_Init — BTN0(PA3), BTN1(PE2) with internal pull-up
 ******************************************************************************/
void BSP_KEY_Init(void)
{
    stc_gpio_init_t stcGpio;

    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinDir = PIN_DIR_IN;
    stcGpio.u16PullUp = PIN_PU_ON;

    (void)GPIO_Init(BSP_BTN0_PORT, BSP_BTN0_PIN, &stcGpio);
    (void)GPIO_Init(BSP_BTN1_PORT, BSP_BTN1_PIN, &stcGpio);
}

uint8_t BSP_KEY_GetStatus(uint32_t u32Key)
{
    uint8_t ret = 0U;

    if ((u32Key & BSP_KEY_BTN0) != 0UL) {
        if (PIN_RESET == GPIO_ReadInputPins(BSP_BTN0_PORT, BSP_BTN0_PIN)) {
            ret = 1U;
        }
    }
    if ((u32Key & BSP_KEY_BTN1) != 0UL) {
        if (PIN_RESET == GPIO_ReadInputPins(BSP_BTN1_PORT, BSP_BTN1_PIN)) {
            ret = 1U;
        }
    }
    return ret;
}

/*******************************************************************************
 * BSP_UART_Init — USART1 on PA9(TX) / PA10(RX), 115200 baud
 ******************************************************************************/
void BSP_UART_Init(void)
{
    stc_usart_uart_init_t stcInit;

    /* GPIO alternate function */
    GPIO_SetFunc(BSP_USART1_TX_PORT, BSP_USART1_TX_PIN, BSP_USART1_TX_FUNC);
    GPIO_SetFunc(BSP_USART1_RX_PORT, BSP_USART1_RX_PIN, BSP_USART1_RX_FUNC);

    /* Enable USART1 clock */
    FCG_Fcg1PeriphClockCmd(BSP_USART1_FCG, ENABLE);

    /* USART1 UART init — follow official example pattern */
    (void)USART_UART_StructInit(&stcInit);
    stcInit.u32ClockDiv      = USART_CLK_DIV64;
    stcInit.u32Baudrate      = BSP_USART1_BAUDRATE;
    stcInit.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
    if (LL_OK != USART_UART_Init(BSP_USART1_UNIT, &stcInit, NULL)) {
        /* Baud rate init failed — blink LED as error indicator */
        for (;;) {
        }
    }

    /* Enable TX & RX */
    USART_FuncCmd(BSP_USART1_UNIT, (USART_TX | USART_RX), ENABLE);
}

/*******************************************************************************
 * UART send/receive helpers
 ******************************************************************************/
void BSP_UART_SendChar(uint8_t ch)
{
    while (RESET == USART_GetStatus(BSP_USART1_UNIT, USART_FLAG_TX_EMPTY)) {
    }
    USART_WriteData(BSP_USART1_UNIT, (uint16_t)ch);
}

void BSP_UART_SendStr(const char *s)
{
    while (*s != '\0') {
        BSP_UART_SendChar((uint8_t)*s);
        s++;
    }
    /* Wait for transmission complete */
    while (RESET == USART_GetStatus(BSP_USART1_UNIT, USART_FLAG_TX_CPLT)) {
    }
}

static char s_printBuf[256];

void BSP_UART_Printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(s_printBuf, sizeof(s_printBuf), fmt, args);
    va_end(args);
    BSP_UART_SendStr(s_printBuf);
}

int32_t BSP_UART_RecvChar(uint8_t *ch)
{
    if (SET == USART_GetStatus(BSP_USART1_UNIT, USART_FLAG_RX_FULL)) {
        *ch = (uint8_t)USART_ReadData(BSP_USART1_UNIT);
        return LL_OK;
    }
    /* Clear overrun if set */
    if (SET == USART_GetStatus(BSP_USART1_UNIT, USART_FLAG_OVERRUN)) {
        USART_ClearStatus(BSP_USART1_UNIT, USART_FLAG_OVERRUN);
    }
    return LL_ERR;
}

/*******************************************************************************
 * Simple delay (SysTick-based at HCLK)
 ******************************************************************************/
void BSP_DelayMS(uint32_t ms)
{
    /* Feed WDT during long delays to prevent reset */
    while (ms > 100UL) {
        DDL_DelayMS(100UL);
        if (s_wdtRunning) { WDT_FeedDog(); }
        ms -= 100UL;
    }
    if (ms > 0UL) {
        DDL_DelayMS(ms);
    }
    if (s_wdtRunning) { WDT_FeedDog(); }
}

void BSP_DelayUS(uint32_t us)
{
    DDL_DelayUS(us);
}

void BSP_WDT_SetRunning(void)
{
    s_wdtRunning = 1U;
}

void BSP_WDT_Feed(void)
{
    if (s_wdtRunning) { WDT_FeedDog(); }
}

/*******************************************************************************
 * BSP_Init — one-call board initialization
 ******************************************************************************/
void BSP_Init(void)
{
    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* Release PB3 (JTDO) and PB4 (NJTRST) from JTAG function.
     * These pins are used for SPI3 (W25Q32 Flash) on this board.
     * SWD (PA13/PA14) remains active for debug. SPFE bits 2,4 → 0x14. */
    GPIO_SetDebugPort(0x14U, DISABLE);

    BSP_CLK_Init();
    BSP_LED_Init();
    BSP_KEY_Init();
    BSP_UART_Init();
    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);
}
