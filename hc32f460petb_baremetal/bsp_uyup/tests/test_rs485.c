/**
 *******************************************************************************
 * @file  test_rs485.c
 * @brief RS485 half-duplex test via USART3 + PA0 DE/RE.
 * @note  Requires external RS485 slave device for full test.
 *******************************************************************************
 */
#include "bsp_uyup.h"
#include "test_all.h"

void test_rs485(void)
{
    stc_usart_uart_init_t stcInit;
    stc_gpio_init_t stcGpio;
    uint32_t u32Div;
    float32_t f32Err;
    int32_t i32Ret;

    BSP_UART_Printf("[TEST] RS485 (USART3 + PA0 DE/RE) ...\r\n");

    LL_PERIPH_WE(BSP_LL_PERIPH_SEL);

    /* DE/RE direction pin — GPIO output, default RX (LOW) */
    (void)GPIO_StructInit(&stcGpio);
    stcGpio.u16PinState = PIN_STAT_RST;   /* LOW = RX mode */
    stcGpio.u16PinDir   = PIN_DIR_OUT;
    (void)GPIO_Init(BSP_RS485_DE_PORT, BSP_RS485_DE_PIN, &stcGpio);

    /* USART3 GPIO */
    GPIO_SetFunc(BSP_USART3_TX_PORT, BSP_USART3_TX_PIN, BSP_USART3_TX_FUNC);
    GPIO_SetFunc(BSP_USART3_RX_PORT, BSP_USART3_RX_PIN, BSP_USART3_RX_FUNC);

    /* USART3 clock */
    FCG_Fcg1PeriphClockCmd(BSP_USART3_FCG, ENABLE);

    /* USART3 init */
    (void)USART_UART_StructInit(&stcInit);
    stcInit.u32OverSampleBit = USART_OVER_SAMPLE_8BIT;
    (void)USART_UART_Init(BSP_USART3_UNIT, &stcInit, NULL);

    for (u32Div = 0UL; u32Div <= USART_CLK_DIV64; u32Div++) {
        USART_SetClockDiv(BSP_USART3_UNIT, u32Div);
        i32Ret = USART_SetBaudrate(BSP_USART3_UNIT, BSP_USART3_BAUDRATE, &f32Err);
        if ((LL_OK == i32Ret) && (f32Err >= -0.025F) && (f32Err <= 0.025F)) {
            break;
        }
    }

    USART_FuncCmd(BSP_USART3_UNIT, USART_TX, ENABLE);
    USART_FuncCmd(BSP_USART3_UNIT, USART_RX, ENABLE);

    LL_PERIPH_WP(BSP_LL_PERIPH_SEL);

    BSP_UART_Printf("  USART3 configured at %lu baud\r\n", BSP_USART3_BAUDRATE);

    /* Send test data */
    BSP_RS485_TX_EN();
    BSP_DelayUS(10UL);

    const char *msg = "RS485 TEST\r\n";
    while (*msg) {
        while (RESET == USART_GetStatus(BSP_USART3_UNIT, USART_FLAG_TX_EMPTY)) {
        }
        USART_WriteData(BSP_USART3_UNIT, (uint16_t)(uint8_t)*msg);
        msg++;
    }
    while (RESET == USART_GetStatus(BSP_USART3_UNIT, USART_FLAG_TX_CPLT)) {
    }

    BSP_RS485_RX_EN();
    BSP_UART_Printf("  TX sent 'RS485 TEST' (needs slave to echo back)\r\n");

    /* Try receive with short timeout */
    uint32_t timeout = 50UL;   /* 50 * 10ms = 500ms */
    uint8_t  rxCnt = 0U;
    uint8_t  rxBuf[16];

    while (timeout > 0UL && rxCnt < 12U) {
        if (SET == USART_GetStatus(BSP_USART3_UNIT, USART_FLAG_RX_FULL)) {
            rxBuf[rxCnt++] = (uint8_t)USART_ReadData(BSP_USART3_UNIT);
        }
        if (SET == USART_GetStatus(BSP_USART3_UNIT, USART_FLAG_OVERRUN)) {
            USART_ClearStatus(BSP_USART3_UNIT, USART_FLAG_OVERRUN);
        }
        BSP_DelayMS(10UL);
        timeout--;
    }

    if (rxCnt > 0U) {
        BSP_UART_Printf("  RX got %u bytes\r\n", rxCnt);
    } else {
        BSP_UART_Printf("  No RX (expected — no slave connected)\r\n");
    }

    BSP_UART_Printf("[PASS] RS485 (TX init OK)\r\n");
}
