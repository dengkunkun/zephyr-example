# Zephyr Application Development

官方文档：`https://docs.zephyrproject.org/latest/develop/application/index.html`

## 1. 目的

本文整理 Zephyr 应用开发的基本结构、应用类型、创建方式、构建变量、配置机制、构建与运行流程，以及应用内自定义 board、SoC 和 devicetree 的方法。内容基于 Zephyr 官方 `Application Development` 文档，并结合当前工作区结构进行说明。

在当前工作区中：

- `zephyr/` 是 Zephyr 主仓库
- `zephyr-example/f411ceu6/` 位于工作区内、但不在 `zephyr/` 仓库内

因此，`zephyr-example/f411ceu6` 属于 **Zephyr workspace application**。

---

## 2. 应用的基本结构

一个最小 Zephyr 应用通常包含如下文件：

```text
<app>
├── CMakeLists.txt
├── app.overlay
├── prj.conf
├── VERSION
└── src
    └── main.c
```

这些文件的职责如下：

- `CMakeLists.txt`：应用构建入口，负责将应用接入 Zephyr 的 CMake 构建系统
- `app.overlay`：应用级设备树 overlay，用于对板级基础设备树做增量修改
- `prj.conf`：应用级 Kconfig 片段，用于启用或配置软件功能
- `VERSION`：应用版本信息，可用于镜像签名和版本管理
- `src/main.c`：应用源代码入口

说明：

- `app.overlay` 和 `prj.conf` 不是强制文件，但绝大多数应用都会使用
- Zephyr 采用 **out-of-tree build**，构建输出必须位于独立的 `build/` 目录，不能在源码目录原地构建

---

## 3. 应用类型

Zephyr 按应用目录所在位置区分三类应用：

### 3.1 repository application

应用位于 `zephyr` 主仓库内部，例如：

```text
zephyr/samples/hello_world
```

这类应用通常用于 Zephyr 自带样例、测试或官方示例。

### 3.2 workspace application

应用位于 west workspace 中，但位于 `zephyr/` 仓库外部，例如：

```text
zephyrproject/
├── .west/
├── zephyr/
├── modules/
└── applications/
    └── app/
```

这是最常见的私有项目组织方式。当前工作区中的 `zephyr-example/f411ceu6` 就属于这一类型。

### 3.3 freestanding application

应用位于 workspace 之外，只要能正确找到 `ZEPHYR_BASE`，仍然可以构建。

这类方式通常用于独立仓库或与既有工程体系集成。

---

## 4. 创建应用

Zephyr 官方推荐两种起点：

1. 基于 `example-application` 参考仓库创建
2. 手工搭建最小应用目录

### 4.1 基于 example-application 创建

官方参考仓库：

- `https://github.com/zephyrproject-rtos/example-application`

该仓库用于展示完整的 Zephyr 应用组织方式，包含以下常见能力：

- 基础 Zephyr 应用骨架
- workspace application 组织方式
- 自定义 board
- 自定义 devicetree binding
- 自定义驱动
- out-of-tree library
- CI 配置
- Twister 测试
- 自定义 west 扩展命令
- 文档模板

在现有 workspace 中创建应用时，可直接克隆：

```sh
git clone https://github.com/zephyrproject-rtos/example-application my-app
```

若希望以其 `west.yml` 为基础创建一个新的完整 workspace，则可以将其作为 manifest repository 使用。

### 4.2 手工创建最小应用

最小创建流程如下：

1. 创建应用目录
2. 创建 `src/` 子目录
3. 添加 `src/main.c`
4. 编写 `CMakeLists.txt`
5. 创建 `prj.conf`
6. 如有需要，创建 `app.overlay`
7. 根据需要增加测试、文档、脚本和自定义模块

最小 `CMakeLists.txt` 可写为：

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr)
project(my_zephyr_app)

target_sources(app PRIVATE src/main.c)
```

关键约束如下：

- `find_package(Zephyr)` 用于接入 Zephyr 构建系统
- `project(...)` 必须位于 `find_package(Zephyr)` 之后
- `target_sources(app ...)` 必须位于 `find_package(Zephyr)` 之后，因为 `app` 目标由 Zephyr 提供

注意：不支持在路径中包含空格的目录中创建或构建 Zephyr 应用。

---

## 5. 应用的 CMakeLists.txt

每个应用都必须有顶层 `CMakeLists.txt`。它既是应用入口，也是 Zephyr 构建入口。

建议按以下顺序组织：

1. 如有需要，设置 `BOARD`
2. 如有需要，设置 `CONF_FILE`
3. 如有需要，设置 `DTC_OVERLAY_FILE`
4. 如有自定义应用 Kconfig，可准备 `Kconfig`
5. 调用 `find_package(Zephyr)`
6. 调用 `project(...)`
7. 通过 `target_sources(app ...)` 添加源文件

如果应用只固定支持一个 board，可以在 `CMakeLists.txt` 中设置：

```cmake
set(BOARD qemu_x86)
```

如果需要多个 Kconfig 片段：

```cmake
set(CONF_FILE "fragment_file1.conf")
list(APPEND CONF_FILE "fragment_file2.conf")
```

如果要在应用中引入额外的 board、SoC 或 DTS 根目录，应在 `find_package(Zephyr)` 之前设置相应变量。

---

## 6. 重要构建变量

Zephyr 官方文档中最重要的构建变量如下。

### 6.1 三个基础变量

- `BOARD`
- `CONF_FILE`
- `DTC_OVERLAY_FILE`

这三个变量可通过以下三种方式提供，优先级从高到低为：

1. 命令行 `-D<VAR>=...`
2. 环境变量
3. `CMakeLists.txt` 中的 `set(...)`

说明：

- 若同一 build 目录已缓存旧值，`CMakeCache.txt` 可能继续影响后续构建
- `DTC_OVERLAY_FILE` 若包含多个 overlay，应使用分号分隔；在命令行中通常需要加引号，例如：`"file1.overlay;file2.overlay"`

### 6.2 常用变量列表

| 变量 | 作用 |
| --- | --- |
| `ZEPHYR_BASE` | 指定 Zephyr 基础目录；`find_package(Zephyr)` 通常会自动解析 |
| `BOARD` | 指定目标板卡 |
| `CONF_FILE` | 指定一个或多个 Kconfig 配置片段 |
| `EXTRA_CONF_FILE` | 在默认 `prj.conf` 基础上额外追加配置片段 |
| `DTC_OVERLAY_FILE` | 指定一个或多个 devicetree overlay |
| `EXTRA_DTC_OVERLAY_FILE` | 在默认 overlay 基础上追加 overlay |
| `SHIELD` | 指定 shield |
| `ZEPHYR_MODULES` | 显式指定完整模块列表，会替代 west 自动发现的模块集合 |
| `EXTRA_ZEPHYR_MODULES` | 在 west 自动发现结果基础上额外追加模块 |
| `FILE_SUFFIX` | 为 `.conf` 和 overlay 文件增加后缀选择机制 |
| `APPLICATION_CONFIG_DIR` | 指定应用配置目录 |

---

## 7. 应用配置机制

Zephyr 应用配置主要包括：

- Kconfig 配置
- devicetree overlay
- 应用配置目录
- 文件后缀机制

### 7.1 Kconfig 配置

应用级配置通常写在 `prj.conf` 中，例如：

```conf
CONFIG_CPP=y
```

如果应用只是在现有 Zephyr Kconfig 选项上赋值，通常只需 `prj.conf` 即可。

如果应用需要定义自己的 Kconfig 选项，应在应用目录中新增 `Kconfig` 文件，并由其 `source "Kconfig.zephyr"` 接入系统。

### 7.2 Devicetree overlay

应用级硬件差异通常放在 overlay 中，例如 `app.overlay`。其适用场景包括：

- 修改某个外设的 `status`
- 为样例或应用启用额外节点
- 调整 `chosen`
- 添加外部器件节点

如果应用需要为特定 board 单独覆盖设备树，通常还会结合 `boards/<board>.overlay` 或变量 `DTC_OVERLAY_FILE` 使用。

### 7.3 APPLICATION_CONFIG_DIR

应用配置目录由 `APPLICATION_CONFIG_DIR` 指定。优先级如下：

1. 命令行 `-DAPPLICATION_CONFIG_DIR=<path>` 或在 `find_package(Zephyr)` 前设置
2. 应用源目录本身

这决定了 Zephyr 默认从哪个目录寻找配置文件。

### 7.4 FILE_SUFFIX

`FILE_SUFFIX` 用于支持单一代码库下的多产品、多变体配置。

例如工程中存在：

```text
<app>
├── prj.conf
├── prj_mouse.conf
└── boards
    ├── native_sim.overlay
    └── qemu_cortex_m3_mouse.overlay
```

当设置 `FILE_SUFFIX=mouse` 时：

- Kconfig 优先使用 `prj_mouse.conf`
- overlay 优先使用带后缀的 board overlay
- 若不存在带后缀的文件，则回退到默认文件

该机制适合处理产品型号、调试版与发布版、不同外设组合等场景。

---

## 8. 应用源码组织

应用源码通常放在 `src/` 目录下，并可按需要继续划分子目录。

建议原则如下：

- 将应用源码与构建文件分离
- 将业务代码按模块拆分到 `src/` 子目录
- 避免使用内核保留的符号命名前缀

如果需要引入第三方库代码，可以放在 `src/` 外部，但必须保证：

- 与当前应用使用相同 ABI
- 使用兼容的编译器选项
- 如有需要，能够访问 Zephyr 头文件和编译定义

Zephyr 提供一组 CMake 扩展函数，便于向第三方构建系统传递编译器、归档器、架构和 board 等信息。

---

## 9. 构建应用

Zephyr 构建分为两个阶段：

1. 使用 `cmake` 生成构建系统
2. 使用底层构建工具执行实际编译

常用方式是通过 `west build` 间接调用 CMake 和 Ninja。

### 9.1 使用 west 构建

```sh
west build -b <board> <app>
```

例如：

```sh
west build -b reel_board samples/hello_world
```

### 9.2 使用 CMake 和 Ninja 构建

```sh
cmake -Bbuild -GNinja -DBOARD=<board> <app>
ninja -Cbuild
```

### 9.3 使用备用配置文件构建

如果需要使用默认 `prj.conf` 之外的配置片段，可通过：

```sh
west build -b <board> <app> -- -DCONF_FILE=prj.alternate.conf
```

### 9.4 build 目录的关键内容

构建目录中重点关注：

- `build/CMakeCache.txt`
- `build/zephyr/.config`
- `build/zephyr/.config.old`
- `build/zephyr/zephyr.dts`
- `build/zephyr/zephyr.elf`
- `build/zephyr/*.hex`
- `build/zephyr/*.bin`

其中：

- `.config` 反映最终 Kconfig 结果
- `zephyr.dts` 反映最终设备树结果
- `zephyr.elf` 是最终链接产物

---

## 10. 重新构建与清理

开发过程中应频繁执行增量构建。Zephyr 默认只重编受影响的部分。

若需要清理构建产物：

### 10.1 保留配置并清理构建结果

```sh
west build -t clean
```

或：

```sh
ninja clean
```

### 10.2 完全清空构建目录

```sh
west build -t pristine
```

或：

```sh
ninja pristine
```

`pristine` 会删除包括 `.config` 在内的生成内容，适用于：

- 更换 board
- 更换关键配置
- CMake cache 污染
- 怀疑构建系统未正确重建

---

## 11. 运行与下载

### 11.1 下载到真实板卡

应用构建完成后，可在 build 目录中执行：

```sh
west flash
```

或：

```sh
ninja flash
```

是否支持 `flash` 取决于 board 支持文件中的 runner 配置，例如 `board.cmake`。

### 11.2 在模拟器中运行

对于支持 QEMU 的 board，可执行：

```sh
west build -t run
```

或：

```sh
ninja run
```

若要显式指定模拟器目标，可使用：

- `run_qemu`

### 11.3 构建 board revision

如果 board 支持 revision，可使用如下形式：

```text
<board>@<revision>
<board>@<revision>/<qualifiers>
```

例如：

```sh
west build -b nrf9160dk@0.14.0/nrf9160/ns
```

---

## 12. 应用内自定义 Board、SoC 和 Devicetree

当目标硬件尚未进入 Zephyr 主树时，可以在应用仓库内部维护：

- `boards/`
- `soc/`
- `dts/`

官方推荐的目录组织方式与 Zephyr 主树保持一致，以便后续上游提交。

### 12.1 自定义 board

应用内 board 支持通常位于：

```text
boards/<vendor>/<board>/
```

典型文件包括：

- `<board>_defconfig`
- `<board>.dts`
- `<board>.yaml`
- `board.cmake`
- `board.h`
- `CMakeLists.txt`
- `Kconfig.<board>`
- `Kconfig.defconfig`
- `doc/`
- `support/`

构建时通过 `BOARD_ROOT` 引入：

```sh
west build -b <board> -- -DBOARD_ROOT=<path to boards>
```

也可以在应用 `CMakeLists.txt` 中设置 `BOARD_ROOT`，但必须放在 `find_package(Zephyr)` 之前，且该路径应为绝对路径。

### 12.2 自定义 SoC

应用内 SoC 支持通常位于：

```text
soc/<vendor>/<family-or-series>/...
```

通过 `SOC_ROOT` 引入：

```sh
west build -b <board> -- -DSOC_ROOT=<path to soc> -DBOARD_ROOT=<path to boards>
```

### 12.3 自定义 devicetree 根目录

可通过额外的 DTS 树根补充：

```text
include/
dts/common/
dts/<arch>/
dts/bindings/
```

通过 `DTS_ROOT` 引入：

```sh
west build -b <board> -- -DDTS_ROOT=<path to dts root>
```

如果需要控制 DTS 预处理阶段的宏，也可以设置 `DTS_EXTRA_CPPFLAGS`。

---

## 13. 与当前工作区的对应关系

当前工作区中，`zephyr-example/f411ceu6` 的组织方式符合 Zephyr 官方推荐的 workspace application 结构。

该目录下已存在：

- `boards/`：用于维护本地 board 定义
- `build/`：独立构建输出目录
- `blinky/`：具体应用或示例代码目录

这说明当前工程采用的是典型的 **应用仓库承载 board 定义** 的模式，即：

- SoC 支持优先复用 `zephyr/` 主树已有实现
- 板级差异在应用仓库内通过 `boards/` 维护
- 构建时通过 `BOARD_ROOT` 将应用内 board 接入构建系统

这种方式适合：

- 板卡尚未上游
- 项目需要维护私有 board 变体
- 一个应用仓库需要同时管理代码与硬件定义

---

## 14. 结论

Zephyr 的应用开发应把以下几个层面明确区分：

- `CMakeLists.txt`：构建入口与源文件组织
- `prj.conf`：软件配置
- `app.overlay` 或其他 overlay：硬件增量配置
- `boards/`、`soc/`、`dts/`：应用内硬件扩展
- `build/`：独立构建输出

对实际项目而言，建议遵循以下原则：

1. 优先使用 workspace application 组织方式
2. 优先复用 Zephyr 主树已有 SoC 支持
3. 将板级差异放在应用内 `boards/`
4. 将软件差异放在 `prj.conf` 和其他配置片段
5. 将硬件差异放在 overlay 与 DTS 扩展中
6. 用 `west build`、`west flash`、`west debug` 作为统一工作流入口

当应用规模扩大后，建议进一步补充：

- `Kconfig`
- `boards/<board>.overlay`
- `tests/`
- `doc/`
- `west.yml`
- 私有模块与驱动目录

这是从最小可构建应用演进到完整产品工程的标准路径。
