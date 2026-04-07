# Zephyr 构建分析笔记（以 `nucleo_f411re + blinky` 为例）

本文目标：把一次 `west build -b nucleo_f411re ./blinky` 实际上做了什么、编进了哪些代码、每一层代码为什么会被选中、最终如何变成 `zephyr.elf` 讲清楚。内容同时参考：

- 当前工程实际源码与构建产物
- Zephyr 官方文档（Build System / Devicetree / Kconfig / Board Porting / SoC Porting / Application Development）

> 结论先说：`blinky` 的代码**不是预编译库**。它来自 `blinky/src/main.c`，先被编译为 `CMakeFiles/app.dir/src/main.c.obj`，再归档成 `app/libapp.a`，最后和内核、驱动、SoC、HAL 一起链接进 `build/zephyr/zephyr.elf`。

---

## 1. 本次分析的构建命令

```sh
west build -b nucleo_f411re ./blinky
```

本次工程中与构建直接相关的目录：

- 应用：`/home/firebot/zephyrproject/f411re/blinky`
- 构建目录：`/home/firebot/zephyrproject/f411re/build`
- Zephyr 源码根：`/home/firebot/zephyrproject/zephyr`

---

## 2. 先回答最关键的问题：`blinky` 的代码到底在哪里？

### 2.1 应用源文件在哪里

应用代码就在：

- `f411re/blinky/src/main.c`

应用 `CMakeLists.txt` 内容非常关键：

- `f411re/blinky/CMakeLists.txt`

核心语句是：

```cmake
target_sources(app PRIVATE src/main.c)
```

这句话的意思不是“引用一个预编译库”，而是：

- 把 `src/main.c` 加入 Zephyr 已经创建好的 `app` 这个 CMake target
- 后续由构建系统把它编译成目标文件
- 再把目标文件打包进 `app/libapp.a`

### 2.2 实际编译产物如何证明它不是预编译库

从 `build/compile_commands.json` 可以直接看到这条编译命令：

- 输入文件：`/home/firebot/zephyrproject/f411re/blinky/src/main.c`
- 输出文件：`CMakeFiles/app.dir/src/main.c.obj`

也就是说，`main.c` 是在这次构建里现编译出来的。

从最终链接图 `build/zephyr/zephyr.map` 又能看到：

- `LOAD app/libapp.a`
- `app/libapp.a(main.c.obj)`
- `.text.main ... app/libapp.a(main.c.obj)`

所以链路非常明确：

```text
blinky/src/main.c
  -> build/CMakeFiles/app.dir/src/main.c.obj
  -> build/app/libapp.a
  -> build/zephyr/zephyr_pre0.elf
  -> build/zephyr/zephyr.elf
```

### 2.3 为什么你在之前的 grep 里没看到 `blinky`

因为你之前筛选的是：

```sh
grep -oE "Building.*object zephyr.*obj" build.log
```

这个正则只保留路径里带 `zephyr/...obj` 的编译行，而应用的目标文件路径是：

- `CMakeFiles/app.dir/src/main.c.obj`

它不在 `zephyr/` 子目录下，所以被过滤掉了。不是没有编译，只是被你的筛选条件误伤了。Zephyr 构建系统，一视同仁；grep 不是。🙂

---

## 3. 官方文档对应的构建阶段

下面这些官方文档最值得长期看：

- [Build System (CMake)](https://docs.zephyrproject.org/latest/build/cmake/index.html)
- [Devicetree](https://docs.zephyrproject.org/latest/build/dts/index.html)
- [Input and output files](https://docs.zephyrproject.org/latest/build/dts/intro-input-output.html)
- [Configuration System (Kconfig)](https://docs.zephyrproject.org/latest/build/kconfig/index.html)
- [Setting Kconfig configuration values](https://docs.zephyrproject.org/latest/build/kconfig/setting.html)
- [Application Development](https://docs.zephyrproject.org/latest/develop/application/index.html)
- [Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [SoC Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/soc_porting.html)
- [Blinky sample](https://docs.zephyrproject.org/latest/samples/basic/blinky/README.html)

官方把构建流程分成两大阶段：

1. **配置阶段（Configuration Phase）**
   - 运行 CMake
   - 收集并合并 Devicetree
   - 合并 Kconfig 配置
   - 生成 Ninja/Make 构建脚本
2. **构建阶段（Build Phase）**
   - 先生成一些头文件、表和链接脚本
   - 再编译各层源文件为 `.obj`
   - 再归档为 `.a`
   - 再进行中间链接与最终链接
   - 最后导出 `elf/bin/hex/map` 等产物

结合当前工程，实际可以更细分为：

```text
配置阶段
  1. 解析应用 CMakeLists.txt
  2. 解析 board / soc / arch / module 的 CMake 和 Kconfig
  3. 合并 Devicetree -> zephyr.dts
  4. 合并 Kconfig -> .config / autoconf.h

预生成阶段
  5. 生成 offsets.h / syscall_list.h / driver-validation.h / linker.cmd 等

编译归档阶段
  6. 编译应用、arch、kernel、drivers、soc、hal、lib
  7. 归档成 libapp.a / libkernel.a / libdrivers__*.a / libarch__*.a ...

链接阶段
  8. 先链接 zephyr_pre0.elf
  9. 生成 isr_tables.c / isr_tables_vt.ld / isr_tables_swi.ld
 10. 最终链接 zephyr.elf

后处理阶段
 11. 导出 zephyr.bin / zephyr.hex / zephyr.map 等
```

---

## 4. 这次构建的输入文件：应用、Board、SoC、Arch、HAL 分别是什么

## 4.1 应用层输入

应用目录：`f411re/blinky`

关键文件：

- `f411re/blinky/CMakeLists.txt`
  - 入口文件
  - 通过 `find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})` 接入 Zephyr 构建系统
  - 通过 `target_sources(app PRIVATE src/main.c)` 把应用源码加入 `app` target
- `f411re/blinky/prj.conf`
  - 应用自己的 Kconfig 片段
  - 本次主要启用了 `CONFIG_GPIO=y`
- `f411re/blinky/src/main.c`
  - blinky 示例主程序
  - 通过 `DT_ALIAS(led0)` 从设备树取 LED 引脚定义

`main.c` 中最关键的一句是：

```c
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
```

它说明这个应用并不是写死 LED 管脚，而是依赖设备树里的 `led0` alias。官方 `Blinky` 文档也明确要求：

- 板子必须有 LED
- 设备树里必须提供 `led0` alias

---

## 4.2 Board 层输入

板级目录：`zephyr/boards/st/nucleo_f411re`

当前工程里这个目录包含：

- `board.yml`
- `Kconfig.nucleo_f411re`
- `Kconfig.defconfig`
- `nucleo_f411re.dts`
- `nucleo_f411re.yaml`
- `nucleo_f411re_defconfig`
- `board.cmake`
- `arduino_r3_connector.dtsi`
- `st_morpho_connector.dtsi`

其中最关键的是：

### `nucleo_f411re.dts`

它定义了这块板子的硬件特征，例如：

- `model = "STMicroelectronics STM32F411RE-NUCLEO board"`
- `compatible = "st,stm32f411re-nucleo"`
- `chosen { zephyr,console = &usart2; ... }`
- `leds { green_led_2: led_2 { gpios = <&gpioa 5 GPIO_ACTIVE_HIGH>; } }`
- `aliases { led0 = &green_led_2; sw0 = &user_button; }`

这几处定义非常重要：

- `zephyr,console = &usart2` 决定默认控制台 UART
- `led0 = &green_led_2` 决定 `blinky` 通过哪个 LED 闪烁
- `gpios = <&gpioa 5 GPIO_ACTIVE_HIGH>` 决定 LED 实际接在 `GPIOA.5`

### `nucleo_f411re_defconfig`

它提供板级默认配置，例如：

- `CONFIG_ARM_MPU=y`
- `CONFIG_HW_STACK_PROTECTION=y`
- `CONFIG_SERIAL=y`
- `CONFIG_CONSOLE=y`
- `CONFIG_UART_CONSOLE=y`
- `CONFIG_GPIO=y`

所以即使应用自己的 `prj.conf` 很短，板级 `defconfig` 也已经为串口控制台、GPIO、MPU 等准备好了默认配置。

### `board.cmake`

它主要负责 `west flash` / `west debug` 的 runner 配置，不决定应用逻辑，但决定如何烧写和调试这块板子。

---

## 4.3 Devicetree 的包含链：Board DTS、SoC DTSI、pinctrl DTSI 如何拼在一起

`nucleo_f411re.dts` 不是孤立文件，它会继续包含 SoC 和 pinctrl 定义：

```text
zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts
  -> #include <st/f4/stm32f411Xe.dtsi>
  -> #include <st/f4/stm32f411r(c-e)tx-pinctrl.dtsi>
  -> #include "arduino_r3_connector.dtsi"
  -> #include "st_morpho_connector.dtsi"
```

其中：

- `zephyr/dts/arm/st/f4/stm32f411Xe.dtsi`
  - 描述 STM32F411xE 这个 SoC 变种
  - 给出 flash/sram 大小等基础信息
- 更底层又会继续包含：
  - `zephyr/dts/arm/st/f4/stm32f411.dtsi`
  - `zephyr/dts/arm/st/f4/stm32f4.dtsi`
  - `zephyr/dts/arm/armv7-m.dtsi`
- `modules/hal/stm32/dts/st/f4/stm32f411r(c-e)tx-pinctrl.dtsi`
  - 提供复用引脚配置节点，比如：
    - `usart2_tx_pa2`
    - `usart2_rx_pa3`
    - `spi1_sck_pa5`
    - `i2c1_scl_pb8`

官方 [Input and output files](https://docs.zephyrproject.org/latest/build/dts/intro-input-output.html) 文档说明了 Devicetree 的输入/输出关系：

- 输入文件类型：`.dts` / `.dtsi` / `.overlay` / `bindings(.yaml)`
- C 预处理器先展开所有 include 和宏
- 再生成：
  - `build/zephyr/zephyr.dts.pre`
  - `build/zephyr/zephyr.dts`
  - `build/zephyr/include/generated/zephyr/devicetree_generated.h`

### 本工程的最终设备树产物

生成文件：

- `f411re/build/zephyr/zephyr.dts`

在这个文件里可以看到：

- `compatible = "st,stm32f411re-nucleo"`
- `zephyr,console = &usart2`
- `led0 = &green_led_2`

而且它还会在注释中标出这些属性来源于哪个原始 DTS/DTSI 文件，这对于追溯问题非常有用。

### 为什么 `blinky` 能直接拿到 LED

因为最终的 `zephyr.dts` 里有：

```dts
aliases {
    led0 = &green_led_2;
};
```

应用侧：

```c
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
```

这就把：

- 应用代码
- 设备树 alias
- GPIO 驱动

三者串起来了。

---

## 4.4 Kconfig 的来源、合并顺序和最终输出

官方 [Setting Kconfig configuration values](https://docs.zephyrproject.org/latest/build/kconfig/setting.html) 文档明确说明：初始配置来自多个片段合并。

在本工程中，可以简单理解为：

```text
Zephyr 全局 Kconfig
  + Board defconfig
  + 应用 prj.conf
  = build/zephyr/.config
```

具体到当前工程，主要输入有：

- Zephyr 全局配置根：`zephyr/Kconfig`
- 板级默认配置：`zephyr/boards/st/nucleo_f411re/nucleo_f411re_defconfig`
- 应用配置：`f411re/blinky/prj.conf`

输出：

- `f411re/build/zephyr/.config`
- `f411re/build/zephyr/include/generated/zephyr/autoconf.h`

### 当前 `.config` 中几个特别关键的符号

实际构建结果里可以看到：

- `CONFIG_SOC_STM32F411XE=y`
- `CONFIG_CPU_CORTEX_M=y`
- `CONFIG_CPU_CORTEX_M4=y`
- `CONFIG_ARM_MPU=y`
- `CONFIG_CLOCK_CONTROL_STM32_CUBE=y`
- `CONFIG_GPIO_STM32=y`
- `CONFIG_RESET_STM32=y`
- `CONFIG_UART_STM32=y`
- `CONFIG_CORTEX_M_SYSTICK=y`

这些符号直接决定了哪些 CMake 子目录会被加入、哪些源文件会被编译。

例如：

- `CONFIG_CPU_CORTEX_M=y`
  - 触发 `zephyr/arch/arm/core/CMakeLists.txt` 进入 `cortex_m` 子目录
- `CONFIG_ARM_MPU=y`
  - 触发 `zephyr/arch/arm/core/mpu` 下源码编译
- `CONFIG_GPIO_STM32=y`
  - 触发 `zephyr/drivers/gpio/gpio_stm32.c`
- `CONFIG_UART_STM32=y`
  - 触发 `zephyr/drivers/serial/uart_stm32.c`
- `CONFIG_CLOCK_CONTROL_STM32_CUBE=y`
  - 触发 STM32 时钟控制相关代码

---

## 5. CMake target、目标文件和源文件路径如何对应

很多初学者看到下面这种路径会有点迷糊：

- `zephyr/arch/arch/arm/core/cortex_m/CMakeFiles/arch__arm__core__cortex_m.dir/reset.S.obj`

其实这只是 CMake 自动生成的“目标文件输出路径”，规律是：

```text
<构建目录>/<库或目标>/CMakeFiles/<target>.dir/<相对源路径>.obj
```

具体例子：

### 5.1 应用层

```text
源文件：f411re/blinky/src/main.c
目标文件：f411re/build/CMakeFiles/app.dir/src/main.c.obj
归档库：f411re/build/app/libapp.a
```

### 5.2 架构层

```text
源文件：zephyr/arch/arm/core/cortex_m/reset.S
目标文件：
f411re/build/zephyr/arch/arch/arm/core/cortex_m/
  CMakeFiles/arch__arm__core__cortex_m.dir/reset.S.obj
归档库：
f411re/build/zephyr/arch/arch/arm/core/cortex_m/libarch__arm__core__cortex_m.a
```

### 5.3 驱动层

```text
源文件：zephyr/drivers/gpio/gpio_stm32.c
目标文件：
f411re/build/zephyr/drivers/gpio/CMakeFiles/drivers__gpio.dir/gpio_stm32.c.obj
归档库：
f411re/build/zephyr/drivers/gpio/libdrivers__gpio.a
```

### 5.4 SoC/HAL 层

```text
源文件：zephyr/soc/st/stm32/stm32f4x/soc.c
目标文件：f411re/build/zephyr/CMakeFiles/zephyr.dir/soc/st/stm32/stm32f4x/soc.c.obj

源文件：modules/hal/stm32/stm32cube/stm32f4xx/soc/system_stm32f4xx.c
目标文件：
f411re/build/modules/hal_stm32/stm32cube/
  CMakeFiles/..__modules__hal__stm32__stm32cube.dir/
  stm32f4xx/soc/system_stm32f4xx.c.obj
```

所以：

- 看 `.obj` 路径时，不要被 `CMakeFiles/...dir/...` 吓到
- 后半段通常就能看出它对应的原始源文件位置

---

## 6. 从源码到 ELF：本工程的实际流水线

## 6.1 配置阶段：CMake、Devicetree、Kconfig

### 第一步：进入应用 `CMakeLists.txt`

应用入口：`f411re/blinky/CMakeLists.txt`

执行顺序大致是：

1. `cmake_minimum_required(...)`
2. `find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})`
3. `project(blinky)`
4. `target_sources(app PRIVATE src/main.c)`

这里的关键点是：

- `find_package(Zephyr)` 会把整个 Zephyr 构建系统拉进来
- 它会创建 `app` 这个 target
- 之后应用只需要往 `app` 里加源文件即可

### 第二步：收集设备树输入

官方文档说明，设备树输入来自：

- Board `.dts`
- SoC / arch `.dtsi`
- overlay
- bindings `.yaml`

对当前工程来说，最核心的是：

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
- `zephyr/dts/arm/st/f4/stm32f411Xe.dtsi`
- `zephyr/dts/arm/st/f4/stm32f411.dtsi`
- `zephyr/dts/arm/st/f4/stm32f4.dtsi`
- `zephyr/dts/arm/armv7-m.dtsi`
- `modules/hal/stm32/dts/st/f4/stm32f411r(c-e)tx-pinctrl.dtsi`

输出：

- `build/zephyr/zephyr.dts.pre`
- `build/zephyr/zephyr.dts`
- `build/zephyr/include/generated/zephyr/devicetree_generated.h`

### 第三步：合并 Kconfig

输入：

- `zephyr/Kconfig`
- `zephyr/boards/st/nucleo_f411re/nucleo_f411re_defconfig`
- `f411re/blinky/prj.conf`

输出：

- `build/zephyr/.config`
- `build/zephyr/include/generated/zephyr/autoconf.h`

---

## 6.2 预生成阶段：还没正式编译应用，先生成辅助文件

从构建日志可以看到，这一阶段会生成很多文件：

- `include/generated/zephyr/version.h`
- `misc/generated/syscalls.json`
- `misc/generated/struct_tags.json`
- `include/generated/zephyr/syscall_dispatch.c`
- `include/generated/zephyr/syscall_list.h`
- `include/generated/zephyr/driver-validation.h`
- `include/generated/zephyr/kobj-types-enum.h`
- `include/generated/zephyr/otype-to-str.h`
- `include/generated/zephyr/otype-to-size.h`
- `include/generated/zephyr/heap_constants.h`
- `include/generated/zephyr/offsets.h`
- `linker_zephyr_pre0.cmd`
- `linker.cmd`

这些文件的作用大致是：

- 给汇编文件提供结构体偏移量（如 `offsets.h`）
- 给系统调用生成分发表和元数据
- 给链接器准备最终布局脚本
- 给用户态/对象系统生成辅助表

这也是为什么 Zephyr 构建不是“直接编译一堆 C 文件”这么简单。

---

## 6.3 编译归档阶段：各层代码分别编译成 `.obj` 和 `.a`

## 6.3.1 应用层

应用代码：

- `f411re/blinky/src/main.c`

编译结果：

- `build/CMakeFiles/app.dir/src/main.c.obj`
- `build/app/libapp.a`

## 6.3.2 架构层（arch）

本次编译进来的架构层文件主要是 ARM Cortex-M 路径下的内容：

- `zephyr/arch/arm/core/fatal.c`
- `zephyr/arch/arm/core/nmi.c`
- `zephyr/arch/arm/core/nmi_on_reset.S`
- `zephyr/arch/arm/core/tls.c`
- `zephyr/arch/arm/core/cortex_m/reset.S`
- `zephyr/arch/arm/core/cortex_m/prep_c.c`
- `zephyr/arch/arm/core/cortex_m/fault.c`
- `zephyr/arch/arm/core/cortex_m/fault_s.S`
- `zephyr/arch/arm/core/cortex_m/scb.c`
- `zephyr/arch/arm/core/cortex_m/vector_table.S`
- `zephyr/arch/arm/core/cortex_m/svc.S`
- `zephyr/arch/arm/core/cortex_m/irq_manage.c`
- `zephyr/arch/arm/core/cortex_m/irq_init.c`
- `zephyr/arch/arm/core/cortex_m/isr_wrapper.c`
- `zephyr/arch/arm/core/cortex_m/thread.c`
- `zephyr/arch/arm/core/cortex_m/thread_abort.c`
- `zephyr/arch/arm/core/cortex_m/cpu_idle.c`
- `zephyr/arch/arm/core/cortex_m/exc_exit.c`
- `zephyr/arch/arm/core/cortex_m/fpu.c`
- `zephyr/arch/arm/core/cortex_m/__aeabi_read_tp.S`
- `zephyr/arch/arm/core/cortex_m/swap_helper.S`
- `zephyr/arch/arm/core/mpu/arm_core_mpu.c`
- `zephyr/arch/arm/core/mpu/arm_mpu.c`
- `zephyr/arch/arm/core/mpu/arm_mpu_regions.c`
- `zephyr/arch/common/init.c`
- `zephyr/arch/common/sw_isr_common.c`
- `zephyr/arch/common/xip.c`
- `zephyr/arch/common/isr_tables.c`

### 关键 CMake 文件及作用

#### `zephyr/arch/arm/core/CMakeLists.txt`

作用：

- 定义 ARM 核心通用库
- 无条件加入 `fatal.c`、`nmi.c`、`nmi_on_reset.S`
- 根据 `CONFIG_CPU_CORTEX_M` 进入 `cortex_m` 子目录
- 根据 `CONFIG_ARM_MPU` 进入 `mpu` 子目录
- 注入向量表相关链接脚本

#### `zephyr/arch/arm/core/cortex_m/CMakeLists.txt`

作用：

- 收集 Cortex-M 专属启动和异常处理文件
- 把 `reset.S`、`prep_c.c`、`vector_table.S`、`irq_manage.c` 等加入构建
- 按配置决定是否加入 `exc_exit.c`、`thread_abort.c`、`isr_wrapper.c` 等

### 几个最值得重点理解的架构文件

#### `zephyr/arch/arm/core/cortex_m/reset.S`

职责：

- 复位入口
- 设置初始栈指针
- 跳转到 C 代码初始化流程

它是“软件世界真正开始”的第一站。

#### `zephyr/arch/arm/core/cortex_m/prep_c.c`

职责：

- 清零 `.bss`
- 拷贝 `.data`
- 设置向量表相关状态
- 调用后续内核启动流程

可以把它理解成“从裸复位状态切换到 C 运行时状态”的桥梁。

#### `zephyr/arch/common/isr_tables.c`

职责：

- 提供中断表框架支持
- 配合后续生成的 `isr_tables.c` 完成最终中断向量布局

---

## 6.3.3 内核层（kernel）

本次进入编译的内核文件包括：

- `zephyr/kernel/main_weak.c`
- `zephyr/kernel/banner.c`
- `zephyr/kernel/busy_wait.c`
- `zephyr/kernel/device.c`
- `zephyr/kernel/errno.c`
- `zephyr/kernel/fatal.c`
- `zephyr/kernel/init.c`
- `zephyr/kernel/kheap.c`
- `zephyr/kernel/mem_slab.c`
- `zephyr/kernel/float.c`
- `zephyr/kernel/version.c`
- `zephyr/kernel/idle.c`
- `zephyr/kernel/mailbox.c`
- `zephyr/kernel/msg_q.c`
- `zephyr/kernel/mutex.c`
- `zephyr/kernel/queue.c`
- `zephyr/kernel/sem.c`
- `zephyr/kernel/stack.c`
- `zephyr/kernel/system_work_q.c`
- `zephyr/kernel/work.c`
- `zephyr/kernel/condvar.c`
- `zephyr/kernel/thread.c`
- `zephyr/kernel/sched.c`
- `zephyr/kernel/pipe.c`
- `zephyr/kernel/timeout.c`
- `zephyr/kernel/timer.c`
- `zephyr/kernel/timeslicing.c`
- `zephyr/kernel/mempool.c`
- `zephyr/kernel/dynamic_disabled.c`

### `zephyr/kernel/CMakeLists.txt` 的作用

这个文件很重要，因为它不是简单“把 kernel 目录全编了”，而是：

- 先定义 `kernel` 库
- 再根据各个 `CONFIG_*` 条件，决定是否把某些文件放进来

例如：

- `CONFIG_MULTITHREADING` 开启时，才会有 `idle.c`、`mutex.c`、`sched.c`、`thread.c` 等
- `CONFIG_SYS_CLOCK_EXISTS` 开启时，才会有 `timeout.c`、`timer.c`
- `CONFIG_KERNEL_MEM_POOL` 开启时，才会有 `mempool.c`

### 两个关键内核文件

#### `zephyr/kernel/init.c`

职责：

- 初始化内核全局状态
- 创建主线程和 idle 线程
- 执行 `SYS_INIT` 初始化序列
- 进入调度器

#### `zephyr/kernel/sched.c`

职责：

- 线程调度
- 就绪队列管理
- 优先级与时间片处理
- 切换下一个运行线程

如果把 `reset.S` 看成“软件启动入口”，那 `sched.c` 就可以看成“系统活起来以后谁先跑”的决策中心。

---

## 6.3.4 库层（lib）

本次编译到的库层主要包括：

- `zephyr/lib/heap/heap.c`
- `zephyr/lib/libc/validate_libc.c`
- `zephyr/lib/os/assert.c`
- `zephyr/lib/os/cbprintf_complete.c`
- `zephyr/lib/os/cbprintf_packaged.c`
- `zephyr/lib/os/clock.c`
- `zephyr/lib/os/printk.c`
- `zephyr/lib/os/sem.c`
- `zephyr/lib/os/thread_entry.c`
- `zephyr/lib/utils/*.c`
- `zephyr/lib/libc/common/...`
- `zephyr/lib/libc/picolibc/...`
- `zephyr/lib/posix/c_lib_ext/...`

这些代码提供：

- `printf` / `malloc` / `abort` / `time` 等 C 库能力
- Zephyr 自己的打印、时间、线程入口、工具型容器/算法支持

为什么 `blinky` 会把 libc 编进来？

因为 `main.c` 里用了：

```c
printf("LED state: %s\n", ...)
```

这自然会把标准输出相关实现链路拉进最终镜像。

---

## 6.3.5 驱动层（drivers）

本次编译进来的驱动文件包括：

- `zephyr/drivers/clock_control/clock_stm32_ll_common.c`
- `zephyr/drivers/clock_control/clock_stm32f2_f4_f7.c`
- `zephyr/drivers/console/uart_console.c`
- `zephyr/drivers/gpio/gpio_stm32.c`
- `zephyr/drivers/interrupt_controller/intc_exti_stm32.c`
- `zephyr/drivers/interrupt_controller/intc_gpio_stm32.c`
- `zephyr/drivers/pinctrl/common.c`
- `zephyr/drivers/pinctrl/pinctrl_stm32.c`
- `zephyr/drivers/reset/reset_stm32.c`
- `zephyr/drivers/serial/uart_stm32.c`
- `zephyr/drivers/timer/cortex_m_systick.c`
- `zephyr/drivers/timer/sys_clock_init.c`

### 为什么这些驱动会被选中

这不是“drivers 目录全编”，而是“配置 + 设备树共同决定”。

例如：

- `CONFIG_GPIO_STM32=y`
  - 编译 `gpio_stm32.c`
- `CONFIG_UART_STM32=y`
  - 编译 `uart_stm32.c`
- `CONFIG_CORTEX_M_SYSTICK=y`
  - 编译 `cortex_m_systick.c`
- `CONFIG_CLOCK_CONTROL_STM32_CUBE=y`
  - 编译 STM32 时钟控制相关代码
- 设备树中 `zephyr,console = &usart2`
  - 让 `uart_console.c` 和 `uart_stm32.c` 有实际用途

### 三个重点驱动文件

#### `zephyr/drivers/gpio/gpio_stm32.c`

职责：

- 实现 STM32 GPIO 驱动
- 把 `gpio_pin_configure_dt()`、`gpio_pin_toggle_dt()` 这类上层 API 落到 STM32 GPIO 寄存器和 HAL/LL 层上

`blinky` 就是直接依赖它在工作。

#### `zephyr/drivers/serial/uart_stm32.c`

职责：

- 实现 STM32 UART/USART 驱动
- 支持控制台输出

因为本板 `chosen` 把控制台指定到了 `usart2`，所以它会进入构建。

#### `zephyr/drivers/timer/cortex_m_systick.c`

职责：

- 使用 Cortex-M 的 SysTick 作为系统时钟源
- 驱动 `k_msleep()`、超时、时间片等内核时间机制

没有它，`k_msleep(SLEEP_TIME_MS)` 就没法正常工作。

---

## 6.3.6 SoC 层（soc）

本次构建进入的 SoC 代码包括：

- `zephyr/soc/st/stm32/common/gpioport_mgr.c`
- `zephyr/soc/st/stm32/common/soc_config.c`
- `zephyr/soc/st/stm32/common/stm32_backup_domain.c`
- `zephyr/soc/st/stm32/common/stm32cube_hal.c`
- `zephyr/soc/st/stm32/stm32f4x/soc.c`

### `zephyr/soc/st/stm32/stm32f4x/CMakeLists.txt`

作用：

- 把 `soc.c` 加入编译
- 指定 SoC 级链接脚本基线
- 按需加入 `power.c`、`poweroff.c`

### `zephyr/soc/st/stm32/stm32f4x/soc.c`

职责：

- SoC 早期初始化钩子
- 配置 F4 系列芯片早期状态
- 配合时钟/缓存等初始化

### `zephyr/soc/st/stm32/common/stm32cube_hal.c`

职责：

- 做 Zephyr 与 STM32Cube HAL 之间的适配
- 例如把 HAL 的 tick / delay 接口接到 Zephyr 时间机制上

---

## 6.3.7 HAL 层（modules/hal/stm32）

本次编译进来的 HAL 代码包括：

- `modules/hal/stm32/stm32cube/stm32f4xx/drivers/src/stm32f4xx_hal.c`
- `modules/hal/stm32/stm32cube/stm32f4xx/drivers/src/stm32f4xx_hal_rcc.c`
- `modules/hal/stm32/stm32cube/stm32f4xx/drivers/src/stm32f4xx_hal_rcc_ex.c`
- `modules/hal/stm32/stm32cube/stm32f4xx/drivers/src/stm32f4xx_ll_rcc.c`
- `modules/hal/stm32/stm32cube/stm32f4xx/drivers/src/stm32f4xx_ll_utils.c`
- `modules/hal/stm32/stm32cube/stm32f4xx/soc/system_stm32f4xx.c`

### `modules/hal/stm32/stm32cube/stm32f4xx/soc/system_stm32f4xx.c`

职责：

- STM32Cube 的系统初始化文件
- 维护 `SystemCoreClock`
- 提供系统时钟初始化/更新的基础能力

它不是 Zephyr 自己写的 board 层代码，而是 ST HAL 提供的底层支持，被 Zephyr SoC/驱动层拿来使用。

---

## 6.3.8 其他生成或辅助文件

本次还有一些非“手写源文件”，但确实参与了构建：

- `build/zephyr/misc/generated/configs.c`
- `build/zephyr/isr_tables.c`
- `zephyr/misc/empty_file.c`
- `zephyr/subsys/mem_mgmt/mem_attr.c`
- `zephyr/subsys/tracing/tracing_none.c`

其中最重要的是：

- `isr_tables.c`
  - 不是你手写的，而是构建过程根据中断配置自动生成的

---

## 6.4 链接阶段：先归档，再中间链接，再最终链接

从构建结果可以看到，先形成一堆静态库：

- `app/libapp.a`
- `zephyr/libzephyr.a`
- `zephyr/kernel/libkernel.a`
- `zephyr/arch/common/libarch__common.a`
- `zephyr/arch/common/libisr_tables.a`
- `zephyr/arch/arch/arm/core/libarch__arm__core.a`
- `zephyr/arch/arch/arm/core/cortex_m/libarch__arm__core__cortex_m.a`
- `zephyr/arch/arch/arm/core/mpu/libarch__arm__core__mpu.a`
- `zephyr/drivers/.../libdrivers__*.a`
- `modules/hal_stm32/stm32cube/lib..__modules__hal__stm32__stm32cube.a`
- `zephyr/lib/libc/picolibc/liblib__libc__picolibc.a`
- `zephyr/lib/posix/c_lib_ext/liblib__posix__c_lib_ext.a`

然后进行两次主要链接：

### 第一次：

- `Linking C executable zephyr/zephyr_pre0.elf`

作用：

- 先把各层代码链接成一个中间 ELF
- 供后续脚本扫描符号、生成 ISR 表等

### 中间生成：

- `Generating isr_tables.c, isr_tables_vt.ld, isr_tables_swi.ld`

作用：

- 根据中断连接信息自动生成最终中断表相关文件

### 第二次：

- `Linking C executable zephyr/zephyr.elf`

作用：

- 引入刚生成的 ISR 表
- 完成最终镜像链接

最终输出：

- `build/zephyr/zephyr.elf`
- `build/zephyr/zephyr.bin`
- `build/zephyr/zephyr.hex`
- `build/zephyr/zephyr.map`

---

## 7. 结合本工程，按“层”总结到底编了什么、为什么编

## 7.1 应用层

### 编了什么

- `f411re/blinky/src/main.c`

### 为什么会编

- `f411re/blinky/CMakeLists.txt` 通过 `target_sources(app PRIVATE src/main.c)` 显式加入

### 它负责什么

- 获取 `led0` 设备树描述
- 配置 LED 对应的 GPIO 为输出
- 循环翻转 GPIO
- 打印 LED 状态

---

## 7.2 Board / Devicetree 层

### 编了/参与了什么

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
- `zephyr/boards/st/nucleo_f411re/arduino_r3_connector.dtsi`
- `zephyr/boards/st/nucleo_f411re/st_morpho_connector.dtsi`
- `zephyr/boards/st/nucleo_f411re/nucleo_f411re_defconfig`
- `zephyr/boards/st/nucleo_f411re/Kconfig.nucleo_f411re`
- `zephyr/boards/st/nucleo_f411re/board.yml`
- `zephyr/boards/st/nucleo_f411re/board.cmake`

### 为什么会参与

- `-b nucleo_f411re` 指定了目标板
- CMake 和 Kconfig 会自动找到对应 board 目录
- DTS 会作为设备树输入
- defconfig 会作为默认配置输入

### 它们负责什么

- 描述板上 LED、按键、控制台 UART、时钟、连接器等
- 为 sample 提供 `led0` alias
- 提供 GPIO / UART / Console 等默认配置

---

## 7.3 Arch 层

### 编了什么

- ARM 通用核心文件
- Cortex-M 启动与异常处理文件
- MPU 支撑文件
- Arch 通用中断表/初始化文件

### 为什么会参与

- `.config` 中有：
  - `CONFIG_CPU_CORTEX_M=y`
  - `CONFIG_CPU_CORTEX_M4=y`
  - `CONFIG_ARM_MPU=y`

### 它负责什么

- 复位启动
- 切换到 C 运行环境
- 建立异常向量表
- 管理 IRQ、线程切换、异常退出、MPU 等

---

## 7.4 Kernel 层

### 编了什么

- 初始化、线程、调度、同步、超时、工作队列、对象管理相关源码

### 为什么会参与

- Zephyr 内核本来就是所有应用都要带的基础部分
- 当前配置启用了多线程、系统时钟、内核内存池等能力

### 它负责什么

- 创建和调度线程
- 提供 `k_msleep()` 等 API
- 提供消息队列、信号量、互斥锁等基础机制

---

## 7.5 Drivers 层

### 编了什么

- GPIO、UART、console、timer、clock_control、pinctrl、reset、中断控制器等驱动

### 为什么会参与

- `.config` 中启用了相关驱动符号
- Devicetree 中存在对应的设备节点和 chosen/alias 引用

### 它负责什么

- 让应用和内核能够通过统一 API 操作具体硬件外设

---

## 7.6 SoC 层

### 编了什么

- STM32 共性代码
- STM32F4 系列特定初始化代码

### 为什么会参与

- `.config` 中 `CONFIG_SOC_STM32F411XE=y`

### 它负责什么

- 提供 SoC 级初始化钩子
- 提供备份域、GPIO 端口管理、HAL 适配等共性逻辑

---

## 7.7 HAL 层

### 编了什么

- STM32Cube HAL/LL 时钟和系统文件

### 为什么会参与

- 当前 SoC/驱动实现依赖 STM32Cube HAL
- `.config` 中 `CONFIG_CLOCK_CONTROL_STM32_CUBE=y`

### 它负责什么

- 提供 ST 官方底层寄存器/时钟/系统支持实现
- 供 Zephyr 的 SoC 层和驱动层调用

---

## 8. 官方文档和当前工程如何对应起来

## 8.1 Devicetree 官方说法 vs 当前工程实际

官方文档说：

- Board `.dts` + SoC `.dtsi` + overlay + bindings
  -> `zephyr.dts.pre`
  -> `zephyr.dts`
  -> `devicetree_generated.h`

当前工程实际完全符合：

- 输入：`nucleo_f411re.dts` + `stm32f411Xe.dtsi` + `stm32f411r(c-e)tx-pinctrl.dtsi`
- 输出：`build/zephyr/zephyr.dts`

## 8.2 Kconfig 官方说法 vs 当前工程实际

官方文档说：

- board defconfig
- 应用 `prj.conf`
- 生成 `.config` / `autoconf.h`

当前工程实际也是：

- `nucleo_f411re_defconfig`
- `blinky/prj.conf`
- `build/zephyr/.config`

## 8.3 应用进入构建的官方说法 vs 当前工程实际

官方 `Application Development` 文档说：

```cmake
find_package(Zephyr)
project(my_zephyr_app)
target_sources(app PRIVATE src/main.c)
```

当前 `blinky/CMakeLists.txt` 正是这个模式。

---

## 9. 与当前问题最相关的几个文件，建议重点读

如果想从“能跑”进阶到“真正理解”，优先看这些文件：

### 应用入口

- `f411re/blinky/CMakeLists.txt`
- `f411re/blinky/src/main.c`

### Board 与设备树

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
- `f411re/build/zephyr/zephyr.dts`
- `f411re/build/zephyr/include/generated/zephyr/devicetree_generated.h`

### 配置

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re_defconfig`
- `f411re/blinky/prj.conf`
- `f411re/build/zephyr/.config`

### 启动链与架构

- `zephyr/arch/arm/core/CMakeLists.txt`
- `zephyr/arch/arm/core/cortex_m/CMakeLists.txt`
- `zephyr/arch/arm/core/cortex_m/reset.S`
- `zephyr/arch/arm/core/cortex_m/prep_c.c`

### 内核

- `zephyr/kernel/CMakeLists.txt`
- `zephyr/kernel/init.c`
- `zephyr/kernel/sched.c`

### 驱动

- `zephyr/drivers/gpio/gpio_stm32.c`
- `zephyr/drivers/serial/uart_stm32.c`
- `zephyr/drivers/timer/cortex_m_systick.c`

### SoC / HAL

- `zephyr/soc/st/stm32/stm32f4x/CMakeLists.txt`
- `zephyr/soc/st/stm32/stm32f4x/soc.c`
- `zephyr/soc/st/stm32/common/stm32cube_hal.c`
- `modules/hal/stm32/stm32cube/stm32f4xx/soc/system_stm32f4xx.c`

### 构建产物

- `f411re/build/compile_commands.json`
- `f411re/build/zephyr/zephyr.map`
- `f411re/build/zephyr/zephyr.elf`

---

## 10. `build/compile_commands.json` 和 `zephyr.map` 有什么用

## 10.1 `compile_commands.json`

这个文件最适合回答两个问题：

1. 某个 `.obj` 对应哪个源文件？
2. 某个源文件编译时到底带了哪些宏和头文件路径？

例如 `main.c` 的编译命令里可以看到：

- 编译器：`arm-zephyr-eabi-gcc`
- CPU：`-mcpu=cortex-m4 -mthumb`
- 自动引入：`autoconf.h`
- 自动包含路径：
  - Zephyr include
  - SoC include
  - HAL include
  - 生成目录 include

也就是说，应用代码虽然很短，但它是在完整 Zephyr 编译上下文里被编译的。

## 10.2 `zephyr.map`

这个文件最适合回答：

- 某个符号最终落在哪个地址
- 某段代码来自哪个 `.o` 或 `.a`
- 某个库是否真正被链接进最终 ELF

例如它能直接证明：

- `app/libapp.a(main.c.obj)` 确实进入最终链接
- `.text.main` 在最终镜像中的地址是多少

---

## 11. 本次构建涉及的自动生成文件整理

根据实际输出，本次构建自动生成的关键文件包括：

### 设备树相关

- `build/zephyr/zephyr.dts.pre`
- `build/zephyr/zephyr.dts`
- `build/zephyr/include/generated/zephyr/devicetree_generated.h`

### 配置相关

- `build/zephyr/.config`
- `build/zephyr/include/generated/zephyr/autoconf.h`
- `build/zephyr/misc/generated/configs.c`

### 系统调用与对象系统

- `build/zephyr/misc/generated/syscalls.json`
- `build/zephyr/misc/generated/struct_tags.json`
- `build/zephyr/include/generated/zephyr/syscall_dispatch.c`
- `build/zephyr/include/generated/zephyr/syscall_list.h`
- `build/zephyr/include/generated/zephyr/kobj-types-enum.h`
- `build/zephyr/include/generated/zephyr/otype-to-str.h`
- `build/zephyr/include/generated/zephyr/otype-to-size.h`

### 链接与偏移相关

- `build/zephyr/include/generated/zephyr/offsets.h`
- `build/zephyr/include/generated/zephyr/heap_constants.h`
- `build/zephyr/linker_zephyr_pre0.cmd`
- `build/zephyr/linker.cmd`

### 中断表相关

- `build/zephyr/isr_tables.c`
- `build/zephyr/isr_tables_vt.ld`
- `build/zephyr/isr_tables_swi.ld`

### 最终镜像相关

- `build/zephyr/zephyr.elf`
- `build/zephyr/zephyr.bin`
- `build/zephyr/zephyr.hex`
- `build/zephyr/zephyr.map`

---

## 12. 简化版理解：为什么会编进这么多文件

一句话总结：

> Zephyr 不是“只编应用”，而是“根据应用 + board + DTS + Kconfig，把需要的内核、驱动、SoC、HAL 一起编成一个完整固件”。

拿这次 `blinky` 来说：

- 你写了 `main.c`
- `main.c` 需要 `GPIO`
- Board DTS 提供了 `led0`
- DTS 告诉系统 LED 在 `GPIOA.5`
- Kconfig 告诉系统要编 `GPIO`、`UART console`、`SysTick`、`MPU`
- SoC 和 HAL 提供 STM32F411 的底层支撑
- 内核提供 `k_msleep()` 和调度机制

于是最终就不是只编一个 `main.c`，而是把整条链全拉起来了。

---

## 13. 移植小华 HC32 到 Zephyr：建议按官方方法分层做

已有前提：

- 驱动 demo
- 芯片手册
- 常规 Cortex-M 芯片资料

这已经是个不错的起点，但要做 Zephyr 适配，建议严格按官方 [SoC Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/soc_porting.html) 和 [Board Porting Guide](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html) 的思路分层。

## 13.1 推荐先做 out-of-tree 适配

官方 `Application Development` 文档支持：

- `BOARD_ROOT`
- `SOC_ROOT`
- `DTS_ROOT`

这意味着你完全可以先在应用或独立仓库里做板级/SoC 适配，不需要一开始就改 Zephyr 主仓。

推荐目录结构如下：

```text
hc32_port/
├── app/
│   ├── CMakeLists.txt
│   ├── prj.conf
│   └── src/
│       └── main.c
├── boards/
│   └── huada/
│       └── hc32_my_board/
│           ├── board.yml
│           ├── board.cmake
│           ├── CMakeLists.txt
│           ├── Kconfig.hc32_my_board
│           ├── Kconfig.defconfig
│           ├── hc32_my_board_<qualifiers>.dts
│           ├── hc32_my_board_<qualifiers>_defconfig
│           └── hc32_my_board_<qualifiers>.yaml
├── soc/
│   └── huada/
│       └── hc32f4xx/
│           ├── soc.yml
│           ├── soc.h
│           ├── CMakeLists.txt
│           ├── Kconfig.soc
│           ├── Kconfig
│           ├── Kconfig.defconfig
│           └── soc.c
└── dts/
    ├── arm/
    └── bindings/
```

---

## 13.2 先移植 SoC，再移植 Board

官方建议的顺序也基本是这样，因为 Board 依赖 SoC。

### 第一步：SoC 目录

SoC 最少要准备：

- `soc.yml`
- `soc.h`
- `CMakeLists.txt`
- `Kconfig.soc`

可选但通常也需要：

- `Kconfig`
- `Kconfig.defconfig`
- `soc.c`

### SoC 层要先解决什么

1. **内存布局**
   - flash 起始地址和大小
   - sram 起始地址和大小
2. **CPU/架构匹配**
   - Cortex-M0/M3/M4/M33 ?
   - 是否有 MPU/FPU/DWT/VTOR
3. **早期启动**
   - `soc.c`
   - `SystemCoreClock`
   - 时钟树初始化
4. **设备树基础节点**
   - flash / sram
   - gpio / uart / timer / clock / reset controller
5. **Kconfig 选择关系**
   - `SOC_<SOC_NAME>`
   - `SOC_SERIES_<SERIES_NAME>`
   - `SOC_FAMILY_<FAMILY_NAME>`

---

## 13.3 Board 目录

Board 层至少需要：

- `board.yml`
- `*.dts`
- `Kconfig.<board>` 或等价板级 Kconfig 文件

通常还要有：

- `*_defconfig`
- `board.cmake`
- `*.yaml`

### Board 层最关键的是把 sample 跑起来

先别急着把所有外设一次全上，建议先做一个“最小可用板级支持”：

1. 让串口控制台跑起来
   - `chosen { zephyr,console = &uartX; }`
2. 让系统时钟跑起来
   - SysTick 或芯片定时器
3. 让 GPIO 跑起来
4. 在 DTS 中提供：
   - `aliases { led0 = &xxx; }`
5. 先跑通：
   - `samples/basic/blinky`
6. 再跑通：
   - `samples/hello_world`

如果连 `hello_world` 和 `blinky` 都跑不通，说明基础层还有问题，不适合往更复杂驱动继续推进。

---

## 13.4 HC32 设备树应该重点补哪些节点

建议最低优先级顺序：

1. `chosen`
   - `zephyr,console`
   - `zephyr,sram`
   - `zephyr,flash`
2. `aliases`
   - `led0`
   - `sw0`（如果有按键）
3. `gpio` 控制器节点
4. `uart` 控制器节点
5. `clock` / `rcc` 类节点
6. `timer` / `systick`
7. `pinctrl`
8. `flash-controller` / `flash0`

设备树的核心思想不是“把厂商头文件翻译一遍”，而是：

> 用 Zephyr 可理解的硬件描述，把设备、引脚、时钟、中断关系表达出来，让驱动可以通过 DT 宏自动实例化。

---

## 13.5 HC32 移植的实际建议顺序

推荐按下面顺序推进：

### 第 1 阶段：最小启动

- SoC Kconfig 打通
- SoC CMakeLists 打通
- 内存映射正确
- 能生成 `zephyr.elf`

### 第 2 阶段：串口控制台

- UART 节点 + 驱动接通
- `zephyr,console` 指向 UART
- `hello_world` 能打印

### 第 3 阶段：GPIO + LED

- GPIO 控制器节点
- Board DTS 提供 `led0`
- `blinky` 能闪

### 第 4 阶段：系统时钟与延时

- SysTick/Timer 驱动稳定
- `k_msleep()` 正常

### 第 5 阶段：补其余驱动

- I2C
- SPI
- PWM
- RTC
- watchdog

---

## 13.6 HC32 移植时最容易踩的坑

### 1. 只补了 DTS，没补 Kconfig

后果：

- DTS 节点有了，但驱动根本没进编译

### 2. 只补了 Kconfig，没补 DTS

后果：

- 驱动编译了，但没有实例节点，应用拿不到设备

### 3. `led0` 没配

后果：

- `blinky` 编译时就会报错

### 4. `zephyr,console` 没配对

后果：

- 串口驱动可能编了，但控制台不输出

### 5. 时钟树初始化不完整

后果：

- UART 波特率不对
- SysTick 频率不对
- `k_msleep()` 不准

### 6. 引脚复用（pinctrl）没理顺

后果：

- 外设节点存在，但实际 IO 不工作

### 7. 中断号或优先级描述错误

后果：

- 外设初始化看似成功，但中断不触发

---

## 14. 一个适合记忆的总图

```text
应用层
  blinky/src/main.c
    ↓ target_sources(app ...)

配置输入
  board.dts + soc.dtsi + pinctrl.dtsi + board_defconfig + prj.conf
    ↓

生成文件
  zephyr.dts / devicetree_generated.h / .config / autoconf.h / linker.cmd
    ↓

编译
  app + arch + kernel + drivers + soc + hal + libc
    ↓

归档
  libapp.a + libkernel.a + libdrivers__*.a + libarch__*.a + libzephyr.a
    ↓

中间链接
  zephyr_pre0.elf
    ↓

自动生成
  isr_tables.c / isr_tables_vt.ld / isr_tables_swi.ld
    ↓

最终链接
  zephyr.elf
    ↓

导出镜像
  zephyr.bin / zephyr.hex / zephyr.map
```

---

## 15. 本文分析时参考的本地文件

### 应用

- `f411re/blinky/CMakeLists.txt`
- `f411re/blinky/prj.conf`
- `f411re/blinky/src/main.c`

### Board / DTS / 配置

- `zephyr/boards/st/nucleo_f411re/nucleo_f411re.dts`
- `zephyr/boards/st/nucleo_f411re/nucleo_f411re_defconfig`
- `zephyr/boards/st/nucleo_f411re/board.yml`

### 关键构建产物

- `f411re/build/compile_commands.json`
- `f411re/build/zephyr/.config`
- `f411re/build/zephyr/zephyr.dts`
- `f411re/build/zephyr/zephyr.map`

### 架构 / 内核 / 驱动 / SoC / HAL

- `zephyr/arch/arm/core/CMakeLists.txt`
- `zephyr/arch/arm/core/cortex_m/CMakeLists.txt`
- `zephyr/kernel/CMakeLists.txt`
- `zephyr/drivers/gpio/CMakeLists.txt`
- `zephyr/soc/st/stm32/stm32f4x/CMakeLists.txt`

---

## 16. 最后一句总结

这次 `nucleo_f411re + blinky` 的构建，本质上不是“编译一个 demo”，而是：

> 由应用告诉系统“我要干什么”，由设备树告诉系统“板子长什么样”，由 Kconfig 告诉系统“哪些能力要打开”，再由 Zephyr 把应用、架构、内核、驱动、SoC、HAL 统合成一个完整固件。

如果后续要继续深入，建议下一步按这个顺序看：

1. `blinky/src/main.c`
2. `nucleo_f411re.dts`
3. `build/zephyr/zephyr.dts`
4. `build/zephyr/.config`
5. `reset.S`
6. `prep_c.c`
7. `kernel/init.c`
8. `gpio_stm32.c`
9. `zephyr.map`

这样就能把“从 LED alias 到 GPIO 翻转，再到最终固件地址”的整条链闭环看明白。