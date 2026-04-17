# Adding a new HC32 SoC variant to this out-of-tree port

This document captures the steps followed when the HC32F4A0 was added on top of
the existing HC32F460 port.  Use it as a checklist when porting another
Huada/XHSC Cortex-M SoC that ships with the same `hc32_ll_*` DDL library.

---

## 1. Pre-flight: gather the vendor assets

1. Obtain the DDL release zip, e.g. `HC32F4A0_DDL_Rev2.4.0.zip`.
2. Un-zip it next to the existing DDL(s) so the tree looks like:

   ```
   zephyr-example/
   ├── HC32F460_DDL_Rev3.3.0/
   ├── HC32F4A0_DDL_Rev2.4.0/   <-- NEW
   └── hc32f/
   ```

3. Open the reference manual and the datasheet (both PDFs sit in the vendor
   distribution).  You will need them for: FCG register bit assignments, pin
   alternate-function numbers, interrupt-source SEL numbers, PLL limits, ICG
   table shape, USB variant (FS/HS).

## 2. Create the SoC tree

Mirror `hc32f/soc/hdsc/hc32f460/` into `hc32f/soc/hdsc/<new-soc>/`.  The files
that always need tweaking are:

| File                 | What to change                                               |
|----------------------|--------------------------------------------------------------|
| `soc.yml`            | `name:` of the SoC                                           |
| `Kconfig.soc`        | `SOC_<NEW>` symbol and `SOC_FAMILY_HC32` membership          |
| `Kconfig`            | `rsource` back to `../../../drivers/Kconfig` – required!     |
| `Kconfig.defconfig`  | `SOC` string, `NUM_IRQS`, SRAM base, etc.                    |
| `CMakeLists.txt`     | `HC32_DDL_ROOT` to the new DDL tree; add required `.c` files |
| `hc32f4xx_conf.h`    | Enable only the LL modules your board uses                   |
| `icg_config.c`       | Table length + `CFG*_CONST` composition (see §4)             |
| `icg_rom_start.ld`   | `. = __rom_region_start + 0x400;` offset is usually same     |
| `power.c`            | WKT clock-source symbol names (§5)                           |
| `soc.c`              | `SystemInit()` / `LL_PERIPH_WE(LL_PERIPH_ALL)` is normally fine |

### Required `Kconfig` content
The shared drivers tree (`hc32f/drivers/`) is ultimately included via the SoC's
Kconfig.  The minimum SoC `Kconfig` is:

```kconfig
# SPDX-License-Identifier: Apache-2.0
config SOC_HC32F4A0
	select ARM
	select CPU_CORTEX_M4
	select CPU_HAS_ARM_MPU
	select CPU_HAS_FPU
	select DYNAMIC_INTERRUPTS
rsource "../../../drivers/Kconfig"
```

If you forget the `rsource`, Zephyr will silently skip the hc32 drivers and
you will get unresolved `DEVICE_DT_GET` references at link time.

## 3. Create the DTS

1. Copy `dts/arm/hdsc/hc32f460.dtsi` to `dts/arm/hdsc/<new-soc>.dtsi`.
2. Update each peripheral's `reg`, `interrupts`, `clocks`, `irqs` entries
   against the reference manual.  The most error-prone fields are:
   * USART base addresses (F4A0 has a different FCG bank than F460)
   * TMRA base addresses (F4A0 has more instances, each with its own base)
   * Flash size and SRAM layout (F4A0 SRAM is a single 364 KB contiguous
     region starting at `0x20000000`; F460 has `0x1FFF8000` SRAMH + `0x20000000`
     SRAM12 split)
3. Update the `clocks { clk_hxt: ... }` node if the board uses a different
   crystal frequency.
4. Update the PLL `m`/`n`/`p`/`q`/`r` divider properties — F4A0's upper bound
   is 240 MHz while F460 tops out at 200 MHz.

## 4. ICG table differences

The **ICG** (Initial Configuration Gate) table sits in the first KB of flash
(`.icg_sec` at `__rom_region_start + 0x400`).  It has a **different length and
layout on every SoC**:

| SoC        | Entries | Non-reserved slots                         |
|------------|---------|--------------------------------------------|
| HC32F460   | 8       | CFG0 (WDT+SWDT), CFG1 (NMI+BOR+HRC)        |
| HC32F4A0   | 24      | CFG0, CFG1 (BOR+HRC, **no NMI**), CFG3 (FLASH_PROTECT) |

The DDL already provides sensible `ICG_REG_CFGx_CONST` defaults in
`hc32_ll_icg.h`.  Override only the entry that depends on your SWDT/WDT
settings (we override `CFG0_CONST` and leave the rest to the DDL).

## 5. PWC / power-mode APIs are renamed

F460 uses **HighPerformance ↔ HighSpeed** transitions.  F4A0 uses
**LowSpeed ↔ HighSpeed** transitions.  They cover the same conceptual
boundary (lowering the voltage before dropping the clock), but the function
names differ.  The shared `clock_control_hc32.c` uses `#if defined(HC32F460) /
#elif defined(HC32F4A0)` switches for this.

Other rename gotchas seen during this port:

| F460                           | F4A0                                |
|--------------------------------|-------------------------------------|
| `PWC_WKT_CLK_SRC_LRC`          | `PWC_WKT_CLK_SRC_RTCLRC`            |
| `PWC_HighSpeedToHighPerformance`| `PWC_LowSpeedToHighSpeed`          |
| `PWC_HighPerformanceToHighSpeed`| `PWC_HighSpeedToLowSpeed`          |
| `EFM_CacheCmd(ENABLE)`         | `EFM_ICacheCmd()` + `EFM_DCacheCmd()` |
| `SRAM_SRAM12 / SRAM_SRAM3 / SRAM_SRAMR` | `SRAM_SRAM123 / SRAM_SRAM4 / SRAM_SRAMB` |
| `ICG_REG_NMI_CONFIG`           | *(not present, drop it from CFG1)*  |

Grep the `drivers/` tree for these and add a `#if defined(HC32xxx)` branch as
needed.

## 6. Create the board

Under `hc32f/boards/hdsc/<board-name>/`:

```
board.yml        # name, full_name, vendor=hdsc, socs: [- name: <soc>]
board.cmake      # include pyOCD/JLink runner; --target=<pack name>
Kconfig.<name>   # select SOC_<NEW>
<name>_defconfig # enable CLOCK_CONTROL, GPIO, PINCTRL, SERIAL, CONSOLE, UART_CONSOLE
<name>.dts       # /dts-v1/ + include dtsi; pin-ctrl nodes; chosen console
```

The DTS must declare a `pinctrl { ... }` node with sub-nodes for every UART
(or other peripheral) pin-mapping it uses.  Each sub-node's `pinmux` list
uses the `HC32_PINMUX(port, pin, func)` macro.  The `func` value comes from
`include/zephyr/dt-bindings/pinctrl/hc32-pinctrl-func.h`.

## 7. Parameterise the shared drivers

The drivers under `hc32f/drivers/` are SoC-agnostic in the DTS-visible
sense, but they still call DDL APIs.  When a DDL call exists on both SoCs
with identical semantics, leave it alone.  When the symbol names differ,
wrap them in `#if defined(HC32xxx)` as shown in §5.

For **peripheral availability** differences (e.g. F460 has USART1-4, F4A0 has
USART1-10), the USART driver uses per-instance `#if defined(INT_SRC_USARTn_RI)`
guards so that only the INTC entries valid on the current SoC are compiled
in.  When adding a new SoC the `hc32_ll_interrupts.h` header in the DDL
provides these symbols automatically.

## 8. Smoke-test the build

```sh
cd hc32f
source /home/firebot/zephyrproject/.venv/bin/activate
west build -b <board-name> . --build-dir build --pristine=always
```

When it links, flash a simple console-heartbeat app and confirm that the
`printk` banner appears on the chosen console UART at the expected baud
rate.  If the clock tree isn't right the UART divider will be off and you
will see garbled characters — good sign that the PLL is up but divider is
wrong.

## 9. Cross-checking the original port

After adding a new SoC **always rebuild the existing board(s)** with
`west build -b <other-board> . --pristine=always` to make sure the
`#if defined(HC32xxx)` branches you added didn't regress anything.
