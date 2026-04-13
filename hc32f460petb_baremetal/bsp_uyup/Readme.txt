# HC32F460PETB UYUP Board — BSP 支持包

## 芯片与开发板

- **MCU**: 小华 HC32F460PETB (ARM Cortex-M4, 200MHz, 512KB Flash, 188KB RAM)
- **开发板**: UYUP-RPI-A-2.3
- **DDL SDK**: v3.3.0
- **电路图**: UYUP-RPI-A-2.3.pdf (多芯片共用，注意区分 HC32F460 与其它芯片变体)

## 参考资料

- 参考手册: HC32F460_DDL_Rev3.3.0/RM_HC32F460_F45x_A460SeriesReferenceManual_Rev1.5.pdf
- 数据手册: HC32F460_DDL_Rev3.3.0/DS_HC32F460SeriesDatasheet_Rev1.61.pdf
- DDL 文档: HC32F460_DDL_Rev3.3.0/documents/hc32f460_ddl_Rev3.3.0.chm
- 官方评估板 BSP: HC32F460_DDL_Rev3.3.0/drivers/bsp
- 官方驱动包: HC32F460_DDL_Rev3.3.0/drivers
- 官方示例: HC32F460_DDL_Rev3.3.0/projects/ev_hc32f460_lqfp100_v2/examples

## 硬件引脚连接

详见 docs/pin_map.md。关键连接：

| 外设 | 引脚 | 备注 |
|------|------|------|
| LED3 (Active-Low) | PD10 | VDD→1K→LED→PD10 |
| LED4 (Active-Low) | PE15 | VDD→1K→LED→PE15 |
| WS2812B (2颗) | PB1 (T3C4) | 链式: LED5→LED6 |
| BTN0 / BTN1 | PA3 / PE2 | Active-Low, 内部上拉 |
| USART1 (VCP 调试) | PA9(TX)/PA10(RX) | DAPLink 桥接串口 |
| USART3 (RS485) | PD8(TX)/PD9(RX)/PA0(DE) | SP3485EN, 需外部从机 |
| I2C3 (EEPROM) | PB8(SCL)/PB9(SDA) | AT24C64D @ 0x50 |
| SPI3 (Flash) | PB3(SCK)/PB4(MISO)/PB5(MOSI)/PE3(CS) | W25Q32 (标准SPI) |
| CAN1 | PE5(TX)/PE6(RX) | 需外部CAN设备 |
| SDIO (SD卡) | PC8-PC12/PD2/PE14(DET) | 4-bit SDIO |
| USB | PA11(D-)/PA12(D+)/PB15(EN) | USB 设备模式 |
| Timer4 PWM | PD12-PD15 (CH1-CH4) | PWM 输出 |
| XTAL 24MHz | PH0/PH1 | 外部高速晶振 |
| XTAL32 32.768kHz | PC14/PC15 | RTC 用低速晶振 |
| SWD | PA13(SWDIO)/PA14(SWCLK) | 调试接口 |

### 重要：GPIO 功能组 (Func_Grp2)

PB3/PB4/PB5/PB8/PB9 属于 **Func_Grp2**（见数据手册 Table 2-1）：
- FUNC_40-43 → **SPI3** (非 SPI1)
- FUNC_48-49 → **I2C3** (非 I2C1)

PB3(JTDO)/PB4(NJTRST) 默认被 JTAG PSPCR 寄存器锁定，必须调用
GPIO_SetDebugPort() 释放后才能用作 SPI。

## 构建 (CMake)

    cd bsp_uyup
    cmake -B build
    cmake --build build -j2

产物: build/bsp_test.elf, build/bsp_test.hex, build/bsp_test.bin

## 烧写与调试

    # 烧写
    pyocd load -t hc32f460xe build/bsp_test.hex

    # 复位
    pyocd reset -t hc32f460xe

    # GDB 调试
    pyocd gdbserver -t hc32f460xe --port 3333 &
    arm-zephyr-eabi-gdb build/bsp_test.elf -ex "target remote :3333"

板载调试器: Open Cherry USB CMSIS-DAP (STM32F042F6P6)

## 串口监控

**重要**: DAPLink 桥接芯片有 3x 波特率偏差。MCU 配置 115200，但主机必须使用 38400：

    # Linux
    screen /dev/ttyACM1 38400

    # Windows: COM9, 波特率 38400

## 测试结果

    13 PASS | 0 FAIL | 1 SKIP

    Auto Tests:
      [PASS] GPIO (LED + KEY)
      [PASS] Clock (HCLK=200MHz, PCLK0-4 verified)
      [PASS] Timer (TMR0 500ms compare match)
      [PASS] DMA (16-word mem-to-mem transfer)
      [PASS] ADC/OTS (chip temperature)
      [PASS] RTC (calendar read)
      [PASS] Crypto (AES-128 + CRC32 + TRNG)
      [PASS] WDT (feed + counter read)
      [PASS] Internal Flash (erase/program/verify)

    Board Peripherals:
      [PASS] I2C EEPROM (AT24C64D write/read 8 bytes)
      [PASS] SPI Flash (W25Q32 JEDEC ID + sector erase + write/read)
      [SKIP] SDIO (no SD card inserted)
      [PASS] WS2812B (RGB color sequence on 2 LEDs)

    Ported DDL Examples:
      [PASS] DCU accumulator add
      [PASS] SRAM read/write (SRAMH + Retention SRAM)
      [PASS] RMU reset flags
      [PASS] EFM Unique ID
      [PASS] HASH SHA-256
      [PASS] SysTick DWT cycle count (~23M cycles/100ms @ 200MHz)

    External (disabled, uncomment in main.c to test):
      CAN (needs CAN bus peer)
      RS485 (needs RS485 slave)
      USB (needs USB host connection)

## 项目结构

    bsp_uyup/
    ├── CMakeLists.txt          -- CMake 构建系统
    ├── cmake/
    │   ├── toolchain-arm-none-eabi.cmake
    │   └── HC32F460xE.ld
    ├── drivers/
    │   ├── bsp_uyup.h          -- 所有引脚定义和 API 声明
    │   └── bsp_uyup.c          -- 时钟/LED/按键/UART/WDT 实现
    ├── tests/
    │   ├── test_all.h          -- 测试函数声明
    │   ├── test_gpio.c         -- GPIO (LED+按键)
    │   ├── test_uart.c         -- UART 回显
    │   ├── test_clock.c        -- 时钟验证
    │   ├── test_timer.c        -- 定时器
    │   ├── test_dma.c          -- DMA 传输
    │   ├── test_adc.c          -- ADC/OTS 温度
    │   ├── test_rtc.c          -- RTC 日历
    │   ├── test_crypto.c       -- AES/CRC/TRNG
    │   ├── test_wdt.c          -- 看门狗
    │   ├── test_i2c.c          -- I2C EEPROM
    │   ├── test_spi.c          -- SPI Flash
    │   ├── test_sdio.c         -- SD 卡
    │   ├── test_can.c          -- CAN 回环
    │   ├── test_rs485.c        -- RS485 (需从机)
    │   ├── test_usb.c          -- USB 存根
    │   ├── test_ws2812b.c      -- WS2812B LED
    │   ├── test_flash_internal.c -- 内部 Flash
    │   └── test_examples.c     -- 移植的官方 Examples
    ├── source/
    │   ├── main.c              -- 综合测试 Demo
    │   ├── main.h
    │   └── hc32f4xx_conf.h
    └── docs/
        └── pin_map.md          -- 完整引脚映射文档
