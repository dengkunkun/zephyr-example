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
- `zephyr/soc/st/stm32/common/gpioport_mgr.c`

### 2.3 设备树、binding 与板级文件

- `zephyr-example/f411ceu6/boards/weact/f411ceu6/f411ceu6.dts`
- `zephyr/dts/arm/st/f4/stm32f411Xe.dtsi`
- `zephyr/dts/arm/st/f4/stm32f411.dtsi`
- `zephyr/dts/arm/st/f4/stm32f4.dtsi`
- `zephyr/dts/bindings/gpio/st,stm32-gpio.yaml`
- `zephyr/dts/bindings/gpio/gpio-controller.yaml`
- `zephyr/dts/bindings/led/gpio-leds.yaml`
- `zephyr/dts/bindings/input/gpio-keys.yaml`

### 2.4 板级与构建设备树

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
- `f411re/build/zephyr/zephyr.dts`

### 2.5 官方文档入口

- `zephyr/doc/hardware/peripherals/gpio.rst`
- `zephyr/doc/kernel/drivers/index.rst`
- `zephyr/doc/build/dts/howtos.rst`
- `zephyr/doc/build/dts/bindings-intro.rst`
- `zephyr/doc/build/dts/api/api.rst`

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

## 14. `f411ceu6` GPIO 驱动实现全链路

这一部分把当前工程里的 `weact,f411ceu6` 板级定义、STM32 SoC 级 GPIO 控制器节点、binding、设备模型以及驱动代码串成一条完整链路。

### 14.1 板级 DTS 如何声明 GPIO 资源

当前板级文件位于：

- `zephyr-example/f411ceu6/boards/weact/f411ceu6/f411ceu6.dts`

其中直接和 GPIO 相关的定义是：

- `user_led: led { gpios = <&gpioc 13 GPIO_ACTIVE_LOW>; }`
- `user_button: button { gpios = <&gpioa 0 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>; }`
- `aliases { led0 = &user_led; sw0 = &user_button; }`

这里要分清两层含义：

1. `leds` 节点的 `compatible = "gpio-leds"`，说明这是一个“GPIO 驱动的 LED 集合”
2. `gpio_keys` 节点的 `compatible = "gpio-keys"`，说明这是一个“GPIO 驱动的按键集合”
3. 真正的 GPIO 控制器并不在这两个节点里定义，而是通过 `&gpioc`、`&gpioa` 这两个 phandle 引到 SoC 级 GPIO 控制器节点

因此对 `f411ceu6` 来说，板级 DTS 做的事情不是“实现 GPIO 驱动”，而是：

- 选择具体使用哪个 GPIO 控制器
- 指定 pin 编号
- 指定逻辑极性和附加 flags
- 通过 alias 暴露给应用

### 14.2 GPIO 控制器节点来自 SoC 级 DTSI

`f411ceu6.dts` 先后包含：

- `stm32f411Xe.dtsi`
- `stm32f411.dtsi`
- `stm32f401.dtsi`
- `stm32f4.dtsi`

真正的 GPIO 控制器节点定义在 `zephyr/dts/arm/st/f4/stm32f4.dtsi` 的 `pinctrl` 节点下面，例如：

- `gpioa: gpio@40020000`
- `gpiob: gpio@40020400`
- `gpioc: gpio@40020800`

这些节点都有共同特征：

- `compatible = "st,stm32-gpio"`
- `gpio-controller;`
- `#gpio-cells = <2>;`
- `reg = <... 0x400>;`
- `clocks = <&rcc STM32_CLOCK(AHB1, ...)>;`

同一个文件里还定义了：

- `exti: interrupt-controller@40013c00`

这就是为什么 `f411ceu6` 上的 GPIO 输入中断最终会落到 EXTI 控制器上，而不是只停留在 GPIO 端口本身。

### 14.3 binding 如何约束这些节点

GPIO 控制器的 binding 位于：

- `zephyr/dts/bindings/gpio/st,stm32-gpio.yaml`

这里明确了几件关键事情：

- `compatible` 必须是 `st,stm32-gpio`
- `reg` 是必需属性
- `clocks` 是必需属性
- `#gpio-cells` 固定为 `2`
- `gpio-cells` 的语义是 `pin` 和 `flags`

这意味着像下面这样的板级写法：

- `gpios = <&gpioc 13 GPIO_ACTIVE_LOW>`

在编译系统看来不是“任意三元组”，而是一个严格受 binding 约束的 phandle-array：

1. `&gpioc` 指向 `st,stm32-gpio` 控制器节点
2. `13` 对应 `pin`
3. `GPIO_ACTIVE_LOW` 对应 `flags`

与此同时，板级 `leds` 和 `gpio_keys` 节点也各自有 binding：

- `zephyr/dts/bindings/led/gpio-leds.yaml`
- `zephyr/dts/bindings/input/gpio-keys.yaml`

它们的 child-binding 又约束了子节点里必须出现 `gpios`，而 `gpio-keys` 还要求按键事件使用 `zephyr,code` 表达输入语义。

因此，`f411ceu6` 的 GPIO 描述其实同时跨了三层 binding：

1. `gpio-leds` / `gpio-keys` 约束“这个节点是什么设备”
2. `st,stm32-gpio` 约束“GPIO 控制器长什么样”
3. `gpio-controller.yaml` 约束“GPIO 控制器作为通用 phandle provider 应该具备什么共性”

### 14.4 从设备树到 `gpio_dt_spec`

官方 GPIO 文档 `zephyr/doc/hardware/peripherals/gpio.rst` 明确推荐用 `gpio_dt_spec` 表达一个具体 GPIO 资源。

对于 `f411ceu6`，典型应用代码仍然是：

1. `#define LED0_NODE DT_ALIAS(led0)`
2. `static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);`

而 `zephyr/include/zephyr/drivers/gpio.h` 里可以看到，`GPIO_DT_SPEC_GET()` 最终会展开成：

- `.port = DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(...))`
- `.pin = DT_GPIO_PIN_BY_IDX(...)`
- `.dt_flags = DT_GPIO_FLAGS_BY_IDX(...)`

这说明它在编译期就已经把三件事固定下来：

- 控制器设备对象是谁
- 使用哪个 pin
- 设备树里声明了哪些 flags

这也是为什么从代码访问设备树时，推荐优先用：

- `DT_ALIAS()` / `DT_NODELABEL()` 定位节点
- `GPIO_DT_SPEC_GET()` 获取 GPIO 资源
- `gpio_is_ready_dt()` 做可用性检查

而不是自己去拆散 `port + pin + flags` 三个字段。

### 14.5 Zephyr 设备驱动模型如何把 GPIO 节点变成 `struct device`

`zephyr/doc/kernel/drivers/index.rst` 对设备驱动模型的核心描述是：每个设备最终都会有一个 `struct device`，其中最关键的三部分是：

- `config`：只读配置
- `data`：运行期状态
- `api`：该驱动向子系统暴露的函数表

STM32 GPIO 在这里有一个很容易忽略的实现细节：

- `zephyr/drivers/gpio/gpio_stm32.c` 只定义了 `DEVICE_API(gpio, gpio_stm32_driver)`
- 真正把 `gpioa`、`gpiob`、`gpioc` 等节点实例化成设备的是 `zephyr/soc/st/stm32/common/gpioport_mgr.c`

`gpioport_mgr.c` 里的 `GPIO_PORT_DEVICE_INIT()` 宏会为每个 `status = "okay"` 的 GPIO 端口节点生成：

- `gpio_stm32_config`
- `gpio_stm32_data`
- `PM_DEVICE_DT_DEFINE(...)`
- `DEVICE_DT_DEFINE(...)`

这里填进去的关键信息包括：

- `base`：GPIO 端口寄存器基地址
- `port`：端口编号
- `pclken`：时钟门控信息
- `port_pin_mask`：支持的 pin mask
- `api = &gpio_stm32_driver`

这一点和官方文档 `zephyr/doc/build/dts/howtos.rst` 里提到的两种“devicetree-aware driver”实例化模式高度一致：

1. 通用外设常用 `DT_DRV_COMPAT + DEVICE_DT_INST_DEFINE()`
2. SoC 专用外设也可以走 `DT_NODELABEL + DEVICE_DT_DEFINE()`

STM32 GPIO 显然更接近第二种，因为 `gpioa`、`gpiob`、`gpioc` 这些端口本身就是 SoC 固定外设实例。

### 14.6 `gpio_stm32.c` 如何把通用 GPIO API 落到硬件

`zephyr/drivers/gpio/gpio_stm32.c` 主要负责“子系统 API 到 STM32 端口操作”的映射。

最关键的路径包括：

1. `gpio_stm32_config()`
   - 调 `gpio_stm32_flags_to_conf()` 把 Zephyr flags 翻译成 STM32 pin 配置语义
   - 需要时通过 runtime PM 打开 GPIO 端口时钟
   - 设置初始输出电平
   - 调 `stm32_gpioport_configure_pin()` 真正写寄存器
2. `gpio_stm32_port_set_bits_raw()` / `clear_bits_raw()` / `toggle_bits()`
   - 对应 STM32 的端口置位、清位、翻转操作
3. `gpio_stm32_port_get_raw()`
   - 读取输入端口状态

而真正写 STM32 `MODER`、`OTYPER`、`OSPEEDR`、`PUPDR`、`AFR` 等寄存器的逻辑，放在：

- `zephyr/soc/st/stm32/common/gpioport_mgr.c`

也就是说，STM32 GPIO 这里实际上分成了两层：

1. `gpio_stm32.c` 负责实现 Zephyr GPIO 子系统 API
2. `gpioport_mgr.c` 负责端口设备实例化和更底层的寄存器配置辅助

### 14.7 `f411ceu6` 的 GPIO 中断路径

如果 `f411ceu6` 上对 `PA0` 按键配置中断，路径会是：

1. 应用调用 `gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE)`
2. `gpio.h` 把它转成 `gpio_pin_interrupt_configure(button.port, button.pin, ...)`
3. 设备 API 分发到 `gpio_stm32_pin_interrupt_configure()`
4. 驱动根据 `mode` / `trig` 选择 `rising`、`falling` 或 `both`
5. 驱动调用 `stm32_gpio_intc_set_irq_callback()` 绑定 `gpio_stm32_isr`
6. `stm32_exti_set_line_src_port(pin, cfg->port)` 把 EXTI line 映射到具体 GPIO 端口
7. EXTI 触发后进入 `gpio_stm32_isr()`，再由 `gpio_fire_callbacks()` 分发到应用注册的 callback

因此对 `f411ceu6` 来说，按键中断并不是“板级节点自己会触发回调”，而是：

- 板级设备树只描述 `PA0 + ACTIVE_LOW + PULL_UP`
- GPIO 驱动把 pin 中断语义翻译成 EXTI 配置
- EXTI/NVIC 负责真正的硬件中断入口
- GPIO callback 机制负责把硬件事件分发回应用

### 14.8 对当前 `f411ceu6` 工程的直接结论

基于当前代码，可以把这块板子的 GPIO 关键事实总结为：

- 用户 LED 走 `PC13`，并且是 `GPIO_ACTIVE_LOW`
- 用户按键走 `PA0`，并且带 `GPIO_PULL_UP | GPIO_ACTIVE_LOW`
- 应用层最稳定的入口仍然是 `led0` / `sw0` alias
- GPIO 控制器节点来自 `stm32f4.dtsi`，不是在板级 DTS 里重新定义
- GPIO binding 是 `st,stm32-gpio.yaml`
- GPIO 端口设备实例化代码在 `gpioport_mgr.c`
- GPIO 子系统 API 实现代码在 `gpio_stm32.c`
- 输入中断最终依赖 `exti: interrupt-controller@40013c00`

---

## 15. 结合官方文档理解 GPIO 驱动开发的完整逻辑

如果把 `zephyr/doc` 里的相关文档串起来，Zephyr GPIO 驱动开发的完整逻辑可以概括成下面这条主线。

### 15.1 第一步：先定义硬件描述，而不是先写寄存器代码

从 `zephyr/doc/build/dts/bindings-intro.rst` 可以看到，binding 的职责有两个：

1. 校验 devicetree 节点是否合法
2. 为 C 代码生成可访问这些属性的宏

所以一个标准的驱动开发流程，第一步通常不是直接写 `driver.c`，而是先明确：

- 这个设备在设备树里的 `compatible` 是什么
- 它有哪些属性
- 哪些属性是必需的
- 它是否作为 phandle provider 暴露给其它节点

对于 GPIO 控制器，这一步就体现在：

- `st,stm32-gpio.yaml`
- `gpio-controller.yaml`

如果你将来开发的是一个新的 GPIO 控制器驱动，这两类 binding 设计就是第一道关。

### 15.2 第二步：在 SoC/board DTS 里把硬件节点放对位置

`zephyr/doc/build/dts/howtos.rst` 强调，最终调试应优先看：

- 构建产物里的 `zephyr.dts`
- `devicetree_generated.h`

原因很简单：驱动最终消费的是“合并后的最终设备树”，不是单个源 DTS 文件。

因此驱动开发时至少要确认：

- SoC 级 `.dtsi` 是否定义了控制器节点
- 节点 `status` 是否为 `okay`
- `reg`、`interrupts`、`clocks`、`resets`、`pinctrl` 等关键属性是否齐全
- 板级 `.dts` / overlay 是否正确引用了这些节点

对于 `f411ceu6`，GPIO 控制器本身在 SoC 级 DTSI；板级 DTS 只是消费它们。

### 15.3 第三步：驱动代码通过设备树 API 读取配置

`zephyr/doc/build/dts/api/api.rst` 说明了几类最核心的 devicetree API：

- 通用定位节点：`DT_PATH()`、`DT_NODELABEL()`、`DT_ALIAS()`
- 驱动内部按实例访问：`DT_DRV_INST()`、`DT_INST_PROP()`
- 设备对象获取：`DEVICE_DT_GET()`

这意味着代码里访问设备树不是“解析文本”，而是“使用编译期宏读取已生成的常量”。

对应用代码：

- 常用 `DT_ALIAS(led0)`
- 常用 `GPIO_DT_SPEC_GET()`

对驱动代码：

- 常用 `DT_DRV_COMPAT`
- 常用 `DT_INST_PROP()`、`DT_INST_REG_ADDR()`
- 常用 `DT_INST_FOREACH_STATUS_OKAY()` 或 `DEVICE_DT_DEFINE()`

### 15.4 第四步：把 `config`、`data`、`api` 组织成 Zephyr 设备模型

`zephyr/doc/kernel/drivers/index.rst` 说明了设备模型的核心分工：

- `config` 保存只读硬件配置
- `data` 保存运行期状态
- `api` 暴露子系统回调表

对一个 GPIO 驱动来说，通常会有：

- `config`：基地址、时钟、支持的 pin mask、中断资源
- `data`：callback 链表、状态位、运行期同步对象
- `api`：`pin_configure`、`port_get_raw`、`port_set_bits_raw`、`pin_interrupt_configure` 等

STM32 GPIO 正是这种经典写法：

- `gpio_stm32_driver` 是 `api`
- `gpio_stm32_config` 是 `config`
- `gpio_stm32_data` 是 `data`

### 15.5 第五步：决定用哪种方式实例化设备

`zephyr/doc/build/dts/howtos.rst` 给出了两条典型路径：

1. `DT_DRV_COMPAT + DEVICE_DT_INST_DEFINE()`
   - 适合多个实例结构完全一致、适合按 instance 编号遍历的驱动
2. `DT_NODELABEL + DEVICE_DT_DEFINE()`
   - 适合 SoC 固定外设、按节点标签逐个实例化的驱动

STM32 GPIO 使用的就是第二类思路，只是把重复代码封装进了 `gpioport_mgr.c` 宏里。

这点对后续写驱动非常重要，因为很多人会误以为“所有驱动都必须 `DEVICE_DT_INST_DEFINE()`”。实际上不是，关键取决于：

- 外设实例是否适合按 compatible 统一遍历
- 是否需要依赖 SoC 固定 node label
- 是否需要按 SoC 具体端口做特殊化处理

### 15.6 第六步：应用侧优先使用设备树感知 API

`zephyr/doc/hardware/peripherals/gpio.rst` 明确建议应用优先围绕 `gpio_dt_spec` 工作。

对应到 GPIO 子系统，推荐调用链就是：

1. `GPIO_DT_SPEC_GET()`
2. `gpio_is_ready_dt()`
3. `gpio_pin_configure_dt()`
4. `gpio_pin_set_dt()` / `gpio_pin_toggle_dt()`
5. `gpio_pin_interrupt_configure_dt()`
6. `gpio_add_callback_dt()` 或 `gpio_add_callback()`

这样写的收益是：

- 板级迁移时代码基本不动
- `ACTIVE_LOW/HIGH` 语义能被保留下来
- 应用层无需关心底层控制器到底是 STM32、NXP 还是 Nordic

### 15.7 第七步：调试时按“binding -> DTS -> generated header -> device -> driver”顺序排查

官方文档虽然分散在多个章节里，但合起来后，一个很实用的排查顺序是：

1. 看 binding 是否定义正确
2. 看 SoC/board DTS 是否正确引用该 binding
3. 看 `zephyr.dts` 和 `devicetree_generated.h` 是否生成了预期节点和宏
4. 看驱动是否把对应节点实例化成了 `struct device`
5. 看 `device_is_ready()` / `gpio_is_ready_dt()` 是否成立
6. 再看驱动 API 实现和底层寄存器配置是否正确

这条链路正是当前 `f411ceu6` GPIO 实现能够正常工作的根本原因。

---

## 16. 当前工程里的 GPIO 事实表

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

## 17. 调试 GPIO 子系统时建议优先看什么

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

## 18. 小结

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

# 代码梳理

## 官方gpio demo(LL接口)

这里选用 STM32CubeF4 中最接近 GPIO 基本操作路径的 LL 工程作为参照：

- `STM32CubeF4/Projects/STM32F411RE-Nucleo/Examples_LL/WWDG/WWDG_RefreshUntilUserEvent/Inc/main.h`
- `STM32CubeF4/Projects/STM32F411RE-Nucleo/Examples_LL/WWDG/WWDG_RefreshUntilUserEvent/Src/main.c`
- `STM32CubeF4/Projects/STM32F411RE-Nucleo/Examples_LL/WWDG/WWDG_RefreshUntilUserEvent/Src/stm32f4xx_it.c`

这个工程的主目标不是专门演示 GPIO，但其中完整包含了 STM32 LL 风格的 LED 输出与按键 EXTI 输入路径，足以作为“裸 STM32 GPIO 编程模型”的参考。

### 代码组织

官方 LL 工程将 GPIO 相关信息拆成三层：

1. `main.h`
   - 定义硬件常量和板级宏
   - 例如 `LED2_PIN = LL_GPIO_PIN_5`、`LED2_GPIO_PORT = GPIOA`
   - 例如 `USER_BUTTON_PIN = LL_GPIO_PIN_13`、`USER_BUTTON_GPIO_PORT = GPIOC`
   - 同时定义 EXTI 线、IRQ 号和 SYSCFG 复用映射宏
2. `main.c`
   - 完成 GPIO 时钟开启、模式配置、拉电阻配置和 NVIC 配置
   - 提供 `LED_Init()`、`LED_On()`、`UserButton_Init()`、`UserButton_Callback()`
3. `stm32f4xx_it.c`
   - 提供 `EXTI15_10_IRQHandler`
   - 在中断服务函数中读取并清除 EXTI flag，再回调业务函数

### 输出路径

LED 初始化代码体现了 STM32 LL 接口的典型写法：

1. 开启 GPIO 端口时钟
   - `LED2_GPIO_CLK_ENABLE()`
2. 配置引脚模式
   - `LL_GPIO_SetPinMode(LED2_GPIO_PORT, LED2_PIN, LL_GPIO_MODE_OUTPUT)`
3. 输出电平控制
   - `LL_GPIO_SetOutputPin()`
   - `LL_GPIO_ResetOutputPin()`
   - `LL_GPIO_TogglePin()`

这条路径的核心特征是：

- 板级引脚号直接写在宏里
- 时钟、端口、pin、输出模式都由应用自行管理
- API 直接面向 STM32 LL 外设层，没有统一的跨平台抽象

### 输入中断路径

按键初始化代码体现了 STM32 传统 EXTI 配置链路：

1. 开启 GPIOC 时钟
2. 将 `PC13` 配置为输入
3. 通过 `LL_SYSCFG_SetEXTISource()` 把 `EXTI13` 映射到 `GPIOC`
4. 通过 `LL_EXTI_EnableIT_0_31()` 和 `LL_EXTI_EnableFallingTrig_0_31()` 使能中断与下降沿触发
5. 在 NVIC 中使能 `EXTI15_10_IRQn`
6. 在 `EXTI15_10_IRQHandler()` 中检查 flag、清 flag、调用 `UserButton_Callback()`

这条路径说明，在 STM32 LL 模型下，GPIO 输入中断至少同时涉及：

- GPIO 端口输入配置
- SYSCFG 的 EXTI 源选择
- EXTI 控制器触发方式配置
- NVIC 中断使能
- 中断服务函数编写

### 直接结论

STM32CubeF4 的 LL 示例呈现的是“应用直接控制硬件”的开发模式。其优点是路径直接、寄存器语义清晰；代价是：

- 代码与具体芯片和板级连线强耦合
- GPIO、EXTI、NVIC、SYSCFG 的职责需要由应用自己协调
- 更换板子时需要同步修改宏定义、端口时钟、引脚号和中断映射

这一点正好构成理解 Zephyr GPIO 子系统的对照组。

## zephyr驱动实现

### 设备树

Zephyr 中 GPIO 的第一入口不是驱动源文件，而是设备树。

对于当前工程，相关文件可以按层次分为四级：

1. 板级 DTS
   - `zephyr-example/f411ceu6/boards/weact/f411ceu6/f411ceu6.dts`
2. SoC 容量与封装级 DTSI
   - `zephyr/dts/arm/st/f4/stm32f411Xe.dtsi`
   - `zephyr/dts/arm/st/f4/stm32f411.dtsi`
3. SoC 公共外设级 DTSI
   - `zephyr/dts/arm/st/f4/stm32f4.dtsi`
4. 构建产物
   - `build/zephyr/zephyr.dts`
   - `build/zephyr/include/generated/zephyr/devicetree_generated.h`

在 `f411ceu6.dts` 中，GPIO 资源的声明方式不是直接定义控制器，而是消费 SoC 级控制器节点：

- `user_led: led { gpios = <&gpioc 13 GPIO_ACTIVE_LOW>; }`
- `user_button: button { gpios = <&gpioa 0 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>; }`
- `aliases { led0 = &user_led; sw0 = &user_button; }`

这里的 `&gpioc` 与 `&gpioa` 都不是本地节点，而是从 `stm32f4.dtsi` 继承而来。真正的控制器定义位于 `pinctrl` 节点下，例如：

- `gpioa: gpio@40020000`
- `gpioc: gpio@40020800`

这些节点共同具备：

- `compatible = "st,stm32-gpio"`
- `gpio-controller;`
- `#gpio-cells = <2>;`
- `reg = <...>`
- `clocks = <&rcc ...>`

同一份 `stm32f4.dtsi` 还定义了：

- `exti: interrupt-controller@40013c00`

因此，设备树层面已经把 GPIO 相关的三个核心对象建立完成：

1. GPIO 控制器节点
2. GPIO 消费者节点
3. EXTI 中断控制器节点

这与 STM32CubeF4 中由应用手工维护的“端口、pin、EXTI line、IRQ 号”形成明确对比。

### 设备树绑定

设备树节点只有在 binding 存在并匹配时，才会获得严格的属性语义和生成宏支持。

当前 GPIO 路径涉及三类 binding：

1. GPIO 控制器 binding
   - `zephyr/dts/bindings/gpio/st,stm32-gpio.yaml`
2. GPIO 控制器公共约束
   - `zephyr/dts/bindings/gpio/gpio-controller.yaml`
3. GPIO 消费者 binding
   - `zephyr/dts/bindings/led/gpio-leds.yaml`
   - `zephyr/dts/bindings/input/gpio-keys.yaml`

`st,stm32-gpio.yaml` 规定了 STM32 GPIO 控制器节点至少需要：

- `reg`
- `clocks`
- `#gpio-cells = <2>`

并且明确 `gpio-cells` 的语义为：

1. `pin`
2. `flags`

因此：

- `gpios = <&gpioc 13 GPIO_ACTIVE_LOW>`

在 Zephyr 编译系统中会被严格解释为：

1. 控制器节点是 `gpioc`
2. 目标引脚是 `13`
3. 标志位是 `GPIO_ACTIVE_LOW`

`gpio-leds.yaml` 和 `gpio-keys.yaml` 则约束了消费者节点的结构。前者要求子节点提供 `gpios` 属性；后者除了 `gpios` 之外，还要求使用 `zephyr,code` 描述输入事件语义。

这一层的作用不是驱动硬件，而是把“板级连接关系”转换成“可验证、可生成宏、可被驱动消费的编译期数据”。

### 设备驱动模型

Zephyr 设备驱动模型的核心对象是 `struct device`。从 GPIO 视角看，至少需要三个组成部分：

1. `config`
   - 保存只读配置，如基地址、端口号、时钟信息、支持的 pin mask
2. `data`
   - 保存运行期状态，如 callback 链表、引脚时钟使用状态
3. `api`
   - 保存子系统接口函数表，如配置、读写、中断配置函数

STM32 GPIO 的实现分成两层：

1. `zephyr/drivers/gpio/gpio_stm32.c`
   - 实现 GPIO 子系统 API
   - 导出 `gpio_stm32_driver`
2. `zephyr/soc/st/stm32/common/gpioport_mgr.c`
   - 基于设备树节点实例化 `gpioa`、`gpiob`、`gpioc` 等端口设备
   - 通过 `DEVICE_DT_DEFINE()` 创建 `struct device`

这一点是 STM32 GPIO 驱动实现中的关键特征。`gpio_stm32.c` 只定义行为，不负责逐端口创建设备实例；端口实例化逻辑集中在 `gpioport_mgr.c` 的宏展开中完成。

`gpioport_mgr.c` 中的 `GPIO_PORT_DEVICE_INIT()` 宏会为每个 `status = "okay"` 的 GPIO 端口节点生成：

- `gpio_stm32_config`
- `gpio_stm32_data`
- `PM_DEVICE_DT_DEFINE(...)`
- `DEVICE_DT_DEFINE(...)`

最终结果是：

- 设备树中的 `gpioa`、`gpioc` 节点变成 Zephyr 运行期可访问的 `struct device`
- 应用和其他驱动不再直接使用寄存器地址，而是通过设备对象进入 GPIO 子系统 API

这正是 Zephyr 与 STM32CubeF4 LL 模型的根本区别。前者通过设备模型统一入口，后者由应用直接组织外设访问。

### 驱动代码实现

#### 从设备树到 `gpio_dt_spec`

应用侧访问 GPIO 的推荐方式是：

1. 使用 `DT_ALIAS()` 或 `DT_NODELABEL()` 定位节点
2. 使用 `GPIO_DT_SPEC_GET()` 提取 GPIO 资源
3. 使用 `gpio_is_ready_dt()` 检查设备状态
4. 使用 `gpio_pin_configure_dt()`、`gpio_pin_set_dt()`、`gpio_pin_toggle_dt()`、`gpio_pin_interrupt_configure_dt()` 操作引脚

`GPIO_DT_SPEC_GET()` 的展开逻辑在 `zephyr/include/zephyr/drivers/gpio.h` 与 `zephyr/include/zephyr/devicetree/gpio.h` 中可以直接看到，本质上会得到三个编译期常量：

- `.port = DEVICE_DT_GET(...)`
- `.pin = DT_GPIO_PIN_BY_IDX(...)`
- `.dt_flags = DT_GPIO_FLAGS_BY_IDX(...)`

这意味着 `gpios = <&gpioa 0 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>` 不会在运行时被解析，而是在编译期展开为设备对象、pin 编号和 flags 常量。

#### GPIO 配置路径

`gpio_stm32.c` 中与配置最相关的入口是：

- `gpio_stm32_flags_to_conf()`
- `gpio_stm32_config()`

执行顺序可以概括为：

1. 将 Zephyr GPIO flags 翻译为 STM32 引脚配置语义
2. 必要时通过 runtime PM 打开 GPIO 端口时钟
3. 根据初始输出标志设置输出电平
4. 调用 `stm32_gpioport_configure_pin()` 下沉到端口配置层

真正涉及 `MODER`、`OTYPER`、`OSPEEDR`、`PUPDR`、`AFR` 等寄存器写入的代码位于 `gpioport_mgr.c` 的 `stm32_gpioport_configure_pin()`。

因此，GPIO 配置路径可以整理为：

1. 应用调用 `gpio_pin_configure_dt()`
2. GPIO 子系统调用 `gpio_pin_configure()`
3. 设备 API 分发到 `gpio_stm32_config()`
4. 端口管理层执行 `stm32_gpioport_configure_pin()`
5. 最终写入 STM32 GPIO 寄存器

#### GPIO 读写路径

输出与输入访问由 `gpio_stm32_driver` 函数表承接，关键实现包括：

- `gpio_stm32_port_get_raw()`
- `gpio_stm32_port_set_masked_raw()`
- `gpio_stm32_port_set_bits_raw()`
- `gpio_stm32_port_clear_bits_raw()`
- `gpio_stm32_port_toggle_bits()`

这些函数分别映射到 STM32 端口输入寄存器、输出寄存器、BSRR/BRR 等硬件机制。对应用而言，仍然只表现为统一的 GPIO API。

#### GPIO 中断路径

中断配置入口是：

- `gpio_stm32_pin_interrupt_configure()`

其主要过程如下：

1. 根据 `mode` 和 `trig` 选择上升沿、下降沿或双边沿触发
2. 通过 `stm32_gpio_intc_set_irq_callback()` 绑定 `gpio_stm32_isr()`
3. 通过 `stm32_exti_set_line_src_port()` 建立 GPIO 端口到 EXTI line 的映射
4. 调用 `stm32_gpio_intc_select_line_trigger()` 配置触发方式
5. 调用 `stm32_gpio_intc_enable_line()` 使能该中断线

当中断到来时：

1. 硬件先进入 EXTI/NVIC 路径
2. STM32 GPIO 中断控制层回调 `gpio_stm32_isr()`
3. `gpio_stm32_isr()` 调用 `gpio_fire_callbacks()`
4. 已注册的 `gpio_callback` 被逐个分发

这一机制与 STM32CubeF4 的 `EXTI15_10_IRQHandler()` 相比，多出了一层 GPIO 子系统事件分发。该层的作用是把“单片机中断入口”转换为“Zephyr GPIO 事件回调接口”。

#### 实现层次总结

按照代码职责划分，Zephyr 的 STM32 GPIO 实现可以总结为四层：

1. 设备树层
   - 描述控制器、消费者、flags、alias 和 EXTI 控制器
2. 宏展开层
   - 将 `gpios` 属性展开成设备对象、pin、flags
3. 设备模型层
   - 将 GPIO 端口节点实例化为 `struct device`
4. 驱动执行层
   - 将统一 GPIO API 映射到 STM32 端口配置、中断配置和寄存器读写

---

## 补充一：应用层 API 与驱动层接口的调用关系

### 核心机制：vtable 分发

Zephyr 的外设子系统采用经典的**函数指针表（vtable）多态**设计。以 GPIO 为例：

1. **子系统头文件** `zephyr/include/zephyr/drivers/gpio.h` 定义了 `struct gpio_driver_api`，即驱动必须实现的函数指针表
2. **应用层 API**（如 `gpio_pin_configure()`）是 `__syscall` 声明 + `z_impl_*` 内联函数，内部做通用逻辑（如 `GPIO_ACTIVE_LOW` 翻转），然后通过 `api->fn()` 分发到具体驱动
3. **驱动实现**填充该函数指针表，通过 `DEVICE_API(gpio, my_driver)` 宏注册

### `gpio_driver_api` 完整定义

```c
__subsystem struct gpio_driver_api {
    int (*pin_configure)(const struct device *port, gpio_pin_t pin,
                         gpio_flags_t flags);
#ifdef CONFIG_GPIO_GET_CONFIG
    int (*pin_get_config)(const struct device *port, gpio_pin_t pin,
                          gpio_flags_t *flags);
#endif
    int (*port_get_raw)(const struct device *port, gpio_port_value_t *value);
    int (*port_set_masked_raw)(const struct device *port,
                               gpio_port_pins_t mask, gpio_port_value_t value);
    int (*port_set_bits_raw)(const struct device *port, gpio_port_pins_t pins);
    int (*port_clear_bits_raw)(const struct device *port, gpio_port_pins_t pins);
    int (*port_toggle_bits)(const struct device *port, gpio_port_pins_t pins);
    int (*pin_interrupt_configure)(const struct device *port, gpio_pin_t pin,
                                   enum gpio_int_mode mode, enum gpio_int_trig trig);
    int (*manage_callback)(const struct device *port,
                           struct gpio_callback *cb, bool set);
    uint32_t (*get_pending_int)(const struct device *dev);
#ifdef CONFIG_GPIO_GET_DIRECTION
    int (*port_get_direction)(const struct device *port, gpio_port_pins_t map,
                              gpio_port_pins_t *inputs, gpio_port_pins_t *outputs);
#endif
};
```

### 应用 API → 驱动实现的调用链

以 `gpio_pin_configure()` 为典型例子：

```
应用调用 gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE)
  └─ gpio_pin_configure(led.port, led.pin, flags | led.dt_flags)     [gpio.h 内联]
       └─ z_impl_gpio_pin_configure(port, pin, flags)                [gpio.h 内联]
            ├─ 处理 GPIO_ACTIVE_LOW 翻转，更新 data->invert
            ├─ 处理 GPIO_OUTPUT_INIT_LOGICAL 语义
            └─ api->pin_configure(port, pin, flags)                  [vtable 分发]
                 └─ gpio_stm32_config(dev, pin, flags)               [gpio_stm32.c]
                      ├─ gpio_stm32_flags_to_conf(flags, &pincfg)
                      ├─ pm_device_runtime_get(dev)  // 开时钟
                      ├─ gpio_stm32_port_set_bits_raw() 或 clear_bits_raw()
                      └─ stm32_gpioport_configure_pin(dev, pin, pincfg, 0)
                           └─ 写 STM32 MODER/OTYPER/OSPEEDR/PUPDR/AFR 寄存器
```

类似地，完整对照表如下：

| 应用层 API（gpio.h） | z_impl 通用逻辑 | 驱动层接口（gpio_driver_api） | STM32 实现（gpio_stm32.c） |
|---|---|---|---|
| `gpio_pin_configure()` | ACTIVE_LOW 翻转, invert 位管理 | `pin_configure()` | `gpio_stm32_config()` |
| `gpio_pin_get_config()` | — | `pin_get_config()` | `gpio_stm32_get_config()` |
| `gpio_port_get_raw()` | — | `port_get_raw()` | `gpio_stm32_port_get_raw()` |
| `gpio_port_set_masked_raw()` | — | `port_set_masked_raw()` | `gpio_stm32_port_set_masked_raw()` |
| `gpio_port_set_bits_raw()` | — | `port_set_bits_raw()` | `gpio_stm32_port_set_bits_raw()` |
| `gpio_port_clear_bits_raw()` | — | `port_clear_bits_raw()` | `gpio_stm32_port_clear_bits_raw()` |
| `gpio_port_toggle_bits()` | — | `port_toggle_bits()` | `gpio_stm32_port_toggle_bits()` |
| `gpio_pin_interrupt_configure()` | ACTIVE_LOW 翻转中断触发方向 | `pin_interrupt_configure()` | `gpio_stm32_pin_interrupt_configure()` |
| `gpio_add_callback()` | — | `manage_callback()` | `gpio_stm32_manage_callback()` |
| `gpio_get_pending_int()` | — | `get_pending_int()` | 未实现（可选） |
| `gpio_port_get_direction()` | — | `port_get_direction()` | 未实现（可选） |

**关键点：** 应用层的便捷函数（如 `gpio_pin_set()`、`gpio_pin_toggle()`、`gpio_pin_get()`）都是在 `gpio.h` 中基于 port 级 raw 函数组合实现的，不会产生额外的驱动层接口。

### STM32 GPIO 的 `DEVICE_API` 注册

```c
DEVICE_API(gpio, gpio_stm32_driver) = {
    .pin_configure           = gpio_stm32_config,
    .pin_get_config          = gpio_stm32_get_config,        // 条件编译
    .port_get_raw            = gpio_stm32_port_get_raw,
    .port_set_masked_raw     = gpio_stm32_port_set_masked_raw,
    .port_set_bits_raw       = gpio_stm32_port_set_bits_raw,
    .port_clear_bits_raw     = gpio_stm32_port_clear_bits_raw,
    .port_toggle_bits        = gpio_stm32_port_toggle_bits,
    .pin_interrupt_configure = gpio_stm32_pin_interrupt_configure,
    .manage_callback         = gpio_stm32_manage_callback,
};
```

`DEVICE_API(gpio, name)` 宏展开为 `const STRUCT_SECTION_ITERABLE(gpio_driver_api, name)`，将函数表放入专用 linker section，支持运行时类型检查 `DEVICE_API_IS(gpio, dev)`。

---

## 补充二：`FOR_EACH_IDX` 与 `DT_INST_FOREACH_STATUS_OKAY` 的区别

### 问题本质

不同驱动使用不同的设备实例化宏，这不是随意选择，而是由硬件和驱动架构决定的。Zephyr 提供两条主要实例化路径：

### 路径一：标准路径 — `DT_INST_FOREACH_STATUS_OKAY`

这是 Zephyr 官方文档推荐的"标准"驱动实例化方式。

```c
#define DT_DRV_COMPAT vendor_device_name

#define MY_DEVICE_INIT(inst)                                          \
    static const struct my_config my_cfg_##inst = {                   \
        .base = DT_INST_REG_ADDR(inst),                               \
        .clock = DT_INST_PROP(inst, clock_frequency),                 \
    };                                                                \
    static struct my_data my_data_##inst;                             \
    DEVICE_DT_INST_DEFINE(inst, my_init, NULL,                        \
        &my_data_##inst, &my_cfg_##inst,                              \
        PRE_KERNEL_1, CONFIG_MY_INIT_PRIORITY, &my_api);

DT_INST_FOREACH_STATUS_OKAY(MY_DEVICE_INIT)
```

**适用场景：**
- 所有实例来自同一个 `compatible`
- 每个实例的结构完全一致，只有配置参数不同
- 驱动不关心实例是 `gpioa` 还是 `gpiob`，只关心 instance index

**典型用户：** SAM GPIO、NPCX GPIO、大多数 I2C/SPI/UART 驱动

例如 `gpio_sam.c`：

```c
#define GPIO_SAM_INIT(n)                                              \
    static const struct gpio_sam_config port_##n##_sam_config = {     \
        .common = GPIO_COMMON_CONFIG_FROM_DT_INST(n),                 \
        .regs = (Pio *)DT_INST_REG_ADDR(n),                          \
        .clock_cfg = SAM_DT_INST_CLOCK_PMC_CFG(n),                   \
    };                                                                \
    DEVICE_DT_INST_DEFINE(n, gpio_sam_init, NULL, ...);

DT_INST_FOREACH_STATUS_OKAY(GPIO_SAM_INIT)
```

### 路径二：STM32 GPIO 使用的 `FOR_EACH_IDX` + `DT_NODELABEL`

STM32 GPIO 没有使用 `DT_INST_FOREACH_STATUS_OKAY`，而是通过 `FOR_EACH_IDX` 遍历一个预定义的端口字母列表（a~z），对每个字母检查 `DT_NODELABEL(gpio##suffix)` 是否 okay：

```c
#define STM32_GPIO_PORTS_LIST_LWR \
    a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z

#define GPIO_PORT_DEVICE_INIT_STM32_IF_OKAY(__suffix, __SUFFIX)       \
    IF_ENABLED(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio##__suffix)),\
        (GPIO_PORT_DEVICE_INIT_STM32(__suffix, __SUFFIX)))

#define DEVICE_INIT_IF_OKAY(idx, __suffix)                            \
    GPIO_PORT_DEVICE_INIT_STM32_IF_OKAY(__suffix,                     \
        GET_ARG_N(UTIL_INC(idx), STM32_GPIO_PORTS_LIST_UPR))

FOR_EACH_IDX(DEVICE_INIT_IF_OKAY, (;), STM32_GPIO_PORTS_LIST_LWR);
```

**为什么 STM32 GPIO 不用标准路径？主要有三个原因：**

1. **驱动代码与设备实例化代码需要分离**
   - `gpio_stm32.c`（驱动行为）和 `gpioport_mgr.c`（设备实例化）是两个文件
   - `gpioport_mgr.c` 不只为 GPIO 子系统服务，它也是 pinctrl 子系统的底层设备提供者
   - 如果没有 `CONFIG_GPIO`，`gpioport_mgr.c` 仍然需要为 pinctrl 创建端口设备
   - 这种"设备对象被多个子系统共享"的需求，无法用标准的 `DT_INST_FOREACH_STATUS_OKAY` 归属到单个驱动

2. **STM32 端口编号有特殊语义**
   - 每个端口需要与 `STM32_PORTA`/`STM32_PORTB` 等 SoC 级常量关联
   - 这种映射依赖端口字母（a/b/c），而不只是 instance index（0/1/2）
   - `FOR_EACH_IDX` 遍历字母列表可以直接建立 suffix → 大写 SUFFIX 的对应关系

3. **跨系列通用性**
   - 同一份 `gpioport_mgr.c` 适用于所有 STM32 系列（F1/F4/L4/H7/WB 等）
   - 对不存在的端口，`DT_NODELABEL(gpiox)` 返回无效节点，`IF_ENABLED` 自动跳过
   - 这种方式不依赖 compatible string 的 instance 编号分配

### 对比总结

| 特征 | `DT_INST_FOREACH_STATUS_OKAY` | `FOR_EACH_IDX` + `DT_NODELABEL` |
|---|---|---|
| 迭代基础 | compatible 匹配的所有 okay 节点 | 预定义名称列表 |
| instance 标识 | 自动分配的数字 index | 显式的端口字母/编号 |
| 驱动与实例化 | 通常在同一文件 | 可以分离 |
| 实例间结构 | 必须完全一致 | 可以有特殊化逻辑 |
| 典型场景 | 通用外设驱动 | SoC 固定外设、共享设备对象 |
| 推荐程度 | 官方推荐首选 | SoC 特殊需求时使用 |

### 如何查找驱动最佳实现和接口文档

1. **驱动层接口定义**
   - 每个子系统的 `struct xxx_driver_api` 定义在对应的头文件中
   - GPIO: `zephyr/include/zephyr/drivers/gpio.h` → `struct gpio_driver_api`
   - UART: `zephyr/include/zephyr/drivers/uart.h` → `struct uart_driver_api`
   - SPI: `zephyr/include/zephyr/drivers/spi.h` → `struct spi_driver_api`
   - I2C: `zephyr/include/zephyr/drivers/i2c.h` → `struct i2c_driver_api`

2. **官方驱动开发文档**
   - `zephyr/doc/kernel/drivers/index.rst` — 设备驱动模型总览
   - `zephyr/doc/build/dts/howtos.rst` — 设备树感知驱动的两种实例化模式
   - `zephyr/doc/hardware/peripherals/*.rst` — 各子系统应用层 API 说明

3. **参考实现**
   - 找到目标子系统头文件中的 `struct xxx_driver_api`
   - 在 `zephyr/drivers/` 下搜索 `DEVICE_API(xxx,` 找所有实现该接口的驱动
   - 选择结构最简单的实现作为参考模板

---

## 补充三：各外设模块驱动层必须实现的接口

### 通用规律

每个 Zephyr 外设子系统都在其头文件中定义一个 `__subsystem struct xxx_driver_api`，驱动必须实现其中的函数指针。接口分为**必需**和**可选**两类，可选接口通常由 Kconfig 守护。

### GPIO 驱动层接口（完整）

文件：`zephyr/include/zephyr/drivers/gpio.h`

| 接口函数 | 必需/可选 | 功能 | STM32 实现状态 |
|---|---|---|---|
| `pin_configure` | **必需** | 配置引脚方向、上下拉、输出类型 | 已实现 |
| `port_get_raw` | **必需** | 读取端口输入电平 | 已实现 |
| `port_set_masked_raw` | **必需** | 按 mask 设置端口输出 | 已实现 |
| `port_set_bits_raw` | **必需** | 置位指定引脚 | 已实现 |
| `port_clear_bits_raw` | **必需** | 清位指定引脚 | 已实现 |
| `port_toggle_bits` | **必需** | 翻转指定引脚 | 已实现 |
| `pin_interrupt_configure` | **必需** | 配置引脚中断触发方式 | 已实现 |
| `manage_callback` | **必需** | 注册/注销中断回调 | 已实现 |
| `pin_get_config` | 可选（`CONFIG_GPIO_GET_CONFIG`） | 读回引脚当前配置 | 已实现（非 F1X） |
| `get_pending_int` | 可选 | 获取待处理中断位 | 未实现 |
| `port_get_direction` | 可选（`CONFIG_GPIO_GET_DIRECTION`） | 查询引脚方向 | 未实现 |

### 以 GPIO 为蓝本：如何确定任意模块的驱动层接口

对任何外设子系统 X，确定驱动层接口的步骤都是相同的：

**第一步：找到子系统头文件**

```
zephyr/include/zephyr/drivers/X.h
```

**第二步：搜索 `__subsystem struct` 或 `_driver_api`**

```bash
grep -n "struct.*_driver_api" zephyr/include/zephyr/drivers/X.h
```

**第三步：阅读函数指针表**

表中每个函数指针就是驱动必须或可选实现的接口。被 `#ifdef CONFIG_XXX` 包裹的是可选接口。

**第四步：查看应用层 API 到驱动层的映射**

在同一文件中搜索 `z_impl_`，即可看到每个应用 API 如何做通用处理后分发到 `api->fn()`。

### 常见外设子系统驱动接口速查

以下列出几个常用子系统的核心驱动接口，帮助对比 GPIO 的模式：

#### UART (`zephyr/include/zephyr/drivers/uart.h`)

| 接口函数 | 必需/可选 | 功能 |
|---|---|---|
| `poll_in` | 必需 | 轮询读一个字节 |
| `poll_out` | 必需 | 轮询写一个字节 |
| `err_check` | 可选 | 检查通信错误 |
| `configure` | 可选（`CONFIG_UART_USE_RUNTIME_CONFIGURE`） | 运行时配置波特率等 |
| `config_get` | 可选 | 读回当前配置 |
| `fifo_fill` | 可选（`CONFIG_UART_INTERRUPT_DRIVEN`） | 填充 TX FIFO |
| `fifo_read` | 可选（`CONFIG_UART_INTERRUPT_DRIVEN`） | 读取 RX FIFO |
| `irq_tx_enable/disable` | 可选 | 使能/关闭 TX 中断 |
| `irq_rx_enable/disable` | 可选 | 使能/关闭 RX 中断 |
| `irq_callback_set` | 可选 | 设置中断回调 |
| `callback_set` | 可选（`CONFIG_UART_ASYNC_API`） | 异步 API 回调 |
| `tx` / `rx_enable` / `rx_buf_rsp` | 可选（`CONFIG_UART_ASYNC_API`） | 异步 DMA 收发 |

#### SPI (`zephyr/include/zephyr/drivers/spi.h`)

| 接口函数 | 必需/可选 | 功能 |
|---|---|---|
| `transceive` | **必需** | 同步收发 |
| `release` | **必需** | 释放 SPI 总线 |
| `transceive_async` | 可选（`CONFIG_SPI_ASYNC`） | 异步收发 |

#### I2C (`zephyr/include/zephyr/drivers/i2c.h`)

| 接口函数 | 必需/可选 | 功能 |
|---|---|---|
| `configure` | **必需** | 设置速率等参数 |
| `transfer` | **必需** | 执行 I2C 事务 |
| `get_config` | 可选 | 读回当前配置 |
| `target_register` | 可选（`CONFIG_I2C_TARGET`） | 注册 I2C 从机 |
| `target_unregister` | 可选 | 注销 I2C 从机 |
| `recover_bus` | 可选 | 总线恢复 |
| `transfer_cb` | 可选（`CONFIG_I2C_CALLBACK`） | 异步传输回调 |

### 从应用层文档反推驱动层接口的通用方法

用户提到的 `https://docs.zephyrproject.org/latest/hardware/peripherals` 只描述了应用层 API，要找到对应的驱动层接口，可以：

1. 应用层 API 名为 `xxx_do_something()`
2. 其 `z_impl_` 实现中会调用 `api->do_something()`
3. `api` 是 `struct xxx_driver_api` 的实例
4. 驱动需要提供 `my_driver_do_something()` 并填入 `DEVICE_API(xxx, my_driver)`

这个映射关系对所有 Zephyr 外设子系统都成立。

### 具体到 STM32 GPIO 的总结

以两个源文件的职责划分来说：

| 职责 | 文件 | 关键函数/宏 |
|---|---|---|
| 定义 GPIO 子系统 API 实现 | `gpio_stm32.c` | `gpio_stm32_config`, `gpio_stm32_port_*`, `gpio_stm32_pin_interrupt_configure` |
| 注册驱动函数表 | `gpio_stm32.c` | `DEVICE_API(gpio, gpio_stm32_driver)` |
| 底层寄存器配置 | `gpioport_mgr.c` | `stm32_gpioport_configure_pin()` |
| 设备实例化 | `gpioport_mgr.c` | `GPIO_PORT_DEVICE_INIT()` + `FOR_EACH_IDX()` |
| PM（时钟门控） | `gpioport_mgr.c` | `stm32_gpioport_pm_action()` |
| 共享数据结构 | `stm32_gpio_shared.h` | `gpio_stm32_config`, `gpio_stm32_data`, `stm32_gpiomgr_pinnum_to_ll_val()` |

这种分离设计使得：
- `gpio_stm32.c` 可以只关注"Zephyr GPIO 语义到 STM32 LL 调用"的映射
- `gpioport_mgr.c` 可以同时服务 GPIO 子系统和 pinctrl 子系统
- 新增 STM32 系列时只需确保 `gpioport_mgr.c` 的宏展开覆盖到新端口即可

与 STM32CubeF4 LL 示例相比，Zephyr 将原本由应用自行承担的板级宏管理、EXTI 路由组织和中断分发，统一收敛到了设备树、设备模型和 GPIO 子系统 API 中。