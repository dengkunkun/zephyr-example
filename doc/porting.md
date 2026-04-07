# Zephyr SoC 与 Board 移植说明

## 1. 目的

本文说明 Zephyr 中 SoC 与 Board 的职责边界、目录结构、移植流程，以及设备树节点如何映射到 binding 和驱动源码。内容基于以下资料与本地代码核对结果整理：

- SoC Porting Guide  
  `https://docs.zephyrproject.org/latest/hardware/porting/soc_porting.html`
- Board Porting Guide  
  `https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html`
- Introduction to devicetree  
  `https://docs.zephyrproject.org/latest/build/dts/intro.html`
- Setting Kconfig configuration values  
  `https://docs.zephyrproject.org/latest/build/kconfig/setting.html`
- Custom Board, Devicetree and SOC Definitions  
  `https://docs.zephyrproject.org/latest/develop/application/index.html#custom-board-definition`
- Building, Flashing and Debugging  
  `https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html`

本文同时结合当前工作区中的 `STM32F411` 实例：

- SoC：`/home/firebot/zephyrproject/zephyr/soc/st/stm32/stm32f4x`
- SoC 设备树：`/home/firebot/zephyrproject/zephyr/dts/arm/st/f4`
- 官方板级实例：
  - `zephyr/boards/st/nucleo_f411re`
  - `zephyr/boards/weact/blackpill_f411ce`
- 当前自定义板实例：
  - `zephyr-example/f411ceu6/boards/weact/f411ceu6`

---

## 2. 硬件支持层次

Zephyr 的硬件支持按以下层次组织：

1. **Architecture**：CPU 架构支持，例如 `arch/arm`
2. **SoC**：芯片级公共支持，包括时钟、复位、中断控制器、基础外设、启动文件、链接脚本和 SoC 级 Kconfig
3. **Board**：具体开发板支持，包括板级设备树、引脚分配、默认控制台、板载 LED、按键、Flash/Debug runner 等
4. **Application**：应用自身的 `prj.conf`、overlay、源码

职责划分原则如下：

- **SoC** 负责描述“芯片拥有哪些硬件资源”
- **Board** 负责描述“这块板子如何使用这些硬件资源”
- **Application** 负责描述“当前应用需要打开哪些软件功能，以及对默认硬件描述做哪些增量修改”

---

## 3. 设备树与 Kconfig 的分工

### 3.1 设备树

设备树用于描述**硬件事实**，例如：

- 外设地址、中断号、时钟输入
- 引脚复用
- 板载 LED、按键、传感器
- 默认控制台指向哪个 UART
- 某个外设在当前板子上是否启用

Zephyr 构建时会解析 DTS、DTSI 和 overlay，生成中间和最终文件，例如：

- `build/zephyr/zephyr.dts`
- `build/zephyr/include/generated/zephyr/devicetree_generated.h`

驱动代码通过 `DT_*` 宏读取这些信息。

### 3.2 Kconfig

Kconfig 用于描述**软件配置与编译开关**，例如：

- 是否编译 GPIO、I2C、SPI、UART 子系统
- 是否启用控制台、日志、Shell
- 默认栈大小、缓冲区大小等软件参数

构建时 Zephyr 会合并板级 defconfig、应用 `prj.conf` 和其他配置片段，生成：

- `build/zephyr/.config`
- `build/zephyr/include/generated/zephyr/autoconf.h`

简化理解如下：

- **设备树回答“硬件是什么、连到了哪里”**
- **Kconfig 回答“软件是否启用该功能、以什么方式构建”**

### 3.3 二者的边界

下列信息应优先放在设备树中：

- GPIO 管脚与极性
- 设备地址、中断、时钟、DMA、pinctrl
- 板载器件与连接关系

下列信息应优先放在 Kconfig 中：

- 驱动和子系统开关
- 默认软件参数
- 日志级别、线程栈大小、协议栈功能开关

---

## 4. SoC 移植

### 4.1 目录结构

官方文档要求 SoC 支持位于：

`soc/<VENDOR>/<soc-name>`

典型结构如下：

```text
soc/<VENDOR>/<soc-name>
├── soc.yml
├── soc.h
├── CMakeLists.txt
├── Kconfig
├── Kconfig.soc
└── Kconfig.defconfig
```

其中：

- `soc.yml`：声明 SoC、series、family、CPU cluster 元数据
- `soc.h`：SoC 公共头文件
- `CMakeLists.txt`：加入 SoC 源文件、包含路径、链接脚本
- `Kconfig.soc`：定义 `SOC_<name>`、`SOC_SERIES_<name>`、`SOC_FAMILY_<name>` 等基础符号
- `Kconfig`：选择架构能力、`HAS_*` 能力和 SoC 功能
- `Kconfig.defconfig`：设置 SoC 默认值

### 4.2 SoC 设备树

SoC 设备树位于：

`dts/<ARCH>/<VENDOR>/<soc>.dtsi`

典型内容包括：

- `cpus`
- `soc`
- 时钟、复位控制器
- GPIO、UART、I2C、SPI、Timer、EXTI 等基础外设节点

SoC `.dtsi` 应由使用该 SoC 的 board `.dts` 引用。

### 4.3 SoC 移植步骤

1. 建立 `soc/<vendor>/<soc-family-or-series>` 目录
2. 编写 `soc.yml`
3. 编写 `Kconfig.soc`，定义 `SOC_<NAME>`、`SOC_SERIES_<NAME>`、`SOC_FAMILY_<NAME>`
4. 编写 `Kconfig`，选择架构与 SoC 能力
5. 编写 `Kconfig.defconfig`，设置 SoC 默认值
6. 编写 `soc.h`、`soc.c`、`power.c` 等 SoC 源文件
7. 编写 `dts/<arch>/<vendor>/<soc>.dtsi`
8. 为各外设节点填写 `compatible`、`reg`、`interrupts`、`clocks`、`status`
9. 验证相关 binding 与驱动是否已存在
10. 构建并检查生成结果 `zephyr.dts` 与 `.config`

---

## 5. Board 移植

### 5.1 目录结构

官方文档要求 board 位于：

`boards/<VENDOR>/<board-name>`

典型结构如下：

```text
boards/<VENDOR>/plank
├── board.yml
├── board.cmake
├── CMakeLists.txt
├── Kconfig.plank
├── Kconfig.defconfig
├── plank_<qualifiers>_defconfig
├── plank_<qualifiers>.dts
└── plank_<qualifiers>.yaml
```

其中：

- `board.yml`：板级元数据，定义 board 名称、SoC、revision、variant
- `board.cmake`：`west flash` 和 `west debug` runner 配置
- `Kconfig.<board>`：选择 SoC 与板级 Kconfig 基础项
- `Kconfig.defconfig`：板级默认配置
- `<board>_<qualifiers>.dts`：板级硬件描述
- `<board>_<qualifiers>.yaml`：Twister 使用的测试元数据

### 5.2 Board 设备树职责

Board `.dts` 的主要职责是：

1. `#include` 对应 SoC `.dtsi`
2. 设置 `chosen`
3. 定义板载器件，例如 `gpio-leds`、`gpio-keys`
4. 设置 `aliases`，供通用 sample 使用
5. 通过 `&uartX`、`&i2cX`、`&spiX`、`&gpioX`、`&timersX` 覆盖 SoC 节点，将需要的外设改为 `status = "okay"`
6. 绑定 `pinctrl-0`、`pinctrl-names` 等板级连线信息

### 5.3 Board 命名与 qualifiers

Zephyr 当前硬件模型使用如下命名形式：

`board[@revision][/soc[/cpucluster][/variant]]`

例如：

`bl5340_dvk@1.2.0/nrf5340/cpuapp/ns`

若是单 SoC、单核、无 variant 的简单板子，构建时通常只写 board 名称即可。

### 5.4 Board 移植步骤

1. 选择一个已支持的同类板作为模板
2. 建立 `boards/<vendor>/<board>` 目录
3. 编写 `board.yml`
4. 编写 `Kconfig.<board>` 以选择正确 SoC
5. 编写 `Kconfig.defconfig` 与 `<board>_<qualifiers>_defconfig`
6. 编写 `<board>_<qualifiers>.dts`
7. 为控制台、LED、按键、存储、传感器等板载器件设置 `chosen` 与 `aliases`
8. 在 `board.cmake` 中配置 flash/debug runner
9. 通过 `west build`、`west flash`、`west debug` 验证

---

## 6. 应用内自定义板、SoC 与设备树根目录

若硬件支持尚未上游，可在应用或私有仓库中维护：

- `BOARD_ROOT`
- `SOC_ROOT`
- `DTS_ROOT`

官方文档给出的调用方式如下：

- 自定义 board：`-DBOARD_ROOT=<path>`
- 自定义 SoC：`-DSOC_ROOT=<path>`
- 自定义设备树树根：`-DDTS_ROOT=<path>`

当前工作区已有一个应用内 board 实例：

- `zephyr-example/f411ceu6/boards/weact/f411ceu6`

其关键文件包括：

- `board.yml`
- `board.cmake`
- `Kconfig.f411ceu6`
- `Kconfig.defconfig`
- `f411ceu6.dts`
- `f411ceu6.yaml`

其中 `board.yml` 内容表明该板选择的 SoC 为：

- `stm32f411xe`

这说明该应用内板定义沿用了 Zephyr 已有的 STM32F411 SoC 支持，仅新增本地 board 层描述。

---

## 7. STM32F411 实例：SoC 支持结构

### 7.1 SoC 目录

当前工作区中 `STM32F4` SoC 支持位于：

- `/home/firebot/zephyrproject/zephyr/soc/st/stm32/stm32f4x`

关键文件如下：

- `CMakeLists.txt`：加入 `soc.c`，按配置加入 `power.c` 和 `poweroff.c`，指定 ARM Cortex-M 链接脚本
- `Kconfig.soc`：定义 `SOC_STM32F411XE` 与 `SOC_SERIES_STM32F4X`
- `Kconfig.defconfig`：设置 STM32F4 系列缺省值，例如 `SHARED_INTERRUPTS`、`IDLE_STACK_SIZE` 等
- `soc.h`：包含 `stm32f4xx.h`
- `soc.c` 和 `power.c`：SoC 级初始化与电源相关逻辑

### 7.2 STM32F411 设备树继承关系

`STM32F411` 的设备树不是单文件完成，而是按层继承：

1. `zephyr/dts/arm/st/f4/stm32f4.dtsi`  
   提供 STM32F4 公共定义，包含 RCC、EXTI、GPIO、USART、I2C、SPI、Timer、RTC、ADC、DMA 等基础节点。
2. `zephyr/dts/arm/st/f4/stm32f401.dtsi`  
   在 F4 公共层之上增加 `F401` 相关差异。
3. `zephyr/dts/arm/st/f4/stm32f411.dtsi`  
   在 `F401` 基础上增加 `F411` 特有外设，例如额外的 SPI 和 I2S 节点。
4. `zephyr/dts/arm/st/f4/stm32f411Xe.dtsi`  
   主要补充 `XE` 封装和容量变体对应的内存布局。

Board `.dts` 通常直接包含 `stm32f411Xe.dtsi`，从而间接继承上述整条链。

---

## 8. STM32F411 实例：Board 支持结构

### 8.1 官方板 `nucleo_f411re`

路径：

- `/home/firebot/zephyrproject/zephyr/boards/st/nucleo_f411re`

关键文件：

- `board.yml`
- `board.cmake`
- `Kconfig.nucleo_f411re`
- `Kconfig.defconfig`
- `nucleo_f411re.dts`
- `nucleo_f411re.yaml`
- `nucleo_f411re_defconfig`
- `arduino_r3_connector.dtsi`
- `st_morpho_connector.dtsi`
- `support/openocd.cfg`

`nucleo_f411re.dts` 的作用包括：

- 包含 `stm32f411Xe.dtsi`
- 设置 `zephyr,console = &usart2`
- 定义板载 LED：`GPIOA5`
- 定义用户按键：`GPIOC13`
- 设置 `aliases { led0; sw0; }`
- 将 `usart1`、`usart2`、`i2c1`、`spi1`、`rtc` 等节点设置为 `okay`
- 绑定对应 `pinctrl`

### 8.2 官方板 `blackpill_f411ce`

当前工作区中的真实路径为：

- `/home/firebot/zephyrproject/zephyr/boards/weact/blackpill_f411ce`

其中 `blackpill_f411ce.dts`：

- 包含 `stm32f411Xe.dtsi`
- 包含 `stm32f411c(c-e)ux-pinctrl.dtsi`
- 设置 `zephyr,console = &usart1`
- 定义板载 LED：`GPIOC13`
- 定义按键：`GPIOA0`
- 设置 `aliases { led0; sw0; }`
- 额外配置 Flash 分区、`timers4/pwm4`、`i2c1`、`spi1`、`adc1`、`usbotg_fs`、`rtc`、`clk_hse`、`pll`、`rcc` 等节点

### 8.3 当前自定义板 `f411ceu6`

路径：

- `/home/firebot/zephyrproject/zephyr-example/f411ceu6/boards/weact/f411ceu6`

该板的 `f411ceu6.dts` 结构与 `blackpill_f411ce.dts` 非常接近，说明当前本地移植采用的是：

- **复用 Zephyr 已支持的 STM32F411 SoC 层**
- **新增本地 board 层进行板级裁剪和引脚配置**

这是推荐路径：如果 SoC 已被 Zephyr 支持，应优先新增 board，而不是复制一套 SoC 支持。

---

## 9. 设备树节点到代码的映射

本节回答两个关键问题：

1. `DTS` 和 `DTSI` 中的节点最终对应到哪里
2. 为什么直接搜索 `compatible` 字符串，有时在驱动源码中找不到完全相同的文本

### 9.1 SoC 层节点映射

| 设备树节点或 compatible | 设备树位置 | Binding | 驱动源码 |
| --- | --- | --- | --- |
| `st,stm32-gpio` | `zephyr/dts/arm/st/f4/stm32f4.dtsi` 中各 `gpiox` 节点 | `zephyr/dts/bindings/gpio/st,stm32-gpio.yaml` | `zephyr/drivers/gpio/gpio_stm32.c` |
| `st,stm32-usart` / `st,stm32-uart` | `stm32f4.dtsi` 中 `usart1`、`usart2`、`usart6` | `zephyr/dts/bindings/serial/st,stm32-usart.yaml`、`zephyr/dts/bindings/serial/st,stm32-uart.yaml` | `zephyr/drivers/serial/uart_stm32.c` |
| `st,stm32-i2c-v1` | `stm32f4.dtsi` 中 `i2c1`、`i2c2`、`i2c3` | `zephyr/dts/bindings/i2c/st,stm32-i2c-v1.yaml` | `zephyr/drivers/i2c/i2c_stm32.c`、`zephyr/drivers/i2c/i2c_stm32_v1.c` |
| `st,stm32-spi` | `stm32f4.dtsi`、`stm32f401.dtsi`、`stm32f411.dtsi` | `zephyr/dts/bindings/spi/st,stm32-spi.yaml` | `zephyr/drivers/spi/spi_stm32.c` |
| `st,stm32-exti` | `stm32f4.dtsi` 中 `exti` 节点 | `zephyr/dts/bindings/interrupt-controller/st,stm32-exti.yaml` | `zephyr/drivers/interrupt_controller/intc_exti_stm32.c` |
| `st,stm32f4-rcc` | `stm32f4.dtsi` 中 `rcc` 节点 | `zephyr/dts/bindings/clock/st,stm32f4-rcc.yaml` | `zephyr/drivers/clock_control/clock_stm32f2_f4_f7.c` |
| `st,stm32-pinctrl` | `stm32f4.dtsi` 中 `pinctrl` 节点 | `zephyr/dts/bindings/pinctrl/st,stm32-pinctrl.yaml` | `zephyr/drivers/pinctrl/pinctrl_stm32.c` |
| `st,stm32-timers` | `stm32f4.dtsi` 中 `timers1` 到 `timers11` 节点 | `zephyr/dts/bindings/timer/st,stm32-timers.yaml` | 具体功能由子节点驱动消费 |
| `st,stm32-pwm` | `timers` 子节点 | `zephyr/dts/bindings/pwm/st,stm32-pwm.yaml` | `zephyr/drivers/pwm/pwm_stm32.c` |
| `st,stm32-counter` | `timers` 子节点 | `zephyr/dts/bindings/counter/st,stm32-counter.yaml` | `zephyr/drivers/counter/counter_stm32_timer.c` |
| `st,stm32-qdec` | `timers` 子节点 | `zephyr/dts/bindings/sensor/st,stm32-qdec.yaml` | `zephyr/drivers/sensor/st/qdec_stm32/qdec_stm32.c` |
| `st,stm32-flash-controller` / `st,stm32f4-flash-controller` | `stm32f4.dtsi` 中 Flash 控制器节点 | `zephyr/dts/bindings/flash_controller/st,stm32-flash-controller.yaml`、`zephyr/dts/bindings/flash_controller/st,stm32f4-flash-controller.yaml` | `zephyr/drivers/flash/flash_stm32.c`、`zephyr/drivers/flash/flash_stm32f4x.c` |

### 9.2 Board 层节点映射

| Board 层节点 | 位置 | Binding 或消费代码 | 驱动或消费路径 |
| --- | --- | --- | --- |
| `compatible = "gpio-leds"` | `nucleo_f411re.dts`、`blackpill_f411ce.dts`、`f411ceu6.dts` | `zephyr/dts/bindings/led/gpio-leds.yaml` | `zephyr/drivers/led/led_gpio.c` |
| `compatible = "gpio-keys"` | 同上 | `zephyr/dts/bindings/input/gpio-keys.yaml` | `zephyr/drivers/input/input_gpio_keys.c` |
| `aliases { led0 = ...; }` | 同上 | 由应用通过 `DT_ALIAS(led0)` 使用 | 例如 `f411re/blinky/src/main.c` |
| `aliases { sw0 = ...; }` | 同上 | 由应用通过 `DT_ALIAS(sw0)` 使用 | 例如 `zephyr/samples/drivers/gpio/button_interrupt/src/main.c` |
| `chosen { zephyr,console = &usartX; }` | 同上 | 由控制台子系统通过 `DT_CHOSEN(zephyr_console)` 消费 | `zephyr/drivers/console/uart_console.c` |

### 9.3 实际调用链示例

#### `led0`

调用链：

1. Board `.dts` 中 `aliases { led0 = &green_led_2; }`
2. 应用中使用 `DT_ALIAS(led0)`
3. 例如：`f411re/blinky/src/main.c`
4. 节点属性 `gpios = <&gpioa 5 GPIO_ACTIVE_HIGH>` 由 GPIO 驱动 `gpio_stm32.c` 最终执行

#### `sw0`

调用链：

1. Board `.dts` 中 `aliases { sw0 = &user_button; }`
2. 示例应用 `zephyr/samples/drivers/gpio/button_interrupt/src/main.c` 中使用 `DT_ALIAS(sw0)`
3. `GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, ...)` 提取 GPIO 控制器、引脚号和标志
4. GPIO 驱动 `gpio_stm32.c` 与 EXTI 驱动 `intc_exti_stm32.c` 共同完成中断配置

#### `zephyr,console`

调用链：

1. Board `.dts` 中 `chosen { zephyr,console = &usart2; }`
2. `zephyr/drivers/console/uart_console.c` 中通过 `DEVICE_DT_GET(DT_CHOSEN(zephyr_console))` 获取设备
3. UART 驱动 `zephyr/drivers/serial/uart_stm32.c` 负责底层串口操作
4. `pinctrl_stm32.c` 根据 `pinctrl-0` 完成对应引脚复用配置

---

## 10. 为什么直接搜索 compatible 字符串，常常找不到驱动代码

这是 Zephyr 设备模型中最容易混淆的部分。

### 10.1 `DT_DRV_COMPAT` 会改写命名

驱动通常不会直接写：

```c
"st,stm32-gpio"
```

而是写成：

```c
#define DT_DRV_COMPAT st_stm32_gpio
```

转换规则是：

- 逗号 `,` 转换为下划线 `_`
- 连字符 `-` 转换为下划线 `_`

因此：

- `st,stm32-gpio` → `st_stm32_gpio`
- `st,stm32-i2c-v1` → `st_stm32_i2c_v1`
- `gpio-leds` → `gpio_leds`

因此，如果只用原始 `compatible` 字符串在驱动 `.c` 文件中全文搜索，常常会误以为“没有驱动”。

### 10.2 binding 和 driver 是两层关系

Zephyr 中通常有三层映射：

1. **设备树节点**：给出 `compatible`
2. **binding YAML**：定义该节点允许哪些属性、属性含义是什么
3. **驱动源码**：通过 `DT_DRV_COMPAT` 或 `DEVICE_DT_*` 宏注册设备实例

因此正确的追踪方式不是只搜一层，而是按以下路径：

`DTS/DTSI 节点 -> binding YAML -> 驱动源码`

### 10.3 一个主节点可能对应多个子驱动

以 `timers` 为例：

- 主节点 `compatible = "st,stm32-timers"`
- 其子节点可能是：
  - `st,stm32-pwm`
  - `st,stm32-counter`
  - `st,stm32-qdec`

这意味着：

- 主节点描述定时器硬件资源
- 具体功能由不同子节点和不同驱动处理

因此仅搜索主节点 compatible，往往无法直接定位到最终使用的功能驱动。

### 10.4 `chosen` 和 `aliases` 不是驱动 compatible

`chosen` 与 `aliases` 本身不是设备驱动，不存在“对应的驱动文件”。

它们的作用是：

- `chosen`：为系统子系统指定默认设备，例如 `zephyr,console`
- `aliases`：为应用和样例提供稳定的逻辑名称，例如 `led0`、`sw0`

因此应追踪“消费这些宏的代码”，而不是寻找 binding 驱动。

---

## 11. 基于当前工作区的移植建议

### 11.1 如果 SoC 已有支持，仅新增 Board

对于 `STM32F411CEU6`，当前工作区已经存在：

- SoC 支持：`zephyr/soc/st/stm32/stm32f4x`
- SoC 设备树链：`stm32f4.dtsi -> stm32f401.dtsi -> stm32f411.dtsi -> stm32f411Xe.dtsi`

因此应优先采用以下策略：

1. 复用现有 SoC 支持
2. 新建 board 目录
3. 编写板级 `board.yml`、`Kconfig`、`.dts`、`board.cmake`
4. 在 board `.dts` 中设置本板的控制台、LED、按键、I2C、SPI、USB 等节点

不应复制一整套 STM32F4 SoC 支持，除非确实存在 Zephyr 尚未支持的 SoC 差异。

### 11.2 如果是应用内私有移植

当前 `zephyr-example/f411ceu6/boards/weact/f411ceu6` 已经体现了一种可行方案：

- 将 board 放在应用目录中
- 构建时通过 `BOARD_ROOT` 引入

若后续需要增加私有 SoC、binding 或 `.dtsi`，可继续引入：

- `SOC_ROOT`
- `DTS_ROOT`

### 11.3 典型验证流程

1. `west build -b <board> <app>`
2. 检查 `build/zephyr/zephyr.dts`
3. 检查 `build/zephyr/include/generated/zephyr/devicetree_generated.h`
4. 检查 `build/zephyr/.config`
5. 若 `board.cmake` 已完成，再执行 `west flash` 和 `west debug`

重点检查：

- `chosen` 是否指向正确 UART
- `aliases` 是否提供 `led0`、`sw0`
- 需要的外设节点是否为 `status = "okay"`
- `pinctrl-0` 是否绑定到正确引脚组
- 对应驱动是否已启用

---

## 12. 结论

Zephyr 的移植工作应按“先 SoC，后 Board，最后应用”的顺序进行。

对 `STM32F411` 这类已被 Zephyr 支持的芯片，实际工作重点通常不在 SoC 层，而在 Board 层：

- 选择正确的 SoC `.dtsi`
- 正确设置 `chosen` 和 `aliases`
- 启用板上实际接出的外设实例
- 提供正确的 `pinctrl` 和默认 Kconfig
- 配置 `board.cmake` 支持下载与调试

当需要追踪“某个设备树节点最终由哪段代码处理”时，应始终沿着以下路径分析：

`DTS/DTSI -> binding YAML -> DT_DRV_COMPAT -> 驱动源码 -> 应用或子系统消费点`

这是定位设备树、驱动与样例代码之间关系的有效方法。