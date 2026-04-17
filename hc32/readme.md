# HC32F460PETB Zephyr Port

Zephyr RTOS port for HC32F460PETB development board (HDSC/Xiaohua Cortex-M4F MCU).

## Features

- **LED blink**: LED3 (PD10) and LED4 (PE15) toggle alternately every 500ms
- **Button interrupt**: BTN1 (PE2) triggers EXTINT2 on falling edge, prints press count
- **UART console**: USART2 (PA2 TX / PA3 RX) at 115200 8N1, output via DAPLink VCP

## Hardware

| Resource | Pin   | Notes                              |
|----------|-------|------------------------------------|
| LED3     | PD10  | Active-low (VDD → 1kΩ → LED → pin) |
| LED4     | PE15  | Active-low                         |
| BTN1     | PE2   | Active-low, internal pull-up       |
| USART2 TX| PA2   | DAPLink VCP forwarding             |
| USART2 RX| PA3   | Shared with BTN0 (not used)        |
| Debugger | —     | CMSIS-DAP (CherryUSB DAPLink)      |

**MCU**: HC32F460PETB — Cortex-M4F, 512KB Flash, 192KB SRAM, running at 8MHz (MRC)

## Project Structure

```
hc32f460petb/
├── CMakeLists.txt          # Top-level, sets SOC/BOARD/DTS roots
├── prj.conf                # Kconfig: GPIO, SERIAL, CONSOLE, DYNAMIC_INTERRUPTS
├── src/main.c              # Application: LED blink + button interrupt + printk
├── boards/hdsc/hc32f460petb/
│   ├── board.yml           # Board metadata
│   ├── board.cmake         # pyOCD flash runner
│   ├── hc32f460petb.dts    # Board devicetree (LEDs, buttons, USART2)
│   ├── hc32f460petb_defconfig
│   └── Kconfig.hc32f460petb
├── soc/hdsc/hc32f460/
│   ├── soc.yml / soc.c / soc.h
│   ├── Kconfig.soc / Kconfig / Kconfig.defconfig
│   ├── CMakeLists.txt      # DDL library integration
│   ├── hc32f4xx_conf.h     # DDL module enables
│   └── drivers/            # SoC-level driver Kconfig/CMake
├── dts/
│   ├── arm/hdsc/hc32f460.dtsi   # SoC devicetree
│   └── bindings/                # DT bindings for GPIO, UART, Flash
├── drivers/
│   ├── gpio/gpio_hc32.c   # GPIO driver with EXTINT interrupt support
│   └── serial/uart_hc32.c # USART polling driver
└── readme.md               # This file
```

## Prerequisites

- **Zephyr SDK** at `/home/firebot/zephyr-sdk-1.0.1` (arm-zephyr-eabi toolchain)
- **Zephyr** source tree at `/home/firebot/zephyrproject/zephyr`
- **Python venv** with west, pyOCD: `source /home/firebot/zephyrproject/.venv/bin/activate`
- **pyOCD** with HC32F460 target support (pack: `hc32f460xe`)
- **DDL library** at `../HC32F460_DDL_Rev3.3.0/` (device headers, LL drivers)

## Build

```bash
source /home/firebot/zephyrproject/.venv/bin/activate
cd /home/firebot/zephyrproject/zephyr-example/hc32f460petb

# Clean build
west build -b hc32f460petb . --build-dir build

# Incremental rebuild
west build --build-dir build
```

## Flash

```bash
pyocd load -t hc32f460xe build/zephyr/zephyr.hex
```

Or reset after flash:
```bash
pyocd load -t hc32f460xe build/zephyr/zephyr.hex && pyocd reset -t hc32f460xe
```

## Serial Monitor

The DAPLink provides a USB CDC ACM serial port at `/dev/ttyACM0`:

```bash
# Using the project serial monitor tool
python3 ../f411ceu6/tools/serial_monitor.py --port-hint Cherry

# Or directly with picocom/minicom
picocom /dev/ttyACM0 -b 115200

# Quick read with Python
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
while True:
    line = s.readline()
    if line: print(line.decode('utf-8', errors='replace').strip())
"
```

Expected output after reset:
```
*** Booting Zephyr OS build 149c8b1758a8 ***
HC32F460 Zephyr demo starting
Button interrupt configured (falling edge)
LED0 (PD10), LED1 (PE15), BTN1 (PE2) configured
alive (led=0)
alive (led=1)
[BTN1] pressed (count=1)    ← when button is pressed
alive (led=0)
...
```

## Debug with GDB

```bash
# Terminal 1: Start GDB server
pyocd gdbserver -t hc32f460xe --port 3333

# Terminal 2: Connect GDB
arm-zephyr-eabi-gdb build/zephyr/zephyr.elf \
  -ex "target remote :3333" \
  -ex "monitor reset halt" \
  -ex "break main" \
  -ex "continue"
```

### Useful GDB commands

```gdb
# Read GPIO/INTC registers
x/1xh 0x40053BFC        # PWPR (should be 0x0001 = unlocked)
x/1xh 0x40053D08        # PCR for PE2 (should have INTE bit set)
x/1xw 0x40051064        # INTC SEL[2] (should be 2 = EIRQ2)
x/1xw 0x40051018        # EIRQCR2 (trigger config)
x/1xw 0x40051054        # EIFR (pending flags)

# Simulate button press via SWIER
set *(uint32_t*)0x4005129C = 0x04

# Check USART2 registers
x/1xw 0x40028008        # CR1
x/1xw 0x40028000        # BRR
```

### Register inspection with pyOCD

```bash
pyocd commander -t hc32f460xe \
  -c "read32 0x40053BFC" \
  -c "read16 0x40053D08" \
  -c "read32 0x40051064" \
  -c "exit"
```

## Architecture Notes

### HC32F460 Interrupt Model (INTC)

The HC32F460 has a unique interrupt controller (INTC) that multiplexes ~512 internal
interrupt sources to 128 NVIC IRQ lines via programmable SEL registers:

- **16 EXTINT channels** (0-15): One per pin number, shared across all GPIO ports
- **EIRQCR[n]**: Per-channel trigger config (falling/rising/both/low-level)
- **PCR.INTE**: Per-pin bit that enables routing to the EXTINT channel
- **INTC_SEL[n]**: Maps interrupt source N to NVIC IRQ N
- **EIFR / EIFCR**: Interrupt flags and flag clear registers

For button PE2: EXTINT channel 2 → INTC_SEL[2] = INT_SRC_PORT_EIRQ2 → NVIC IRQ 2

### GPIO Register Layout

Single register block at 0x40053800 with per-port stride of 0x10 for data registers
and per-pin configuration (PCR/PFSR) at offset 0x0400.

### USART Key Settings

- **CR1 bit 31 (SBS)**: Must be set for UART mode (start bit = falling edge detect)
- **PFSR BFE bit**: Do NOT set for USART pins — interferes with GPIO input reads
- **PCR DRV**: Set high drive strength on TX pins for reliable operation

### Write Protection

GPIO PWPR, PWC FPRC, FCG0PC, and EFM FAPRT must all be unlocked in `soc_early_init_hook()`
before any peripheral configuration. See `soc/hdsc/hc32f460/soc.c`.

## Status

- [x] LED output (PD10, PE15)
- [x] UART console (USART2, 115200 8N1)
- [x] Button interrupt (PE2, EXTINT falling edge)
- [x] Boot banner and printk logging
- [ ] Clock configuration (currently uses 8MHz MRC default)
- [ ] Low-power modes
- [ ] Additional peripherals (SPI, I2C, ADC, Timer, etc.)