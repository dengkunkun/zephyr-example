# HC32F460 裸机开发与 Zephyr RTOS 移植技术文档

> HC32F460PETB 开发板从裸机到 Zephyr 的完整移植记录，涵盖裸机编译流程、Zephyr 项目架构搭建、
> 每个驱动的实现细节，以及调试过程中遇到的关键问题和解决方案。
>
> **Phase 3 更新：** USART1 纠正、LL_PERIPH_WE API、中断驱动 UART、input 子系统、Zephyr shell。
> **Phase 4 更新：** DMA 模式 UART (TX TE 边沿触发 + RX k_timer 轮询)。
> 另见 [启动流程对比文档](startup-comparison.md)。

---

## 目录

1. [硬件概述](#1-硬件概述)
2. [裸机开发流程](#2-裸机开发流程)
3. [Zephyr 移植架构概述](#3-zephyr-移植架构概述)
4. [SoC 层实现](#4-soc-层实现)
5. [设备树 (Device Tree) 实现](#5-设备树-device-tree-实现)
6. [Board 层实现](#6-board-层实现)
7. [GPIO 驱动实现](#7-gpio-驱动实现)
8. [UART 驱动实现](#8-uart-驱动实现) *(Phase 3: 多实例 + 中断模式, Phase 4: DMA 模式)*
9. [应用层实现](#9-应用层实现) *(Phase 3: input 子系统 + shell)*
10. [调试过程中的关键问题](#10-调试过程中的关键问题)
11. [构建、烧写与调试](#11-构建烧写与调试)

---

## 1. 硬件概述

### 1.1 MCU 参数

| 参数 | 值 |
|------|-----|
| 型号 | HC32F460PETB (小华半导体/HDSC) |
| 内核 | ARM Cortex-M4F (含 FPU, MPU) |
| Flash | 512 KB @ 0x00000000 |
| SRAM | 192 KB @ 0x1FFF8000 (188KB 通用 + 4KB 保持 RAM) |
| 默认时钟 | MRC 8 MHz (内部中速 RC 振荡器) |
| NVIC | 144 个中断线, 4-bit 优先级 (16 级) |
| 调试接口 | CMSIS-DAP (CherryUSB DAPLink) |

### 1.2 板载资源映射

| 资源 | 引脚 | 说明 |
|------|------|------|
| LED3 | PD10 | 低电平有效 (VDD → 1kΩ → LED → 引脚) |
| LED4 | PE15 | 低电平有效 |
| BTN1 | PE2  | 低电平有效, 内部上拉 |
| USART1 TX | PA9 | DAPLink VCP (U3 STM32F042F6P6) 转发, GPIO_FUNC_32 |
| USART1 RX | PA10 | DAPLink VCP 转发, GPIO_FUNC_33 |

> **USART 更正 (Phase 3):** 原文档错误记载为 USART2 (PA2/PA3)。经 Windows 环境实测验证，
> DAPLink VCP 实际连接的是 **USART1** (PA9/PA10)。PA9=FUNC_32 (USART1_TX)，PA10=FUNC_33 (USART1_RX)。

### 1.3 工具链环境

| 工具 | 版本/路径 |
|------|-----------|
| Zephyr SDK | `/home/firebot/zephyr-sdk-1.0.1` (GCC 14.3.0, arm-zephyr-eabi) |
| Zephyr | 4.4.0-rc2, west 1.5.0 |
| pyOCD | `/home/firebot/zephyrproject/.venv/bin/pyocd`, 目标: `hc32f460xe` |
| DDL 库 | `HC32F460_DDL_Rev3.3.0` (HDSC 官方低层驱动库) |

---

## 2. 裸机开发流程

### 2.1 目标

在 Linux 环境下使用 Zephyr SDK 中的 arm-zephyr-eabi 交叉编译器编译 HC32F460 裸机代码，
验证工具链和烧写/调试流程正常工作，为后续 Zephyr 移植打下基础。

### 2.2 核心问题：picolibc vs newlib-nano

Zephyr SDK 的 GCC 使用 **picolibc** 作为 C 库，而非传统嵌入式开发常用的 newlib-nano。
这导致原始 Makefile 中的 `--specs=nano.specs --specs=nosys.specs` 无法使用。

**问题表现：**
```
arm-zephyr-eabi-gcc: error: unrecognized command-line option '--specs=nano.specs'
```

**解决方案 — Makefile 自动检测工具链：**

```makefile
# 检测是否支持 nano.specs (标准 arm-none-eabi) 还是 picolibc (Zephyr SDK)
HAS_NANO_SPECS := $(shell $(CC) --specs=nano.specs -E -x c /dev/null -o /dev/null 2>/dev/null && echo yes || echo no)

ifeq ($(HAS_NANO_SPECS),yes)
  # 标准 arm-none-eabi-gcc (newlib-nano)
  LDFLAGS := $(MCU_FLAGS) --specs=nano.specs --specs=nosys.specs -T$(LINKER_SCRIPT) ...
else
  # Zephyr SDK (picolibc)
  LDFLAGS := $(MCU_FLAGS) --specs=picolibc.specs -nostartfiles -T$(LINKER_SCRIPT) ...
  NEED_PICOLIBC_STUB := yes
endif
```

### 2.3 picolibc stdout 桩函数

DDL 的 `hc32_ll_utility.c` 引用了 `printf`，而 picolibc 的 `printf` 需要 `stdout` 的
`FILE*` 定义。需要提供一个最小实现：

```c
/* picolibc_stub.c */
#include <stdio.h>

static int _stub_putc(char c, FILE *f)
{
    (void)f;
    (void)c;
    return c;
}

static FILE __stdout = FDEV_SETUP_STREAM(_stub_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &__stdout;
```

### 2.4 裸机编译流程

```bash
# 设置 Zephyr SDK 工具链前缀
export PREFIX=/home/firebot/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-

# 编译
cd hc32f460petb_baremetal/gpio_output/GCC
make -j$(nproc) PREFIX=$PREFIX

# 烧写
make flash PREFIX=$PREFIX

# 调试 (终端 1)
make gdbserver
# 调试 (终端 2)
${PREFIX}gdb -ex "target remote :3333" build/gpio_output.elf
```

### 2.5 裸机 Makefile 结构

完整的 Makefile 支持以下功能：

- **自动工具链检测**：根据 GCC 是否支持 `nano.specs` 自动切换 picolibc / newlib-nano
- **DDL 库编译**：编译系统初始化、时钟、GPIO、USART 等 LL 驱动源文件
- **烧写/调试目标**：`make flash` (pyOCD), `make gdbserver`, `make debug`
- **输出格式**：同时生成 .elf, .hex, .bin, .lst 文件

```makefile
# 核心编译参数
MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mthumb-interwork
CFLAGS := $(MCU_FLAGS) -ffunction-sections -fdata-sections -Og -g3 -std=c11
CPPFLAGS := -DHC32F460 -DUSE_DDL_DRIVER \
    -I../../drivers/cmsis/Device/HDSC/hc32f4xx/Include \
    -I../../drivers/hc32_ll_driver/inc \
    -I../../drivers/cmsis/Include

# DDL 源文件
C_SRCS := ../source/main.c \
    ../../drivers/cmsis/Device/HDSC/hc32f4xx/Source/system_hc32f460.c \
    ../../drivers/hc32_ll_driver/src/hc32_ll.c \
    ../../drivers/hc32_ll_driver/src/hc32_ll_clk.c \
    ../../drivers/hc32_ll_driver/src/hc32_ll_gpio.c \
    ../../drivers/hc32_ll_driver/src/hc32_ll_usart.c \
    # ... 其他 DDL 模块

# 链接使用 DDL 自带的链接脚本
LDFLAGS := ... -Tconfig/linker/HC32F460xE.ld -Wl,--gc-sections
```

### 2.6 裸机编译结果

- ELF 大小：22 KB text
- 格式验证：ARM Cortex-M4 ELF
- pyOCD 烧写：22528 bytes, 3 sectors
- GDB 调试：PC 在 `Reset_Handler` (0x1254)，可读取 Flash/RAM 内存

---

## 3. Zephyr 移植架构概述

### 3.1 Out-of-Tree 项目模式

由于 HC32F460 在 Zephyr 主线代码中没有任何支持，采用 **out-of-tree** (树外) 项目模式，
将 SoC 定义、Board 定义、驱动和 DTS 全部放在项目目录内。

**核心机制：** 在 `CMakeLists.txt` 中设置 `SOC_ROOT`、`BOARD_ROOT`、`DTS_ROOT` 指向
项目目录，然后调用 `find_package(Zephyr)`：

```cmake
cmake_minimum_required(VERSION 3.20.0)

list(APPEND SOC_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hc32f460petb_demo)

target_sources(app PRIVATE src/main.c)
```

### 3.2 项目目录结构

```
hc32f460petb/
├── CMakeLists.txt                  ← 顶层构建入口
├── prj.conf                        ← Kconfig 配置
├── src/main.c                      ← 应用代码
│
├── soc/hdsc/hc32f460/              ← SoC 定义层
│   ├── soc.yml                     ← SoC 元数据
│   ├── soc.c                       ← 早期初始化 (外设解锁)
│   ├── soc.h                       ← SoC 头文件 + CMSIS 兼容
│   ├── Kconfig.soc                 ← SoC 选项 (CPU 特性)
│   ├── Kconfig / Kconfig.defconfig ← 默认配置
│   ├── CMakeLists.txt              ← DDL 库集成 + 链接脚本
│   ├── hc32f4xx_conf.h             ← DDL 模块开关
│   └── drivers/                    ← 驱动层的 Kconfig 引用
│
├── boards/hdsc/hc32f460petb/       ← Board 定义层
│   ├── board.yml                   ← Board 元数据
│   ├── board.cmake                 ← 烧写方式 (pyOCD)
│   ├── hc32f460petb.dts            ← Board 设备树
│   ├── hc32f460petb_defconfig      ← Board 默认配置
│   └── Kconfig.hc32f460petb
│
├── dts/                            ← 设备树层
│   ├── arm/hdsc/hc32f460.dtsi      ← SoC 级设备树
│   └── bindings/                   ← DT Bindings
│       ├── gpio/hdsc,hc32-gpio.yaml
│       ├── serial/hdsc,hc32-usart.yaml
│       └── flash/hdsc,hc32-flash-controller.yaml
│
└── drivers/                        ← 外设驱动层
    ├── gpio/gpio_hc32.c            ← GPIO 驱动 (含 EXTINT 中断)
    ├── serial/uart_hc32.c          ← UART 轮询驱动
    ├── CMakeLists.txt / Kconfig    ← 驱动构建配置
    └── ...
```

### 3.3 移植层次关系

```
┌─────────────────────────────────────┐
│           应用层 (main.c)            │  ← Zephyr 标准 GPIO/UART API
├─────────────────────────────────────┤
│         Zephyr 内核 / 驱动框架       │  ← 设备模型, 中断管理, printk
├─────────────────────────────────────┤
│  GPIO 驱动 (gpio_hc32.c)            │  ← 直接寄存器操作
│  UART 驱动 (uart_hc32.c)            │  ← 直接寄存器操作
├─────────────────────────────────────┤
│  SoC 层 (soc.c)                     │  ← 外设解锁, SystemInit
├─────────────────────────────────────┤
│  DDL 库 (hc32_ll_*.c)               │  ← 系统初始化, 头文件定义
├─────────────────────────────────────┤
│  CMSIS 6 Core (Zephyr 提供)          │  ← core_cm4.h, NVIC, SCB
└─────────────────────────────────────┘
```

---

## 4. SoC 层实现

### 4.1 SoC 元数据 (soc.yml)

告诉 Zephyr 构建系统这个 SoC 的家族归属和兼容设置：

```yaml
family:
  - name: hc32
    series:
      - name: hc32f460
        socs:
          - name: hc32f460
```

### 4.2 Kconfig 配置

**Kconfig.soc** — 定义 SoC 的 CPU 特性：

```kconfig
config SOC_HC32F460
    bool
    select SOC_FAMILY_HC32
    select ARM
    select CPU_CORTEX_M4
    select CPU_CORTEX_M_HAS_DWT     # 调试观察点
    select CPU_HAS_FPU              # 硬件浮点
    select CPU_HAS_ARM_MPU          # 内存保护单元
    select SOC_EARLY_INIT_HOOK      # 启用 soc_early_init_hook()
    select HAS_SWO                  # Serial Wire Output 调试
```

**Kconfig.defconfig** — 默认参数：

```kconfig
if SOC_HC32F460
config NUM_IRQS
    default 144                     # HC32F460 有 144 个中断线

config SYS_CLOCK_HW_CYCLES_PER_SEC
    default 8000000                 # MRC 8 MHz (系统 tick 时钟源)
endif
```

### 4.3 SoC 早期初始化 (soc.c)

这是 Zephyr 移植中最关键的文件之一。HC32F460 的大部分外设寄存器都有 **写保护** 机制，
必须在任何驱动初始化之前解锁。

**Phase 3 改进：** 使用 DDL 的 `LL_PERIPH_WE()` API 替代之前的硬编码寄存器地址。
DDL 的解锁函数底层只是简单的寄存器写入，不依赖 CMSIS Core 的 `SCB` 结构体，
因此可以在 Zephyr 的 CMSIS 6 环境下正常工作。

```c
#include <hc32_ll.h>

void DDL_AssertHandler(void)
{
    __ASSERT_NO_MSG(0);  /* DDL 断言重定向到 Zephyr */
}

void soc_early_init_hook(void)
{
    /* SystemInit: 设置 VTOR, FPU 访问权限, SystemCoreClock */
    SystemInit();

    /*
     * 一次性解锁所有外设写保护寄存器：
     *   LL_PERIPH_GPIO  → GPIO_REG_Unlock()     (PWPR = 0xA501)
     *   LL_PERIPH_FCG   → PWC_FCG0_REG_Unlock() (FCG0PC = 0xA5010001)
     *   LL_PERIPH_PWC   → PWC_REG_Unlock()      (FPRC |= 0xA501/0xA502)
     *   LL_PERIPH_EFM   → EFM_REG_Unlock()      (FAPRT = 0x123, 0x3210)
     *   LL_PERIPH_SRAM  → SRAM_REG_Unlock()     (SRAM WTPR)
     */
    LL_PERIPH_WE(LL_PERIPH_ALL);
}
```

> **与旧版的区别：** 旧版直接硬编码 4 组寄存器地址 (0x40048090, 0x40048010, 0x40053BFC,
> 0x40010404)。新版通过 DDL API 调用，代码可读性更强，且与 DDL 文档保持一致。底层行为
> 完全相同——都是简单的寄存器写入操作。

### 4.4 HC32F460 写保护机制详解

HC32F460 有 4 组独立的写保护系统：

| 保护域 | 寄存器 | 地址 | 解锁方式 | 保护的寄存器 |
|--------|--------|------|----------|-------------|
| GPIO | PWPR | 0x40053BFC | 写 0xA501 | PCR, PFSR, PSPCR, PCCR, PINAER |
| PWC/CLK | FPRC | 0x40048090 | 写 0xA501 | PWC, CLK, RMU 相关寄存器 |
| FCG0 | FCG0PC | 0x40048010 | 写 0xA5010001 | FCG0 (时钟门控) |
| EFM (Flash) | FAPRT | 0x40010404 | 连写 0x123 + 0x3210 | Flash 控制寄存器 |

> **重要特性：** 写保护寄存器被锁定时，对受保护寄存器的写入 **静默丢弃**，不会产生
> 任何错误或异常。必须通过回读验证解锁是否成功。

### 4.5 CMSIS 6 兼容性处理 (soc.h)

HC32F460 的 `hc32f460.h` 包含 `core_cm4.h`。Zephyr 通过 cmsis_6 模块提供 CMSIS 6 版本的
Core 头文件，但 HC32F460 的 IRQ 名称与 CMSIS 6 标准不一致：

```c
/* HC32F460 使用非标准的 IRQ 名称, Zephyr/CMSIS-6 需要标准名称 */
#ifndef SVCall_IRQn
#define SVCall_IRQn             SVC_IRQn
#endif
#ifndef MemoryManagement_IRQn
#define MemoryManagement_IRQn   MemManageFault_IRQn
#endif
```

### 4.6 DDL 库集成 (CMakeLists.txt)

关键配置：**只包含设备头文件目录，排除旧版 CMSIS Core 目录**：

```cmake
# DDL 库路径
set(HC32_DDL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../HC32F460_DDL_Rev3.3.0)

# 只包含设备头文件目录 — 不包含 drivers/cmsis/Include (旧 CMSIS Core)
zephyr_include_directories(
  ${HC32_DDL_ROOT}/drivers/cmsis/Device/HDSC/hc32f4xx/Include   # hc32f460.h 等
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/inc                   # DDL LL 驱动头文件
)

# 编译 DDL 源文件
zephyr_sources(
  ${HC32_DDL_ROOT}/drivers/cmsis/Device/HDSC/hc32f4xx/Source/system_hc32f460.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_clk.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_efm.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_fcg.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_gpio.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_interrupts.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_pwc.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_sram.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_usart.c
  ${HC32_DDL_ROOT}/drivers/hc32_ll_driver/src/hc32_ll_utility.c
)

# 使用 Zephyr 标准 Cortex-M 链接脚本 (不使用 DDL 自带的)
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")
```

### 4.7 DDL 模块配置 (hc32f4xx_conf.h)

DDL 库通过 `hc32f4xx_conf.h` 控制各模块的编译开关。只启用必需的模块以减小代码体积：

```c
/* 启用 (DDL_ON) */
#define LL_ICG_ENABLE          (DDL_ON)   // Initial Configuration
#define LL_CLK_ENABLE          (DDL_ON)   // 时钟
#define LL_EFM_ENABLE          (DDL_ON)   // Flash
#define LL_FCG_ENABLE          (DDL_ON)   // 时钟门控
#define LL_GPIO_ENABLE         (DDL_ON)   // GPIO
#define LL_INTERRUPTS_ENABLE   (DDL_ON)   // 中断
#define LL_PWC_ENABLE          (DDL_ON)   // 电源控制
#define LL_SRAM_ENABLE         (DDL_ON)   // SRAM
#define LL_USART_ENABLE        (DDL_ON)   // USART
#define LL_UTILITY_ENABLE      (DDL_ON)   // 工具函数

/* 禁用 (DDL_OFF) — 未使用的外设 */
#define LL_ADC_ENABLE          (DDL_OFF)
#define LL_DMA_ENABLE          (DDL_OFF)
#define LL_SPI_ENABLE          (DDL_OFF)
#define LL_I2C_ENABLE          (DDL_OFF)
/* ... 其余模块全部 DDL_OFF */
```

---

## 5. 设备树 (Device Tree) 实现

### 5.1 SoC 级设备树 (hc32f460.dtsi)

定义 SoC 的内存布局、CPU、Flash 控制器、GPIO 端口和 USART 外设：

```dts
#include <arm/armv7-m.dtsi>
#include <zephyr/dt-bindings/gpio/gpio.h>
#include <mem.h>

/ {
    compatible = "hdsc,hc32f460";

    cpus {
        cpu0: cpu@0 {
            compatible = "arm,cortex-m4f";
            clock-frequency = <8000000>;  /* MRC 8 MHz */
        };
    };

    sram0: memory@1fff8000 {
        compatible = "mmio-sram";
        reg = <0x1FFF8000 DT_SIZE_K(192)>;
    };

    soc {
        flash_controller: flash-controller@40010400 {
            compatible = "hdsc,hc32-flash-controller";
            reg = <0x40010400 0x400>;

            flash0: flash@0 {
                compatible = "soc-nv-flash";
                reg = <0x00000000 DT_SIZE_K(512)>;
            };
        };

        /* GPIO 端口使用伪地址区分 (见 5.2 节) */
        gpioa: gpio@40053801 {
            compatible = "hdsc,hc32-gpio";
            reg = <0x40053800 0x800>;
            gpio-controller;
            #gpio-cells = <2>;
            port-index = <0>;  /* Port A */
            status = "disabled";
        };
        /* gpiob ~ gpiod 结构相同, port-index 依次为 1~3 */
        gpioe: gpio@40053805 {
            compatible = "hdsc,hc32-gpio";
            reg = <0x40053800 0x800>;
            gpio-controller;
            #gpio-cells = <2>;
            port-index = <4>;  /* Port E */
            status = "disabled";
        };

        usart2: serial@4001d400 {
            compatible = "hdsc,hc32-usart";
            reg = <0x4001D400 0x400>;
            status = "disabled";
        };
    };
};

&nvic {
    arm,num-irq-priority-bits = <4>;  /* 16 优先级等级 */
};
```

### 5.2 GPIO DTS 伪地址问题

**问题：** HC32F460 的所有 GPIO 端口共享同一个寄存器块 (CM_GPIO @ 0x40053800)。DTS
要求每个节点有唯一的 unit-address，但 `gpio@40053800` 不能重复出现。

**解决方案：** 使用伪唯一地址 (`gpio@40053801` ~ `gpio@40053805`)，但 `reg` 属性仍指向
真实地址 `0x40053800`。驱动通过 `port-index` 属性区分不同端口。这会产生 DTS 编译警告
(unit-address 与 reg 不匹配)，但功能正常。

### 5.3 DT Bindings

**GPIO Binding (hdsc,hc32-gpio.yaml):**

```yaml
compatible: "hdsc,hc32-gpio"
include: [gpio-controller.yaml, base.yaml]
properties:
  reg:
    required: true
  port-index:
    type: int
    required: true
    description: "Port index (0=A, 1=B, 2=C, 3=D, 4=E)"
  "#gpio-cells":
    const: 2
gpio-cells:
  - pin
  - flags
```

**USART Binding (hdsc,hc32-usart.yaml):**

```yaml
compatible: "hdsc,hc32-usart"
include: uart-controller.yaml
properties:
  reg:
    required: true
```

---

## 6. Board 层实现

### 6.1 Board 设备树 (hc32f460petb.dts)

在 SoC DTS 基础上添加板级硬件定义：

```dts
/dts-v1/;
#include <arm/hdsc/hc32f460.dtsi>
#include <zephyr/dt-bindings/input/input-event-codes.h>

/ {
    model = "HC32F460PETB custom board";
    compatible = "hdsc,hc32f460petb";

    chosen {
        zephyr,console = &usart2;    /* 串口控制台 */
        zephyr,shell-uart = &usart2;
        zephyr,sram = &sram0;
        zephyr,flash = &flash0;
    };

    leds: leds {
        compatible = "gpio-leds";
        led3: led_3 {
            gpios = <&gpiod 10 GPIO_ACTIVE_LOW>;
            label = "LED3";
        };
        led4: led_4 {
            gpios = <&gpioe 15 GPIO_ACTIVE_LOW>;
            label = "LED4";
        };
    };

    gpio_keys {
        compatible = "gpio-keys";
        btn1: button_1 {
            gpios = <&gpioe 2 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
            label = "BTN1";
            zephyr,code = <INPUT_KEY_0>;
        };
    };

    aliases {
        led0 = &led3;
        led1 = &led4;
        sw0 = &btn1;
    };
};

/* 启用用到的 GPIO 端口和 USART */
&gpioa { status = "okay"; };
&gpiod { status = "okay"; };
&gpioe { status = "okay"; };
&usart2 { current-speed = <115200>; status = "okay"; };
```

### 6.2 Board Kconfig 和 defconfig

**Kconfig.hc32f460petb** — 板级 Kconfig 将 board 和 SoC 关联：
```kconfig
config BOARD_HC32F460PETB
    select SOC_HC32F460
```

**hc32f460petb_defconfig** — Board 默认使能的功能：
```
CONFIG_GPIO=y
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_ARM_MPU=y
CONFIG_HW_STACK_PROTECTION=y
```

### 6.3 Board 元数据 (board.yml)

```yaml
board:
  name: hc32f460petb
  vendor: hdsc
  socs:
    - name: hc32f460
```

### 6.4 烧写配置 (board.cmake)

```cmake
board_runner_args(pyocd "--target=hc32f460xe")
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
```

---

## 7. GPIO 驱动实现

### 7.1 驱动概述

文件：`drivers/gpio/gpio_hc32.c`

实现 Zephyr 标准 GPIO API，包括：
- 引脚配置 (输入/输出/上拉/开漏)
- 端口读写 (PIDR/PODR/POSR/PORR/POTR)
- 外部中断 (EXTINT, 通过 INTC 控制器)

### 7.2 HC32F460 GPIO 寄存器布局

所有 GPIO 端口共享一个寄存器块 **CM_GPIO @ 0x40053800**，布局如下：

#### 端口级数据寄存器 (步进 0x10/端口)

| 寄存器 | 偏移 | 宽度 | 说明 |
|--------|------|------|------|
| PIDR | +0x0000 + port×0x10 | 16-bit | 引脚输入数据 (只读) |
| PODR | +0x0004 + port×0x10 | 16-bit | 输出数据锁存器 |
| POER | +0x0006 + port×0x10 | 16-bit | 输出使能 |
| POSR | +0x0008 + port×0x10 | 16-bit | 写1置位 |
| PORR | +0x000A + port×0x10 | 16-bit | 写1清零 |
| POTR | +0x000C + port×0x10 | 16-bit | 写1取反 |

端口索引：A=0, B=1, C=2, D=3, E=4

#### 引脚级配置寄存器 (PCR + PFSR 交替排列)

| 寄存器 | 偏移 | 宽度 | 说明 |
|--------|------|------|------|
| PCR(port,pin) | +0x0400 + port×0x40 + pin×0x04 | 16-bit | 引脚配置 |
| PFSR(port,pin) | +0x0402 + port×0x40 + pin×0x04 | 16-bit | 功能选择 |

**PCR 位定义 (经 offsetof 验证)：**

| 位 | 名称 | 说明 |
|----|------|------|
| 0 | POUT | 输出数据值 |
| 1 | POUTE | 输出使能 |
| 2 | NOD | N-channel 开漏 |
| [5:4] | DRV | 驱动强度 (00=低, 11=高) |
| 6 | PUU | 上拉使能 |
| 8 | PIN | 引脚输入状态 (只读) |
| 9 | INVE | 输入反相 |
| 12 | INTE | 外部中断使能 (路由到 EXTINT) |

**PFSR 位定义：**

| 位 | 名称 | 说明 |
|----|------|------|
| [5:0] | FSEL | 功能选择 (0=GPIO, 36=USART_TX, 37=USART_RX, ...) |
| 8 | BFE | 子功能使能 (**不要对 USART 引脚设置，会干扰 GPIO 读取**) |

#### 特殊寄存器

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| PWPR | 0x40053BFC | 写保护寄存器, 写 0xA501 解锁 |

> **关键教训：** 这些偏移量是通过编写 C 程序使用 `offsetof()` 从 DDL 的结构体定义中计算
> 得出的，而非从参考手册推测。早期的错误偏移导致了大量调试时间浪费。

### 7.3 基本 GPIO 操作实现

```c
static int gpio_hc32_pin_configure(const struct device *dev,
                                   gpio_pin_t pin, gpio_flags_t flags)
{
    const struct gpio_hc32_config *cfg = dev->config;
    uint16_t pcr = 0;

    /* 设置引脚功能为 GPIO (FSEL=0) */
    sys_write16(0, cfg->base + GPIO_PFSR_OFFSET(cfg->port_idx, pin));

    if (flags & GPIO_OUTPUT) {
        pcr |= PCR_POUTE;                          /* 输出使能 */
        if (flags & GPIO_OUTPUT_INIT_HIGH) pcr |= PCR_POUT;  /* 初始高电平 */
        if (flags & GPIO_OPEN_DRAIN) pcr |= PCR_NOD;         /* 开漏模式 */
    }
    if (flags & GPIO_PULL_UP) pcr |= PCR_PUU;       /* 内部上拉 */

    gpio_hc32_write_pcr(cfg, pin, pcr);
    return 0;
}
```

端口操作利用 HC32F460 的原子寄存器实现高效读写：

```c
/* 置位 — 使用 POSR (写1置位), 无需读-改-写 */
gpio_hc32_write16(cfg, GPIO_POSR_OFFSET(cfg->port_idx), (uint16_t)pins);

/* 清零 — 使用 PORR (写1清零), 无需读-改-写 */
gpio_hc32_write16(cfg, GPIO_PORR_OFFSET(cfg->port_idx), (uint16_t)pins);

/* 取反 — 使用 POTR (写1取反), 无需读-改-写 */
gpio_hc32_write16(cfg, GPIO_POTR_OFFSET(cfg->port_idx), (uint16_t)pins);
```

### 7.4 外部中断 (EXTINT) 实现

HC32F460 的外部中断系统与常见的 STM32 EXTI 不同，使用 **INTC** (中断控制器) 进行
灵活的中断源路由。

#### INTC 架构

```
GPIO 引脚 ──→ EXTINT 通道 (0-15) ──→ INTC SEL[n] ──→ NVIC IRQ ──→ ISR
 (由 PCR.INTE 启用)  (按引脚号共享)     (源→IRQ映射)
```

- **16 个 EXTINT 通道**：按引脚号区分 (不是端口！)。EXTINT2 处理所有端口的 pin2。
- **EIRQCR[n]**：每通道触发配置 (下降沿/上升沿/双边沿/低电平)
- **EIFR / EIFCR**：中断标志和清除寄存器
- **SEL[n]**：将中断源映射到 NVIC IRQ 线 (128 个可编程映射)

#### INTC 寄存器地址

| 寄存器 | 地址 | 说明 |
|--------|------|------|
| EIRQCR[n] | 0x40051010 + n×4 | EXTINT 通道 n 触发配置 |
| EIFR | 0x40051054 | EXTINT 标志 (bit n = 通道 n) |
| EIFCR | 0x40051058 | 写1清除标志 |
| SEL[n] | 0x4005105C + n×4 | NVIC IRQ n 的中断源选择 |

#### EIRQCR 触发类型编码

| EIRQTRG[1:0] | 触发方式 |
|---------------|----------|
| 00 | 下降沿 (Falling edge) |
| 01 | 上升沿 (Rising edge) |
| 10 | 双边沿 (Both edges) |
| 11 | 低电平 (Low level) |

#### 中断配置流程

```c
static int gpio_hc32_pin_interrupt_configure(const struct device *dev,
    gpio_pin_t pin, enum gpio_int_mode mode, enum gpio_int_trig trig)
{
    /* 1. 配置 EIRQCR: 触发类型 + 数字滤波 */
    uint32_t eirqcr = EIRQCR_EFEN | EIRQCR_FCLK_DIV8;  // 使能滤波, PCLK3/8
    switch (trig) {
    case GPIO_INT_TRIG_LOW:  eirqcr |= EIRQCR_TRIG_FALLING; break;
    case GPIO_INT_TRIG_HIGH: eirqcr |= EIRQCR_TRIG_RISING;  break;
    case GPIO_INT_TRIG_BOTH: eirqcr |= EIRQCR_TRIG_BOTH;    break;
    }
    sys_write32(eirqcr, INTC_EIRQCR(pin));

    /* 2. 清除挂起标志 */
    sys_write32(BIT(pin), INTC_EIFCR);

    /* 3. 将 INT_SRC_PORT_EIRQ<pin> 映射到 NVIC IRQ <pin> */
    sys_write32(pin, INTC_SEL(pin));  // INT_SRC_PORT_EIRQn 的值恰好等于 n

    /* 4. 设置 PCR 的 INTE 位，使此引脚的事件路由到 EXTINT */
    pcr = gpio_hc32_read_pcr(cfg, pin);
    pcr |= PCR_INTE;
    gpio_hc32_write_pcr(cfg, pin, pcr);

    /* 5. 使用 Zephyr 动态 IRQ 连接并启用 */
    irq_connect_dynamic(irq_num, 0, gpio_hc32_extint_isr,
                        (const void *)(uintptr_t)pin, 0);
    irq_enable(irq_num);
    return 0;
}
```

#### ISR 实现

```c
static void gpio_hc32_extint_isr(const void *arg)
{
    uint8_t ch = (uint8_t)(uintptr_t)arg;

    /* 清除 EXTINT 标志 */
    sys_write32(BIT(ch), INTC_EIFCR);

    /* 触发 Zephyr GPIO 回调 */
    const struct device *dev = extint_owner[ch];
    if (dev != NULL) {
        struct gpio_hc32_data *data = dev->data;
        gpio_fire_callbacks(&data->callbacks, dev, BIT(ch));
    }
}
```

#### EXTINT 通道所有权管理

由于同一引脚号在不同端口共享同一个 EXTINT 通道，驱动使用全局数组追踪通道所有权：

```c
/* 全局: 每个 EXTINT 通道归哪个 GPIO 设备所有 */
static const struct device *extint_owner[16];

/* 配置中断时检查冲突 */
if (extint_owner[pin] != NULL && extint_owner[pin] != dev) {
    return -EBUSY;  // 其他端口已占用此通道
}
extint_owner[pin] = dev;
```

### 7.5 DT 设备实例化宏

```c
#define GPIO_HC32_INIT(n)                                          \
    static struct gpio_hc32_data gpio_hc32_data_##n;               \
    static const struct gpio_hc32_config gpio_hc32_config_##n = {  \
        .common = { .port_pin_mask =                               \
                GPIO_PORT_PIN_MASK_FROM_DT_INST(n) },              \
        .base = DT_INST_REG_ADDR(n),                               \
        .port_idx = DT_INST_PROP(n, port_index),                   \
    };                                                             \
    DEVICE_DT_INST_DEFINE(n, gpio_hc32_init, NULL,                 \
        &gpio_hc32_data_##n, &gpio_hc32_config_##n,               \
        PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,                   \
        &gpio_hc32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_HC32_INIT)
```

---

## 8. UART 驱动实现

### 8.1 驱动概述

文件：`drivers/serial/uart_hc32.c`，`drivers/serial/Kconfig`

**Phase 3 改进：** 完全重写的 UART 驱动，支持以下特性：
- **多实例支持**：通过 DT 自动实例化 USART1/USART2/USART3/USART4
- **轮询 API**：`poll_in`, `poll_out`, `err_check`
- **中断 API**：`fifo_fill`, `fifo_read`, `irq_tx_enable/disable`, `irq_rx_enable/disable` 等
- **USART1 纠正**：DAPLink VCP 连接的是 PA9/PA10 (USART1)，非 PA2/PA3 (USART2)
- **INTC 中断映射**：每个 USART 实例 4 个中断源通过 INTC SEL 映射到 NVIC IRQ

### 8.2 USART 寄存器布局

以 USART1 (基址 0x4001D000) 为例：

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| SR | +0x00 | 状态寄存器 (TXE, RXNE, TC, ORE, FE, PE) |
| TDR | +0x04 | 发送数据寄存器 (16-bit) |
| RDR | +0x06 | 接收数据寄存器 (16-bit) |
| BRR | +0x08 | 波特率寄存器 |
| CR1 | +0x0C | 控制寄存器 1 (TE, RE, RIE, TXEIE, OVER8, SBS, FBME) |
| CR2 | +0x10 | 控制寄存器 2 |
| CR3 | +0x14 | 控制寄存器 3 |
| PR  | +0x18 | 时钟分频寄存器 (PCLK1 预分频) |

USART 基址映射：
| 实例 | 基址 | FCG1 位 |
|------|------|---------|
| USART1 | 0x4001D000 | PWC_FCG1_USART1 |
| USART2 | 0x4001D400 | PWC_FCG1_USART2 |
| USART3 | 0x40021000 | PWC_FCG1_USART3 |
| USART4 | 0x40021400 | PWC_FCG1_USART4 |

### 8.3 CR1 关键位定义 (DDL 验证)

| 位 | 名称 | 值 | 说明 |
|----|------|-----|------|
| 2 | RE | BIT(2) | 接收使能 |
| 3 | TE | BIT(3) | 发送使能 |
| 5 | RIE | BIT(5) | 接收中断使能 |
| 6 | TCIE | BIT(6) | 发送完成中断使能 |
| 7 | TXEIE | BIT(7) | 发送缓冲区空中断使能 |
| 15 | OVER8 | BIT(15) | 8 倍过采样模式 |
| 29 | FBME | BIT(29) | 分数波特率模式使能 |
| 31 | SBS | BIT(31) | 起始位选择 (下降沿检测) |

> **注意：** RE=BIT(2), TE=BIT(3)，与直觉相反。之前版本的驱动将两者交换了，
> 已通过 DDL 头文件 `hc32f460.h` 中的 `USART_CR1_RE_POS=2, USART_CR1_TE_POS=3` 验证修正。

### 8.4 波特率计算

HC32F460 USART 支持整数和分数两种波特率模式：

**分数模式 (精度更高)：**
```
B = C × (128 + DIV_Fraction) / (8 × (2-OVER8) × (DIV_Integer + 1) × 256)
```

- BRR 寄存器格式：`[14:8] = DIV_Integer`, `[6:0] = DIV_Fraction`
- CR1 的 FBME 位 (bit 29) 必须在分数模式下置位
- PR 寄存器支持 1/4/16/64 预分频

**8 MHz / 115200 的计算结果：**
```
PR = 0 (不分频), OVER8 = 1
DIV_Integer = 7, DIV_Fraction = 108
BRR = (7 << 8) | 108 = 0x076C
实际波特率 = 8000000 × (128+108) / (8 × 8 × 256) = 115234 Hz
误差 = 0.03%  ✓
```

驱动中的 `uart_hc32_set_baud()` 自动遍历 4 个预分频档位，先尝试分数模式（精度高），
回退到整数模式，误差超过 2.5% 则跳过。

### 8.5 GPIO 引脚复用配置

驱动通过 config 结构体保存每个实例的引脚配置，初始化时设置 PFSR 和 PCR：

```c
struct uart_hc32_config {
    uint32_t base;
    uint32_t baud_rate;
    uint8_t  instance;      /* 1=USART1, 2=USART2 */
    uint8_t  tx_port, tx_pin, tx_func;
    uint8_t  rx_port, rx_pin, rx_func;
};
```

引脚映射（通过 DT 基址自动推导）：
| 实例 | TX | RX | TX FUNC | RX FUNC |
|------|----|----|---------|---------|
| USART1 | PA9 | PA10 | 32 | 33 |
| USART2 | PA2 | PA3 | 36 | 37 |

### 8.6 中断模式 (CONFIG_UART_HC32_INTERRUPT)

HC32F460 的 USART 中断通过 INTC 控制器路由到 NVIC：

**每个 USART 实例的 4 个中断源：**
| 中断源 | USART1 ID | USART2 ID | 说明 |
|--------|-----------|-----------|------|
| RI  (接收满) | 279 | 284 | RDR 有数据 |
| EI  (错误) | 278 | 283 | 帧错误/溢出 |
| TI  (发送空) | 280 | 285 | TDR 可写 |
| TCI (发送完成) | 281 | 286 | 移位寄存器也空了 |

**INTC SEL 映射过程：**
```c
static void uart_hc32_irq_setup(const struct device *dev)
{
    /* 分配 4 个连续 NVIC IRQ（从 IRQ 16 开始，避开 GPIO EXTINT 使用的 0-15） */
    static uint8_t next_irq = 16;
    irq_base = next_irq;
    next_irq += 4;

    /* 将中断源映射到 NVIC IRQ */
    sys_write32(INT_SRC_USART1_RI,  INTC_SEL(irq_base));     // SEL[16] = 279
    sys_write32(INT_SRC_USART1_EI,  INTC_SEL(irq_base + 1)); // SEL[17] = 278
    sys_write32(INT_SRC_USART1_TI,  INTC_SEL(irq_base + 2)); // SEL[18] = 280
    sys_write32(INT_SRC_USART1_TCI, INTC_SEL(irq_base + 3)); // SEL[19] = 281

    /* 所有 4 个 IRQ 连接到同一个 ISR，由 ISR 内部查询 SR 判断来源 */
    irq_connect_dynamic(irq_base,     1, uart_hc32_isr, dev, 0);
    irq_connect_dynamic(irq_base + 1, 1, uart_hc32_isr, dev, 0);
    irq_connect_dynamic(irq_base + 2, 1, uart_hc32_isr, dev, 0);
    irq_connect_dynamic(irq_base + 3, 1, uart_hc32_isr, dev, 0);

    irq_enable(irq_base);
    irq_enable(irq_base + 1);
    irq_enable(irq_base + 2);
    irq_enable(irq_base + 3);
}
```

**ISR 实现：** 简单地转发到用户回调（shell 或其他注册的回调函数）：
```c
static void uart_hc32_isr(const void *arg)
{
    const struct device *dev = (const struct device *)arg;
    struct uart_hc32_data *data = dev->data;
    if (data->user_cb) {
        data->user_cb(dev, data->user_data);
    }
}
```

### 8.7 Kconfig 配置

```kconfig
config UART_HC32_INTERRUPT
    bool "HC32 UART interrupt mode"
    depends on UART_HC32
    select SERIAL_SUPPORT_INTERRUPT
    select UART_INTERRUPT_DRIVEN
    select DYNAMIC_INTERRUPTS
    help
      Enable interrupt-driven UART for HC32F460.
      Required by Zephyr shell backend.

config UART_HC32_DMA
    bool "HC32 UART DMA mode"
    depends on UART_HC32
    depends on !UART_HC32_INTERRUPT
    select SERIAL_SUPPORT_ASYNC
    select UART_ASYNC_API
    select DYNAMIC_INTERRUPTS
    help
      Enable DMA-driven UART for HC32F460. Uses DMA1
      for RX (CH0) and DMA2 for TX (CH0), with AOS
      hardware trigger routing. A k_timer polls for
      partial RX data to support interactive shell.
```

### 8.9 DMA 模式 (CONFIG_UART_HC32_DMA)

DMA 模式使用 HC32F460 的两个 DMA 控制器实现零 CPU 干预的收发：

- **TX**: DMA2 CH0, 源=内存(递增), 目的=USART TDR(固定), AOS 触发源=EVT_SRC_USARTx_TI
- **RX**: DMA1 CH0, 源=USART RDR(固定), 目的=内存(递增), AOS 触发源=EVT_SRC_USARTx_RI

#### 8.9.1 关键发现：AOS 边沿触发机制

HC32F460 的 AOS (异步对象信号) 路由外设事件到 DMA 触发。**关键点**：

1. **SWREQ 与 AOS 互斥** — 当 AOS 硬件触发已配置时，软件触发 (SWREQ) 不生效
2. **TXE 事件是边沿触发** — 仅在 TE 从 0→1 时产生 TXE 上升沿
3. **TX 发送流程必须 toggle TE** — 每次发送前禁用 TE，配置好 DMA 后再使能 TE

DDL 官方例程 (`usart_uart_dma`) 证实了这一流程：

```c
/* DDL TX 流程 (简化) */
DMA_SetSrcAddr(unit, ch, (uint32_t)buf);
DMA_SetTransCount(unit, ch, len);
DMA_ChCmd(unit, ch, ENABLE);
USART_FuncCmd(unit, USART_TX, ENABLE);  // TE 0→1 产生 TXE 事件触发 DMA
```

#### 8.9.2 TX DMA 实现

```c
static int uart_hc32_async_tx(const struct device *dev,
                              const uint8_t *buf, size_t len,
                              int32_t timeout)
{
    /* 1. 等待上一次传输完成 (SR.TC=1) */
    while (!(usart_read32(cfg, USART_OFF_SR) & USART_SR_TC)) {}

    /* 2. 禁用 TE — 为后续 TE 使能创造上升沿 */
    uint32_t cr1 = usart_read32(cfg, USART_OFF_CR1);
    usart_write32(cfg, USART_OFF_CR1, cr1 & ~USART_CR1_TE);

    /* 3. 配置 DMA2 CH0: SAR=buf(递增), DAR=TDR(固定), 8位, CNT=len */
    dma_write(DMA2_BASE, DMA_CH_SAR(ch), (uint32_t)buf);
    dma_write(DMA2_BASE, DMA_CH_DAR(ch), cfg->base + USART_OFF_TDR);
    dma_write(DMA2_BASE, DMA_CH_DTCTL(ch), ((uint32_t)len << 16) | 1U);
    dma_write(DMA2_BASE, DMA_CH_CHCTL(ch),
              DMA_CHCTL_SINC_INC | DMA_CHCTL_HSIZE_8 | DMA_CHCTL_IE);

    /* 4. 清除标志, 使能 DMA 通道 */
    dma_write(DMA2_BASE, DMA_INTCLR1_OFF, DMA_TC_FLAG(ch));
    uint32_t chen = dma_read(DMA2_BASE, DMA_CHEN_OFF);
    dma_write(DMA2_BASE, DMA_CHEN_OFF, chen | BIT(ch));

    /* 5. 重新使能 TE → TXE 上升沿 → AOS 触发 DMA 开始传输 */
    usart_write32(cfg, USART_OFF_CR1, cr1 | USART_CR1_TE);

    return 0;
}
```

#### 8.9.3 RX DMA + k_timer 轮询

DMA RX 的 TC (Transfer Complete) 中断仅在**整个缓冲区填满时**触发。
对于交互式 shell，每输入一个字符就需要通知 — 因此使用 `k_timer` 周期性轮询 DMA 进度：

```c
#define RX_POLL_PERIOD_MS  5

static void uart_hc32_rx_timer_handler(struct k_timer *timer)
{
    /* 读取 DMA MONDTCTL 寄存器获取剩余传输计数 */
    uint32_t mondtctl = dma_read(DMA1_BASE, DMA_CH_MONDTCTL(ch));
    size_t remaining = (mondtctl >> 16) & 0xFFFF;
    size_t received = buf_len - remaining;

    if (received > rx_offset) {
        /* 通知上层有新数据 */
        struct uart_event evt = {
            .type = UART_RX_RDY,
            .data.rx.buf = rx_buf,
            .data.rx.len = received - rx_offset,
            .data.rx.offset = rx_offset,
        };
        rx_offset = received;
        uart_hc32_async_notify(dev, &evt);
    }
}
```

**定时器启停时机：**
- `rx_enable()` → `k_timer_start(5ms, 5ms)` 开始周期轮询
- DMA RX TC ISR → `k_timer_stop()` 缓冲区满，停止轮询
- `rx_disable()` → `k_timer_stop()` 手动禁用

#### 8.9.4 DMA 寄存器布局

| 分类 | 偏移 | 说明 |
|------|------|------|
| **全局** | 0x00 EN | DMA 使能 |
| | 0x04 INTSTAT0 | 错误标志 (TRNERR, REQERR) |
| | 0x08 INTSTAT1 | 完成标志 (TC[3:0], BTC[19:16]) |
| | 0x0C INTMASK0 | 错误中断屏蔽 |
| | 0x10 INTMASK1 | 完成中断屏蔽 |
| | 0x1C CHEN | 通道使能 (自动清除) |
| **通道 (步长 0x40)** | +0x00 SAR | 源地址 |
| | +0x04 DAR | 目的地址 |
| | +0x08 DTCTL | [31:16] CNT, [9:0] BLKSIZE |
| | +0x1C CHCTL | SINC, DINC, HSIZE, IE |
| | +0x28 MONDTCTL | 监控: 实时剩余 CNT |

#### 8.9.5 DMA 配置文件 (prj-dma.conf)

```ini
CONFIG_UART_HC32_DMA=y
CONFIG_SHELL=y
CONFIG_SHELL_ASYNC_API=y
CONFIG_SHELL_BACKEND_SERIAL=y
```

构建命令：
```bash
west build -b hc32f460petb . --build-dir build-dma -- -DCONF_FILE=prj-dma.conf
```

### 8.8 初始化流程

```c
static int uart_hc32_init(const struct device *dev)
{
    /* 1. 确保 GPIO 写保护已解锁 */
    sys_write16(0xA501U, 0x40053BFCUL);

    /* 2. 使能外设时钟 (FCG1 中清除对应位 = 使能) */
    if (cfg->instance == 1) CM_PWC->FCG1 &= ~PWC_FCG1_USART1;
    else                    CM_PWC->FCG1 &= ~PWC_FCG1_USART2;

    /* 3. 配置 GPIO 引脚复用 (从 config 结构体读取) */
    gpio_set_func(cfg->tx_port, cfg->tx_pin, cfg->tx_func);
    gpio_set_func(cfg->rx_port, cfg->rx_pin, cfg->rx_func);
    gpio_set_drv_high(cfg->tx_port, cfg->tx_pin);

    /* 4. 配置 USART: 8N1, 8× 过采样, 下降沿起始位检测 */
    usart_write32(cfg, USART_OFF_CR1, USART_CR1_OVER8 | USART_CR1_SBS);
    usart_write32(cfg, USART_OFF_CR2, 0U);
    usart_write32(cfg, USART_OFF_CR3, 0U);

    /* 5. 设置波特率 */
    uart_hc32_set_baud(cfg);

    /* 6. 使能发送和接收 */
    cr1 |= USART_CR1_TE | USART_CR1_RE;
    usart_write32(cfg, USART_OFF_CR1, cr1);

    /* 7. [中断模式] 设置 INTC 映射和 NVIC */
    #ifdef CONFIG_UART_HC32_INTERRUPT
    uart_hc32_irq_setup(dev);
    #endif

    return 0;
}
```

### 8.9 DT 设备实例化

```c
/* 根据基址推导实例号 */
#define HC32_USART_INSTANCE(base) \
    (((base) == 0x4001D000UL) ? 1 : \
     ((base) == 0x4001D400UL) ? 2 : \
     ((base) == 0x40021000UL) ? 3 : 4)

/* 根据实例号推导默认引脚 */
#define HC32_USART_TX_PORT(inst)  (... == 1 ? PORT_A : PORT_A)
#define HC32_USART_TX_PIN(inst)   (... == 1 ? 9 : 2)
#define HC32_USART_TX_FUNC(inst)  (... == 1 ? 32 : 36)

#define UART_HC32_INIT(n)                                          \
    static struct uart_hc32_data uart_hc32_data_##n;               \
    static const struct uart_hc32_config uart_hc32_config_##n = {  \
        .base = DT_INST_REG_ADDR(n),                               \
        .baud_rate = DT_INST_PROP_OR(n, current_speed, 115200),    \
        .instance = HC32_USART_INSTANCE(DT_INST_REG_ADDR(n)),     \
        .tx_port = HC32_USART_TX_PORT(n),                          \
        .tx_pin  = HC32_USART_TX_PIN(n),  ...                      \
    };                                                             \
    DEVICE_DT_INST_DEFINE(n, uart_hc32_init, NULL,                 \
        &uart_hc32_data_##n, &uart_hc32_config_##n,               \
        PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY,                 \
        &uart_hc32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(UART_HC32_INIT)
```

### 8.10 GDB 硬件验证结果

通过 GDB 读取寄存器确认 USART1 配置完全正确：

| 寄存器 | 地址 | 读取值 | 说明 |
|--------|------|--------|------|
| PA9 PFSR | 0x40053E24 | 0x0020 | FUNC_32 (USART1_TX) ✓ |
| PA10 PFSR | 0x40053E28 | 0x0021 | FUNC_33 (USART1_RX) ✓ |
| USART1 CR1 | 0x4001D00C | 0xA00080AC | SBS+FBME+OVER8+TXEIE+RIE+TE+RE ✓ |
| USART1 BRR | 0x4001D008 | 0x07EC | 115234 baud (0.03% err) ✓ |
| USART1 SR | 0x4001D000 | 0xC0 | TXE+TC (正常空闲状态) ✓ |
| FCG1 | 0x4004800C | 0x00000000 | 所有外设时钟已使能 ✓ |
| INTC SEL[16] | 0x40051098 | 279 | USART1_RI → IRQ16 ✓ |
| NVIC ISER0 | 0xE000E100 | 0x000F0004 | IRQ 2,16-19 已使能 ✓ |

---

## 9. 应用层实现

### 9.1 应用代码 (main.c)

**Phase 3 改进：** 使用 Zephyr input 子系统替代手动 GPIO 中断回调，添加 shell 支持。

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/input/input.h>

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

static volatile uint32_t btn_press_count;

/* Zephyr input 子系统回调 — 替代手动 gpio_callback */
static void btn_input_cb(struct input_event *evt, void *user_data)
{
    if (evt->type == INPUT_EV_KEY && evt->value) {
        btn_press_count++;
        printk("[BTN1] pressed (count=%u, code=%u)\n",
               btn_press_count, evt->code);
    }
}

/* 注册全局 input 回调（匹配所有 gpio-keys 设备） */
INPUT_CALLBACK_DEFINE(NULL, btn_input_cb, NULL);

int main(void)
{
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);

    printk("HC32F460 Zephyr demo starting\n");
    printk("USART1: PA9(TX) PA10(RX) @ 115200\n");
    printk("Shell available — type 'help' for commands\n");

    bool led_state = true;
    while (1) {
        gpio_pin_set_dt(&led0, led_state);
        gpio_pin_set_dt(&led1, !led_state);
        led_state = !led_state;
        k_msleep(500);
    }
}
```

**与旧版的关键区别：**

| 方面 | Phase 2 (旧版) | Phase 3 (新版) |
|------|----------------|----------------|
| 按键处理 | 手动 `gpio_init_callback` + `gpio_add_callback` + `gpio_pin_interrupt_configure_dt` | `INPUT_CALLBACK_DEFINE(NULL, cb, NULL)` 一行搞定 |
| 中断配置 | 应用层配置边沿触发 | DTS `gpio-keys` 驱动自动处理 |
| 串口 | USART2 轮询模式 | USART1 中断模式 + shell + DMA 模式 |
| Shell | 无 | 完整 shell 支持 |

### 9.2 Zephyr Input 子系统

DTS 中的 `gpio-keys` 兼容节点由 `drivers/input/input_gpio_keys.c` 自动驱动：

```dts
gpio_keys {
    compatible = "gpio-keys";
    btn1: button_1 {
        gpios = <&gpioe 2 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
        zephyr,code = <INPUT_KEY_0>;
    };
};
```

- `CONFIG_INPUT=y` 启用 input 子系统
- `CONFIG_INPUT_GPIO_KEYS` 由 DT 自动选择（`DT_HAS_GPIO_KEYS_ENABLED`）
- 回调签名：`void cb(struct input_event *evt, void *user_data)`
- `INPUT_CALLBACK_DEFINE(NULL, ...)` 中的 NULL 表示匹配所有 input 设备

### 9.3 Zephyr Shell Demo (shell_cmds.c)

文件：`src/shell_cmds.c`，提供 3 组 shell 命令：

**LED 控制：**
```
uart:~$ led on 0       → LED0 ON
uart:~$ led off 1      → LED1 OFF
uart:~$ led toggle 0   → LED0 toggled
uart:~$ led status      → LED0: ON, LED1: OFF
```

**GPIO 读取：**
```
uart:~$ gpio_read 4 2   → GPIO PE2 = 1 (按键未按下)
uart:~$ gpio_read 3 10  → GPIO PD10 = 0 (LED3 亮)
```

**系统信息：**
```
uart:~$ sysinfo
=== HC32F460PETB System Info ===
MCU: HC32F460 (Cortex-M4F)
SystemCoreClock: 8000000 Hz
Zephyr: 4.4.0
Uptime: 12345 ms
UART: USART1 PA9(TX)/PA10(RX) @ 115200
GPIO PWPR: 0x0001 (unlocked)
```

Shell 命令注册使用 Zephyr 标准宏：
```c
SHELL_STATIC_SUBCMD_SET_CREATE(sub_led,
    SHELL_CMD_ARG(on,     NULL, "Turn on LED <n>",  cmd_led_on,     2, 0),
    SHELL_CMD_ARG(off,    NULL, "Turn off LED <n>", cmd_led_off,    2, 0),
    SHELL_CMD_ARG(toggle, NULL, "Toggle LED <n>",   cmd_led_toggle, 2, 0),
    SHELL_CMD(status,     NULL, "Show LED states",  cmd_led_status),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(led, &sub_led, "LED control commands", NULL);
```

### 9.4 Kconfig 配置 (prj.conf)

```ini
# 基础驱动
CONFIG_GPIO=y                   # GPIO 驱动
CONFIG_SERIAL=y                 # 串口驱动
CONFIG_CONSOLE=y                # 控制台支持
CONFIG_UART_CONSOLE=y           # UART 作为控制台
CONFIG_PRINTK=y                 # printk 支持
CONFIG_LOG=y                    # 日志子系统

# HC32F460 特定
CONFIG_ARM_MPU=n                # 禁用 MPU (简化调试)
CONFIG_HW_STACK_PROTECTION=n    # 禁用栈保护
CONFIG_DYNAMIC_INTERRUPTS=y     # 动态中断连接 (GPIO + UART 需要)
CONFIG_MAIN_STACK_SIZE=2048     # 主线程栈大小
CONFIG_SYSTEM_CLOCK_NO_WAIT=y   # 跳过时钟等待

# [Phase 3] UART 中断模式 (shell 必需)
CONFIG_UART_HC32_INTERRUPT=y

# [Phase 3] Input 子系统 (gpio-keys 按键)
CONFIG_INPUT=y

# [Phase 3] Zephyr Shell
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
```

> **CONFIG_UART_HC32_INTERRUPT** 是 shell 正常工作的前提条件。Shell 需要通过中断接收
> 用户输入字符，纯轮询模式下 shell backend 无法运行。

---

## 10. 调试过程中的关键问题

### 10.1 GPIO 寄存器偏移量全部错误

**问题：** 最初基于参考手册推测的 GPIO 寄存器偏移量与实际硬件不一致。

| 寄存器 | 错误偏移 | 正确偏移 (offsetof 验证) |
|--------|----------|--------------------------|
| PWPR | 0x900 (地址 0x40053900) | **0x3FC (地址 0x40053BFC)** |
| PFSR | 0x0600 + port×0x40 + pin×0x04 | **0x0402 + port×0x40 + pin×0x04** |
| PIDR/PODR/... | 步进 0x04/端口 | **步进 0x10/端口** |

**验证方法：** 编写宿主机 C 程序，使用 `offsetof()` 从 DDL 的 `CM_GPIO_TypeDef` 结构体
定义中计算每个成员的偏移量：

```c
#include "hc32f460.h"
printf("PWPR: 0x%03lx\n", offsetof(CM_GPIO_TypeDef, PWPR));    // → 0x3FC
printf("PIDR: stride = 0x%03lx\n",
       offsetof(CM_GPIO_TypeDef, PIDRB) - offsetof(CM_GPIO_TypeDef, PIDRA)); // → 0x10
printf("PFSR: 0x%03lx\n", offsetof(CM_GPIO_TypeDef, PFSRA00)); // → 0x402
```

### 10.2 UART 无输出 — 三个隐藏差异

即使所有寄存器地址都修正后，UART 仍然无输出。通过 **裸机固件和 Zephyr 固件的寄存器
逐项对比** 发现三个差异：

| 差异 | 裸机值 | Zephyr 值 | 影响 |
|------|--------|-----------|------|
| CR1 bit 31 (SBS) | 1 (下降沿起始位) | 0 | DAPLink 不转发串口数据 |
| PFSR BFE bit | 0 | 1 | 干扰同端口 GPIO 输入读取 |
| PCR DRV bit | 高驱动 | 低驱动 | TX 信号质量不足 |

**对比方法：**
1. 先烧写已知可工作的裸机固件
2. 用 pyOCD 读取所有关键寄存器的值
3. 再烧写 Zephyr 固件
4. 再读一次相同寄存器，逐个对比差异

```bash
# 读取 USART2 CR1 对比
pyocd commander -t hc32f460xe -c "read32 0x4001D40C" -c "exit"
# 裸机: 0xA0008004 (SBS=1, FBME=1, OVER8=1, TE=1)
# Zephyr: 0x20008004 (SBS=0, FBME=1, OVER8=1, TE=1)  ← SBS 缺失!
```

**解决方案：**
1. 初始化 CR1 时加上 `USART_CR1_SBS` (bit 31)
2. `gpio_set_func()` 只设置 FSEL 位，不设置 BFE
3. TX 引脚 PCR 设置高驱动强度

### 10.3 GPIO 写保护未解锁

**问题：** soc.c 中的 GPIO PWPR 地址用的是错误地址 0x40053900 (偏移 0x900)，
实际地址是 0x40053BFC (偏移 0x3FC)，导致解锁操作无效，
所有后续的 PCR/PFSR 写入被静默忽略。

**症状：** pyOCD 读取 PWPR 显示 0x0000 (锁定状态)，而非 0x0001 (已解锁)。

**教训：** HC32F460 的写保护寄存器写入不会报错，只是静默丢弃，必须通过寄存器回读验证。

### 10.4 CMSIS 6 与 DDL 的冲突

**问题：** HC32F460 DDL 自带的 CMSIS Core (`drivers/cmsis/Include/core_cm4.h`) 使用旧
API (`SCB->SHP[12]`)，而 Zephyr 的 CMSIS 6 使用新 API (`SCB->SHPR[12]`)。同时包含两个
版本会导致结构体成员名不一致的编译错误。

**解决方案：** CMakeLists.txt 中只包含 DDL 的设备头文件目录，不包含其 CMSIS Core 目录：

```cmake
# ✓ 包含设备头文件
zephyr_include_directories(
  ${DDL_ROOT}/drivers/cmsis/Device/HDSC/hc32f4xx/Include
)
# ✗ 不包含旧 CMSIS Core
# ${DDL_ROOT}/drivers/cmsis/Include  ← 排除这个目录
```

### 10.5 PCR 位定义错误

**问题：** 最初的 PCR 位定义基于猜测：

```c
/* 错误定义 */
#define PCR_NOD   BIT(4)   /* 实际是 BIT(2) */
#define PCR_DRV   BIT(8)   /* 实际是 [5:4] */
#define PCR_PUU   BIT(9)   /* 实际是 BIT(6) */
```

**影响：** PUU 实际写到了 INVE 位 (bit 9 = 输入反相)，但由于按键有外部上拉，且反相后
读取逻辑恰好相反，程序仍然"看似正常"。修正后按键读取更加可靠。

### 10.6 调试工具和技术总结

| 方法 | 命令 | 用途 |
|------|------|------|
| pyOCD 寄存器读取 | `pyocd commander -t hc32f460xe -c "read32 0xADDR"` | 实时读取外设寄存器 |
| GDB 断点 | `break uart_hc32_poll_out` | 确认驱动函数是否被调用 |
| offsetof 验证 | `gcc -o offsets offsets.c && ./offsets` | 验证结构体寄存器偏移 |
| 裸机对比 | 先烧裸机 → 读寄存器 → 再烧 Zephyr → 对比 | 找出配置差异 |
| SWIER 模拟中断 | `write32 0x4005129C 0x04` | 软件触发 EXTINT 验证中断链路 |

---

## 11. 构建、烧写与调试

### 11.1 构建

```bash
source /home/firebot/zephyrproject/.venv/bin/activate
cd /home/firebot/zephyrproject/zephyr-example/hc32f460petb

# 清洁构建
west build -b hc32f460petb . --build-dir build

# 增量构建
west build --build-dir build
```

**构建结果：** 57 KB Flash, 16 KB RAM (含 shell + 中断驱动 UART)

### 11.2 烧写

```bash
pyocd load -t hc32f460xe build/zephyr/zephyr.hex
# 带复位
pyocd load -t hc32f460xe build/zephyr/zephyr.hex && pyocd reset -t hc32f460xe
```

### 11.3 串口监控

```bash
picocom /dev/ttyACM0 -b 115200
```

**预期输出：**
```
*** Booting Zephyr OS build 149c8b1758a8 ***
HC32F460 Zephyr demo starting
Button interrupt configured (falling edge)
LED0 (PD10), LED1 (PE15), BTN1 (PE2) configured
alive (led=0)
alive (led=1)
[BTN1] pressed (count=1)
alive (led=0)
...
```

### 11.4 GDB 调试

```bash
# 终端 1: 启动 GDB 服务器
pyocd gdbserver -t hc32f460xe --port 3333

# 终端 2: 连接 GDB
arm-zephyr-eabi-gdb build/zephyr/zephyr.elf \
  -ex "target remote :3333" \
  -ex "monitor reset halt" \
  -ex "break main" \
  -ex "continue"
```

### 11.5 常用寄存器检查命令

```bash
pyocd commander -t hc32f460xe \
  -c "read16 0x40053BFC" \
  -c "read32 0x4001D00C" \
  -c "read32 0x4001D008" \
  -c "exit"
```

| 寄存器 | 地址 | 期望值 | 说明 |
|--------|------|--------|------|
| GPIO PWPR | 0x40053BFC | 0x0001 | GPIO 已解锁 |
| USART1 CR1 | 0x4001D00C | 0xA00080AC | SBS+FBME+OVER8+TXEIE+RIE+TE+RE |
| USART1 BRR | 0x4001D008 | 0x07EC | 115200 baud |
| PA9 PFSR | 0x40053E24 | 0x0020 | FUNC_32 (USART1_TX) |
| PA10 PFSR | 0x40053E28 | 0x0021 | FUNC_33 (USART1_RX) |
| INTC SEL[16] | 0x40051098 | 279 | USART1_RI → IRQ16 |
| NVIC ISER0 | 0xE000E100 | 0x000F0004 | IRQ 2+16+17+18+19 |

> **GDB 调试注意事项：** pyOCD 的内存映射不包含 HC32F460 外设区域。需要在 GDB 中设置
> `set mem inaccessible-by-default off` 才能读取外设寄存器。

---

## 附录：文件清单

### 裸机项目 (hc32f460petb_baremetal/)
| 文件 | 说明 |
|------|------|
| gpio_output/GCC/Makefile | 自动检测工具链的 Makefile |
| gpio_output/GCC/source/picolibc_stub.c | picolibc stdout 桩函数 |

### Zephyr 项目 (hc32f460petb/)
| 文件 | 说明 |
|------|------|
| CMakeLists.txt | 顶层构建, 设置 SOC/BOARD/DTS ROOT, 条件编译 shell |
| prj.conf | Kconfig: GPIO, SERIAL, INPUT, SHELL, UART_INTERRUPT |
| src/main.c | LED 闪烁 + input 子系统按键 + 串口启动消息 |
| src/shell_cmds.c | Shell 命令: led, gpio_read, sysinfo |
| soc/hdsc/hc32f460/soc.c | 早期初始化: SystemInit + LL_PERIPH_WE(LL_PERIPH_ALL) |
| soc/hdsc/hc32f460/soc.h | CMSIS IRQ 名称兼容 |
| soc/hdsc/hc32f460/CMakeLists.txt | DDL 集成 (排除旧 CMSIS Core) |
| soc/hdsc/hc32f460/hc32f4xx_conf.h | DDL 模块开关 |
| soc/hdsc/hc32f460/Kconfig.soc | CPU 特性: CM4, FPU, MPU, DWT |
| soc/hdsc/hc32f460/Kconfig.defconfig | NUM_IRQS=144, 8MHz 时钟 |
| dts/arm/hdsc/hc32f460.dtsi | SoC DTS: 内存, GPIO, USART1/USART2 |
| boards/hdsc/hc32f460petb/hc32f460petb.dts | Board DTS: LED, BTN (gpio-keys), 控制台=USART1 |
| boards/hdsc/hc32f460petb/board.cmake | pyOCD 烧写配置 |
| drivers/gpio/gpio_hc32.c | GPIO 驱动 + EXTINT 中断支持 |
| drivers/serial/uart_hc32.c | UART 驱动: 多实例 + 轮询 + 中断 + DMA 模式 |
| drivers/serial/Kconfig | UART_HC32_INTERRUPT / UART_HC32_DMA 配置 |
| doc/porting-guide.md | 本文档 |
| doc/startup-comparison.md | 裸机 vs Zephyr 启动流程对比 |
