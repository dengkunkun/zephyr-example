# Zephyr GPIO 子系统实现详解

本文结合当前 `nucleo_f411re` 板级设备树、`f411re/blinky`、Zephyr 官方 GPIO 文档，以及 STM32 GPIO 驱动源码，系统说明 Zephyr GPIO 子系统的设计与实现。

重点关注：

- GPIO 在 Zephyr 中是如何被抽象的
- 设备树如何描述 GPIO 控制器与具体引脚
- `gpio_dt_spec` 与 `GPIO_DT_SPEC_GET()` 为什么是推荐用法
- STM32 GPIO 驱动如何落地这些抽象
- 普通输出、输入中断、callback 分发分别如何工作

---

## 1. GPIO 子系统在 Zephyr 中的角色

GPIO 是 Zephyr 中最基础、也是最常见的外设子系统之一。

从设计上看，Zephyr 不希望应用直接：

- 写 GPIO 寄存器地址
- 硬编码端口和引脚编号
- 手写板级差异判断

而是希望应用通过：

- 设备树描述硬件连线
- 通用 GPIO API 操作引脚
- 底层驱动完成不同 SoC/MCU 的适配

因此，GPIO 子系统本质上是一套“三层模型”：

1. **设备树层**：描述这个引脚接在哪个控制器、引脚号是多少、极性是什么
2. **驱动抽象层**：提供统一 API，如配置输入/输出/中断
3. **SoC 驱动实现层**：例如 STM32 的 `gpio_stm32.c`

---

## 2. 当前工程相关文件

### 2.1 应用与 sample

- `f411re/blinky/src/main.c`
- `zephyr/samples/drivers/gpio/button_interrupt/src/main.c`

### 2.2 公共头文件与驱动

- `zephyr/include/zephyr/drivers/gpio.h`
- `zephyr/drivers/gpio/gpio_stm32.c`

### 2.3 板级与构建设备树

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
- `f411re/build/zephyr/zephyr.dts`

---

## 3. 当前板子上的 GPIO 资源是如何描述的

板级 DTS 位于：

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`

其中最关键的 GPIO 相关描述有：

- `led0 = &green_led_2`
- `sw0 = &user_button`

对应节点定义为：

- LED：`gpios = <&gpioa 5 GPIO_ACTIVE_HIGH>`
- 按键：`gpios = <&gpioc 13 GPIO_ACTIVE_LOW>`

这三元组/多元组信息非常关键，含义分别是：

1. GPIO 控制器：`&gpioa` 或 `&gpioc`
2. 引脚编号：`5` 或 `13`
3. 标志位：如 `GPIO_ACTIVE_HIGH` / `GPIO_ACTIVE_LOW`

因此：

- 板载 LED2 在 `PA5`
- 用户按键在 `PC13`

### 3.1 为什么 `aliases` 很重要

因为应用代码通常不会直接写 `&gpioa 5` 这种板级细节，而是写：

- `DT_ALIAS(led0)`
- `DT_ALIAS(sw0)`

这样一来：

- 应用逻辑不变
- 换板子只需改板级设备树别名
- sample 可以跨很多板复用

这就是 Zephyr 设备树设计的精髓之一：**把板级差异从应用中挪出去。**

---

## 4. 构建后设备树才是最终落地结果

虽然源文件是 `nucleo_f411re.dts`，但调试时建议优先看：

- `f411re/build/zephyr/zephyr.dts`

因为这是合并后的最终硬件视图。

在当前构建产物中，可以看到：

- `led0 = &green_led_2`
- `sw0 = &user_button`
- `gpioa: gpio@40020000`
- `gpioc: gpio@40020800`
- `exti: interrupt-controller@40013c00`

这说明当前工程中：

- LED 和按键最终确实分别落在 `GPIOA` 和 `GPIOC`
- 外部中断能力与 STM32 的 `EXTI` 控制器相关

---

## 5. GPIO 公共 API：`gpio.h`

Zephyr 的 GPIO 公共接口位于：

- `zephyr/include/zephyr/drivers/gpio.h`

这是整个 GPIO 子系统的核心对外接口头文件。

里面最值得掌握的概念有：

- `struct gpio_dt_spec`
- `GPIO_DT_SPEC_GET()`
- `gpio_is_ready_dt()`
- `gpio_pin_configure_dt()`
- `gpio_pin_set_dt()` / `gpio_pin_toggle_dt()`
- `gpio_pin_interrupt_configure_dt()`
- `gpio_callback`
- `gpio_init_callback()`
- `gpio_add_callback()`

---

## 6. `gpio_dt_spec`：推荐用法的核心

### 6.1 它是什么

`struct gpio_dt_spec` 本质上把一个 GPIO 资源打包成一个结构体，至少包含：

- 对应 GPIO 设备指针
- pin 编号
- flags 标志

也就是说，它把“这个引脚属于哪个控制器、编号是多少、逻辑极性如何”三件事绑定在一起。

### 6.2 为什么推荐它

因为如果不用 `gpio_dt_spec`，应用就得自己分别维护：

- `const struct device *port`
- `gpio_pin_t pin`
- `gpio_flags_t flags`

这样容易：

- 写错
- 漏掉 active low/high 语义
- 板级迁移时代码变脆

而 `gpio_dt_spec` 配合 `GPIO_DT_SPEC_GET()` 后，应用只要围绕一个对象操作就行，清晰得多。

---

## 7. `blinky` 示例：GPIO 输出的标准写法

示例文件：

- `f411re/blinky/src/main.c`

这是当前工程最直接的 GPIO 输出例子。

### 7.1 代码路径

sample 的关键逻辑是：

1. `#define LED0_NODE DT_ALIAS(led0)`
2. `static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);`
3. `gpio_is_ready_dt(&led)`
4. `gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE)`
5. 循环中调用 `gpio_pin_toggle_dt(&led)`
6. `k_msleep(SLEEP_TIME_MS)`

### 7.2 这里发生了什么

从抽象层面理解，这几行代码完成了：

- 从设备树把 `led0` 解析成一个 GPIO 资源描述
- 检查底层 GPIO 控制器驱动是否已经初始化完成
- 把引脚配置成输出模式
- 周期性切换引脚电平

### 7.3 为什么 `GPIO_OUTPUT_ACTIVE` 很重要

这个标志不是简单地“输出高电平”，而是“输出到逻辑 active 状态”。

若某个引脚在设备树中标为：

- `GPIO_ACTIVE_HIGH`

那么 active 就对应高电平；

若标为：

- `GPIO_ACTIVE_LOW`

那么 active 可能对应低电平。

这让应用可以写“逻辑语义”，而不是写“物理电平细节”。

---

## 8. `button_interrupt` 示例：GPIO 输入中断的标准写法

示例文件：

- `zephyr/samples/drivers/gpio/button_interrupt/src/main.c`

这个 sample 展示的是另一条完整路径：**GPIO 不只是能读写，还能作为中断事件源。**

### 8.1 sample 的关键步骤

1. 获取 `sw0` 对应的 `gpio_dt_spec`
2. 配置按键引脚为输入
3. `gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE)`
4. 定义 `struct gpio_callback`
5. `gpio_init_callback()` 初始化回调结构
6. `gpio_add_callback(button.port, &button_cb_data)` 注册到驱动

### 8.2 这说明了什么

对应用来说，GPIO 中断不是直接：

- 操作 NVIC
- 写 EXTI 寄存器
- 写中断向量表

而是通过统一 GPIO API 把“某个 pin 的中断事件”注册给驱动层。

应用只关心：

- 哪个 pin
- 什么时候触发（上升沿、下降沿、双边沿、逻辑激活）
- 触发后调用哪个 callback

底层复杂度由 GPIO 驱动和中断子系统承担。

---

## 9. GPIO callback 机制的本质

很多初学者会把 `gpio_init_callback()` 误认为“注册硬件中断”。

实际上它注册的是：

- **驱动层回调对象**

也就是说：

1. 硬件 IRQ 先进入底层 ISR
2. 底层驱动判断是哪些 pin 产生事件
3. 驱动遍历 callback 列表
4. 调用匹配 pin mask 的 callback

因此：

- `IRQ_CONNECT()` 是 ISR 注册
- `gpio_add_callback()` 是驱动事件订阅

两者层级完全不同。

---

## 10. STM32 GPIO 驱动：`gpio_stm32.c`

当前平台最关键的底层 GPIO 实现位于：

- `zephyr/drivers/gpio/gpio_stm32.c`

这个文件把 Zephyr 通用 GPIO API 映射到 STM32 硬件。

### 10.1 驱动里能看到什么

从源码可见，该文件包含中断和配置相关的重要函数，例如：

- `gpio_stm32_flags_to_conf`
- `gpio_stm32_config`
- `gpio_stm32_pin_interrupt_configure`
- `gpio_stm32_isr`

这几个函数分别承担的职责可以概括为：

- 把 Zephyr GPIO flags 翻译成 STM32 寄存器配置语义
- 完成引脚模式/上拉下拉/输出类型配置
- 配置 pin 的中断触发方式
- 在 ISR 中处理事件并分发 callback

### 10.2 通用 API 是如何落到 STM32 的

比如 `gpio_pin_configure_dt()` 最终会走到 GPIO 驱动的实现函数，再进一步写 STM32 GPIO 寄存器。

从应用看是统一 API；
从驱动看则是：

- 模式寄存器配置
- 输出类型配置
- pull-up / pull-down 配置
- EXTI 相关设置

这就是 Zephyr “上层统一、底层适配”的典型体现。

---

## 11. GPIO 与 EXTI / 中断系统的关系

GPIO 输入中断不是单独存在的，它与中断子系统深度耦合。

在当前构建后的设备树中，可以看到：

- `exti: interrupt-controller@40013c00`

这意味着在 STM32 上，一个 GPIO pin 的中断能力往往依赖：

- GPIO 控制器本身
- EXTI 外部中断控制器
- NVIC

因此 `gpio_pin_interrupt_configure_dt()` 在底层并不只是“设个标志”，而是会触发一整套：

- 引脚到 EXTI 线的映射
- 触发方式配置
- 中断使能
- ISR 分发准备

所以 GPIO 子系统本身其实站在：

- 设备树子系统
- 中断子系统
- STM32 pinctrl / EXTI / RCC 支撑层

这些模块的交界处。

---

## 12. 设备树绑定视角：`gpios` 属性到底是什么

在设备树中，一个常见写法是：

- `gpios = <&gpioa 5 GPIO_ACTIVE_HIGH>`

它通常遵循控制器 binding 规定的 `#gpio-cells` 语义。

对应用开发者来说，可以把它理解成：

- 第 1 个单元：GPIO 控制器
- 第 2 个单元：pin 号
- 第 3 个单元：flags

Zephyr 通过 `GPIO_DT_SPEC_GET()` 把这段设备树描述编译成 C 结构体初始化内容。

也就是说：

- 设备树不是运行时解析的字符串配置
- 它大部分会在编译期展开成 C 宏/常量

这也是 Zephyr 既保持可配置性，又不引入太多运行时开销的重要原因。

---

## 13. GPIO ready 检查为什么不能省

无论是 `blinky` 还是 `button_interrupt`，都先做：

- `gpio_is_ready_dt()`

这一步的意义是确认：

- 底层 GPIO 设备对象存在
- 驱动已经初始化
- 当前设备状态可用

从启动链角度看，这也是在验证：

- 驱动已经在正确的 init level 完成初始化

如果这里失败，通常要回头检查：

- `CONFIG_GPIO` 是否启用
- 板级设备树节点状态是否为 `okay`
- 驱动是否被编进镜像
- 相关时钟/SoC 支撑是否可用

---

## 14. 当前工程里的 GPIO 事实表

基于当前 `nucleo_f411re + blinky`，可以把核心事实总结为：

| 项目 | 当前工程实际情况 |
| --- | --- |
| 应用 sample | `f411re/blinky/src/main.c` |
| GPIO 通用 API | `zephyr/include/zephyr/drivers/gpio.h` |
| STM32 驱动 | `zephyr/drivers/gpio/gpio_stm32.c` |
| LED 别名 | `led0` |
| LED 物理引脚 | `PA5` |
| 按键别名 | `sw0` |
| 按键物理引脚 | `PC13` |
| 中断关联控制器 | `exti` + `nvic` |
| 应用输出操作 | `gpio_pin_toggle_dt()` |
| 应用中断订阅 | `gpio_pin_interrupt_configure_dt()` + `gpio_add_callback()` |

---

## 15. 调试 GPIO 子系统时建议优先看什么

如果你在当前工程中调试 GPIO，建议按这个顺序排查：

1. `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
   - 看 `led0`、`sw0`、具体 `gpios` 定义
2. `f411re/build/zephyr/zephyr.dts`
   - 看最终合并后的节点是否正确
3. `f411re/blinky/src/main.c`
   - 验证应用调用路径
4. `zephyr/samples/drivers/gpio/button_interrupt/src/main.c`
   - 参考中断回调标准用法
5. `zephyr/include/zephyr/drivers/gpio.h`
   - 确认 API 语义
6. `zephyr/drivers/gpio/gpio_stm32.c`
   - 看 STM32 底层实现

如果 LED 不亮或者按键没中断，最常见问题往往不是“GPIO API 坏了”，而是：

- alias 指向错了
- pin flags 配错了
- 节点未 `okay`
- 引脚复用/pinctrl 冲突
- EXTI 触发方式不匹配

---

## 16. 小结

Zephyr GPIO 子系统最重要的设计特点是：

- 用设备树表达板级连线
- 用 `gpio_dt_spec` 把控制器、pin、flags 绑定成统一对象
- 用通用 GPIO API 隔离上层应用和底层 SoC 差异
- 用具体驱动（如 `gpio_stm32.c`）把抽象落到硬件寄存器
- 对于中断输入，再通过 callback 机制把底层 IRQ 转成上层事件

对当前 `nucleo_f411re` 工程来说，最典型的两条学习路径就是：

- **输出路径**：`blinky` -> `led0` -> `gpio_pin_toggle_dt()` -> STM32 GPIO 驱动
- **中断路径**：`button_interrupt` -> `sw0` -> `gpio_pin_interrupt_configure_dt()` -> `gpio_add_callback()` -> EXTI/GPIO ISR -> 应用 callback

掌握这两条路径之后，GPIO 子系统在 Zephyr 中的定位就会非常清楚：

它不是“读写一个引脚”的小工具，而是连接**设备树、驱动模型、中断系统、板级抽象**的一条主干通道。