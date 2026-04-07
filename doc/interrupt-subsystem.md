# Zephyr 中断子系统实现详解

本文结合 Zephyr 官方中断文档、ARM Cortex-M 架构代码、当前 `nucleo_f411re` 的设备树与构建产物，详细说明：

- Zephyr 的中断系统整体设计是什么
- 向量表、`_sw_isr_table`、`_isr_wrapper` 分别扮演什么角色
- 驱动如何把 ISR 注册到 Zephyr 中
- 应用如何通过驱动/回调使用中断，而不是直接硬写寄存器
- 当前 STM32F411RE 上，GPIO 外部中断是怎么走到用户回调里的

本文尤其关注“**中断系统设计 + 具体 sample 路径**”两条线并行说明。

---

## 1. 先说结论：Zephyr 的中断不是单层设计

在 Cortex-M 上，Zephyr 的中断路径通常不是“外设 IRQ 直接跳到某个业务函数”这么简单，而是至少分为两层：

1. **硬件向量层**：CPU 根据向量表跳转到入口
2. **Zephyr 分发表层**：统一 wrapper 根据 IRQ 号查 `_sw_isr_table`
3. **具体 ISR / 驱动层**：实际执行某个驱动 ISR
4. **驱动回调层**：驱动再把事件转交给应用注册的 callback

所以如果把一个按键中断从硬件一路追到应用，通常路径更像：

- EXTI 线触发
- NVIC 产生 IRQ
- CPU 根据向量表进入中断入口
- `_isr_wrapper` 取 IRQ 号
- `_sw_isr_table[irq]` 找到具体 ISR
- GPIO/EXTI 驱动 ISR 读取并清中断
- 驱动调用 callback 列表
- 应用中的回调函数被执行

这也是 Zephyr 既能保持可移植性，又能支持动态 ISR、直接 ISR、驱动回调机制的原因。

---

## 2. 当前工程里最关键的中断相关文件

### 2.1 架构与公共层

- `zephyr/arch/arm/core/cortex_m/vector_table.S`
- `zephyr/arch/arm/core/cortex_m/reset.S`
- `zephyr/arch/arm/core/cortex_m/irq_manage.c`
- `zephyr/include/zephyr/irq.h`
- `zephyr/include/zephyr/sw_isr_table.h`
- `zephyr/arch/common/dynamic_isr.c`

### 2.2 当前构建产物

- `f411re/build/zephyr/isr_tables.c`
- `f411re/build/zephyr/zephyr.dts`

### 2.3 相关 sample / test

- 应用层 GPIO 中断 sample：`zephyr/samples/drivers/gpio/button_interrupt/src/main.c`
- ISR 表生成测试：`zephyr/tests/arch/common/gen_isr_table/src/main.c`

---

## 3. 硬件入口：异常向量表

向量表定义位于：

- `zephyr/arch/arm/core/cortex_m/vector_table.S`

这个文件决定 CPU 收到异常/中断后，第一跳跳到哪里。

对中断子系统来说，它至少解决两件事：

- Reset/NMI/HardFault/SVC/PendSV/SysTick 这类核心异常的入口
- 外设 IRQ 的统一入口布局

在很多 Zephyr Cortex-M 场景中，向量表中的外设 IRQ 项并不直接指向每一个业务 ISR，而是先进入统一包装入口。真正的具体 ISR 分派，往往在下一层完成。

---

## 4. 第二层：`_isr_wrapper` 与 `_sw_isr_table`

### 4.1 设计目的

Zephyr 需要支持：

- 统一 ISR 入口管理
- ISR 附带参数 `arg`
- 动态 ISR 安装
- 某些架构/场景下生成软件 ISR 表

为此，Zephyr 在公共层定义了软件 ISR 表相关结构。

### 4.2 相关头文件

- `zephyr/include/zephyr/sw_isr_table.h`

在这个头文件中，可以看到：

- `_isr_table_entry`
- `_sw_isr_table`
- `_isr_list`
- `Z_ISR_DECLARE`
- `Z_ISR_DECLARE_DIRECT`

这些对象共同支撑“构建期收集中断声明，生成最终表项，运行时再根据 IRQ 号分发”的机制。

### 4.3 当前构建已经生成了实际 ISR 表

在当前工程构建目录中，已经生成：

- `f411re/build/zephyr/isr_tables.c`

该文件中可以直接看到：

- `_irq_vector_table[42]`
- `_sw_isr_table[42]`

而且 `_irq_vector_table` 中大部分项都指向：

- `_isr_wrapper`

这说明当前工程的很多 IRQ 进入 Zephyr 后，会先落到统一 wrapper，再由 wrapper 查 `_sw_isr_table` 做二次分发。

`_sw_isr_table` 的每个条目保存：

- `arg`
- `isr`

于是 Zephyr 可以用统一机制支持：

- 同一个入口包装
- 每个 IRQ 对应不同 ISR
- 同时给 ISR 传入参数

这比“全部硬编码到汇编向量表里”灵活得多。

---

## 5. `irq.h`：Zephyr 对外提供的中断 API

中断 API 的核心头文件位于：

- `zephyr/include/zephyr/irq.h`

最值得掌握的宏/接口有：

- `IRQ_CONNECT()`
- `IRQ_DIRECT_CONNECT()`
- `irq_connect_dynamic()`
- `ISR_DIRECT_DECLARE()`
- `irq_enable()` / `irq_disable()`
- `irq_lock()` / `irq_unlock()`

这几类接口分别对应不同使用场景。

---

## 6. 三种典型中断注册方式

### 6.1 静态注册：`IRQ_CONNECT()`

这是驱动里最常见的方式。

特点：

- 编译期已知 IRQ 号
- 编译期已知优先级
- 编译期已知 ISR 函数和参数
- 构建系统可以把这些信息收集起来，生成 ISR 表

典型模式是：

- `IRQ_CONNECT(irqn, priority, isr, arg, flags)`
- 再 `irq_enable(irqn)`

一个实际例子来自：

- `zephyr/drivers/timer/stm32_lptim_timer.c`

其中可以看到：

- `IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), lptim_irq_handler, 0, 0);`
- `irq_enable(DT_INST_IRQN(0));`

这个例子非常典型，说明 Zephyr 驱动里常见的中断注册套路是：

1. 从设备树拿 IRQ 号与优先级
2. 用 `IRQ_CONNECT()` 注册 ISR
3. 用 `irq_enable()` 真正打开中断

也就是说，**驱动不是手写“向量表偏移 = xxx”**，而是通过 Zephyr 提供的抽象宏完成注册。

### 6.2 动态注册：`irq_connect_dynamic()`

当 ISR 无法在编译期完全确定，或者需要运行时安装时，可以使用：

- `irq_connect_dynamic()`

其通用实现相关文件是：

- `zephyr/arch/common/dynamic_isr.c`

这里的核心函数是：

- `z_isr_install`
- `arch_irq_connect_dynamic`

适合场景：

- 某些运行时可配置设备
- 中断处理函数/参数在运行期才确定
- 需要更灵活安装/替换 ISR

代价通常是：

- 运行期开销更高
- 需要 `CONFIG_DYNAMIC_INTERRUPTS`
- 结构上没有静态方案那么“链接期可分析”

### 6.3 直接 ISR：`IRQ_DIRECT_CONNECT()`

对极致低延迟场景，Zephyr 还提供：

- `IRQ_DIRECT_CONNECT()`
- `ISR_DIRECT_DECLARE()`

这类中断可以绕开常规 `_sw_isr_table` 分发路径的一部分逻辑，以更小开销进入处理函数。

但代价是：

- 使用限制更多
- 适合非常明确的低延迟需求
- 代码可移植性和灵活性通常不如普通 ISR 路径

因此大多数普通外设驱动和应用，不会优先使用 direct ISR。

---

## 7. 驱动如何“向 Zephyr 注册中断处理函数”

这是很多移植和驱动开发中最关键的问题。

### 7.1 一般驱动注册模式

典型驱动的 IRQ 连接过程可以抽象为：

1. 设备树为该外设声明 `interrupts`
2. 驱动在实例化阶段通过 `DT_INST_IRQN()` / `DT_INST_IRQ()` 取出 IRQ 号与优先级
3. 驱动 init 函数中调用 `IRQ_CONNECT()`
4. 驱动调用 `irq_enable()`
5. 中断发生后，Zephyr 根据 ISR 表调用该驱动 ISR
6. 驱动 ISR 再把事件转发给上层回调或内部状态机

也就是说，真正要“注册到 Zephyr”里的，通常不是你的应用业务函数，而是**驱动 ISR**。

应用更常见的做法是：

- 调驱动 API
- 向驱动注册 callback
- 由驱动在 ISR 里回调应用

这是 Zephyr 驱动模型和裸机风格最本质的区别之一。

### 7.2 为什么这样设计

好处非常明显：

- 应用不必直接碰 NVIC 寄存器
- 驱动可以统一处理中断状态位、屏蔽位、错误位
- 同一个驱动可以提供更高层 API，而不是把底层中断细节暴露给应用
- 有利于跨芯片复用同一套上层代码

---

## 8. 设备树如何参与中断系统

### 8.1 当前构建的中断控制器

在当前 `f411re/build/zephyr/zephyr.dts` 中，可以看到：

- 根节点：`interrupt-parent = <&nvic>`
- `nvic: interrupt-controller@e000e100`
- `exti: interrupt-controller@40013c00`

这说明当前系统至少包含两层关键中断相关硬件：

- **NVIC**：ARM Cortex-M 核心中断控制器
- **EXTI**：STM32 外部中断/事件控制器

### 8.2 设备树在这里到底提供了什么

设备树为中断子系统提供的，不只是“一个设备存在”，更重要的是：

- 该设备挂在哪个中断控制器下
- IRQ 号是多少
- 优先级等中断参数是多少
- 某些控制器之间的级联关系如何建立

因此驱动中常见的：

- `DT_INST_IRQN(n)`
- `DT_INST_IRQ(n, priority)`
- `DT_IRQN(node)`

本质上都是在消费设备树中断信息。

### 8.3 当前板子的按钮与 EXTI

在板级 DTS `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts` 中：

- `sw0` 对应 `gpioc 13 GPIO_ACTIVE_LOW`

这意味着用户按键接在 `PC13`。在 STM32 上，当把该引脚配置成中断输入时，实际中断路径会涉及：

- GPIO 端口配置
- EXTI 线映射
- NVIC 中对应 EXTI IRQ 的使能与处理中转

所以按钮中断虽然应用层只写了 GPIO API，但底层真正工作的仍然是完整的中断控制链。

---

## 9. STM32 GPIO 中断：从 EXTI 到应用 callback

与本主题最贴近的驱动文件是：

- `zephyr/drivers/gpio/gpio_stm32.c`

该文件中可以看到与中断直接相关的实现，例如：

- `gpio_stm32_pin_interrupt_configure`
- `gpio_stm32_isr`

这说明 STM32 GPIO 驱动在 Zephyr 中承担了两层责任：

1. 配置某个 GPIO 引脚的中断触发方式
2. 在 ISR 中把 EXTI 事件分发给注册的 GPIO callback

所以应用代码通常并不会直接 `IRQ_CONNECT()` 一个按钮 IRQ，而是：

- 配置 GPIO interrupt
- 注册 GPIO callback
- 由驱动在底层 ISR 中回调应用

这正是 Zephyr 驱动框架的标准用法。

---

## 10. 结合 sample：`button_interrupt` 看应用如何使用中断

示例文件：

- `zephyr/samples/drivers/gpio/button_interrupt/src/main.c`

这个 sample 是理解“应用怎么用 Zephyr 中断系统”的最好例子之一。

### 10.1 它没有直接碰 NVIC

sample 的主要步骤是：

1. 从设备树别名 `sw0` 获取按钮 GPIO
2. 从设备树别名 `led0` 获取 LED GPIO（若存在）
3. `gpio_pin_configure_dt()` 配置引脚方向
4. `gpio_pin_interrupt_configure_dt(button, GPIO_INT_EDGE_TO_ACTIVE)` 配置按键中断触发
5. `gpio_init_callback()` 初始化回调对象
6. `gpio_add_callback()` 把回调挂到 GPIO 驱动

也就是说，**应用没有直接注册硬件 IRQ**，而是把自己的函数注册到驱动层 callback 链表中。

### 10.2 sample 中真正的回调是什么

sample 定义了按钮回调函数，在回调里：

- 打印 `k_cycle_get_32()`
- 可选地翻转 LED

这说明在应用视角里，中断被抽象成“某个 GPIO 事件到了，我收到一个 callback”。

底层复杂度被驱动吃掉了，包括：

- EXTI 线配置
- 中断标志清除
- NVIC 相关入口
- callback 分发

### 10.3 这就是 Zephyr 推荐的应用写法

对于 GPIO、UART、SPI、I2C、CAN 等驱动，应用一般都应优先使用：

- 驱动 API
- callback / event / workqueue

而不是在应用里直接用底层 `IRQ_CONNECT()` 硬绑中断。因为前者更可移植、可维护，也更符合 Zephyr 的分层设计。

---

## 11. 结合 test：`gen_isr_table` 看构建期表生成

测试文件：

- `zephyr/tests/arch/common/gen_isr_table/src/main.c`

这个测试的价值不在“业务逻辑”，而在它能帮助理解：

- Zephyr 如何在编译/链接阶段收集中断声明
- 如何生成最终 ISR 表
- 多级中断/不同注册方式如何进入最终产物

配合当前工程真实存在的：

- `build/zephyr/isr_tables.c`

就可以把中断注册链条完整串起来：

1. 源码里通过中断宏声明 ISR
2. 构建系统收集 `.intList` 等信息
3. 生成 `_irq_vector_table` 与 `_sw_isr_table`
4. 运行时由 wrapper 按 IRQ 号分发

这一步非常重要，因为它解释了：

> 为什么你明明只写了 `IRQ_CONNECT()`，却能在最终镜像里得到正确的中断分发表。

---

## 12. `irq_manage.c`：ARM Cortex-M 上的中断底层管理

文件：

- `zephyr/arch/arm/core/cortex_m/irq_manage.c`

该文件负责 ARM NVIC 相关的底层管理逻辑，例如：

- `arm_irq_enable`
- `arm_irq_disable`
- `arm_irq_priority_set`
- `z_irq_spurious`

这里的职责不是“执行业务 ISR”，而是提供 Zephyr 架构层与 NVIC 之间的接口。

也就是说：

- `irq_enable()` 这类 API 最终要落到这里
- 默认未注册 IRQ 的兜底逻辑，也会在这里体现

当某个中断没有正确连接时，经常会落入：

- `z_irq_spurious`

这是非常重要的调试信号，意味着：

- IRQ 真的来了
- 但 Zephyr 没有为它找到有效 ISR

---

## 13. 直接 ISR、普通 ISR、驱动 callback 的职责边界

这三者很容易混淆，实际上职责完全不同：

### 13.1 直接 ISR

- 追求最低延迟
- 接近硬件入口
- 适合非常底层、性能敏感场景

### 13.2 普通 ISR

- 最常见
- 通过 Zephyr 统一机制进入
- 可带参数
- 易于与驱动模型结合

### 13.3 驱动 callback

- 不是硬件 IRQ 本身
- 是驱动在 ISR 处理完硬件状态后，向上层报告事件的方式
- 是应用层最常使用的“中断接口”

以 `button_interrupt` 为例：

- 硬件 IRQ：EXTI/NVIC
- 驱动 ISR：GPIO/EXTI 驱动中的中断处理函数
- 应用 callback：sample 里注册的 button callback

这三层不要混为一谈。

---

## 14. 当前 `nucleo_f411re` 上中断相关的实际观察点

如果你要在当前工程中调试中断，建议重点观察：

- `build/zephyr/zephyr.dts`
  - `interrupt-parent`
  - `nvic`
  - `exti`
  - `sw0`
- `build/zephyr/isr_tables.c`
  - `_irq_vector_table`
  - `_sw_isr_table`
- `zephyr/include/zephyr/irq.h`
  - 注册宏与 API
- `zephyr/include/zephyr/sw_isr_table.h`
  - 分发表结构
- `zephyr/arch/arm/core/cortex_m/irq_manage.c`
  - NVIC 底层管理
- `zephyr/drivers/gpio/gpio_stm32.c`
  - GPIO 中断配置与 callback 分发
- `zephyr/samples/drivers/gpio/button_interrupt/src/main.c`
  - 应用层标准用法

---

## 15. 驱动开发时的推荐中断接入思路

如果你要为一个新外设写 Zephyr 驱动，推荐按下面顺序思考：

1. **设备树**：先定义好该外设的 `interrupts` 属性
2. **实例化宏**：确保驱动能通过 `DT_INST_IRQN()` / `DT_INST_IRQ()` 读到 IRQ 信息
3. **初始化函数**：在 driver init 中 `IRQ_CONNECT()` 并 `irq_enable()`
4. **ISR 最小化**：ISR 里只做必要寄存器处理、状态清理、快速投递
5. **向上抽象**：通过 callback、workqueue、k_msgq、k_sem 等机制通知上层
6. **应用只调 API**：不要要求应用直接知道底层 IRQ 号

这样写出来的驱动，才真正符合 Zephyr 风格。

---

## 16. 小结

Zephyr 的中断系统可以概括为：

- **向量表**解决 CPU 第一跳问题
- **`_isr_wrapper + _sw_isr_table`** 解决 Zephyr 的统一分发问题
- **`IRQ_CONNECT()` / `irq_connect_dynamic()` / `IRQ_DIRECT_CONNECT()`** 提供不同粒度的 ISR 注册方式
- **设备树**提供 IRQ 号、优先级、控制器归属等硬件信息
- **驱动 ISR + 应用 callback** 构成了 Zephyr 推荐的使用模式

对于当前 `nucleo_f411re` 工程，最实用的理解路径是：

- 先看 `build/zephyr/isr_tables.c`，确认系统实际生成了什么
- 再看 `button_interrupt`，理解应用如何消费“中断事件”
- 再看 `gpio_stm32.c`，理解驱动如何把底层 IRQ 转成回调
- 最后结合 `irq.h` 与 `irq_manage.c`，理解 Zephyr 如何把中断抽象成统一框架

掌握这一层后，再读 GPIO、定时器、低功耗子系统时，会轻松很多，因为它们本质上都要借助这套中断骨架来完成异步事件处理。