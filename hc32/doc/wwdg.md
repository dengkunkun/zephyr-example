# HC32F460PETB WWDG (WDT) Bring-up

## Scope
- Zephyr `watchdog` support for the HC32 **WDT**
- HC32 `WDT` is used as the board’s **window watchdog**
- Driver supports:
  - runtime timeout selection
  - contiguous feed windows
  - reset mode
  - interrupt mode with a Zephyr callback

## Why WDT maps to WWDG
- HC32 `WDT` is clocked from **PCLK3**, not the independent 10 kHz SWDT clock
- It exposes a programmable refresh window and reset/interrupt response at runtime
- That matches Zephyr’s window-watchdog shape better than SWDT

## Clock / devicetree model
- Binding: `hdsc,hc32-wdt`
- SoC node: `wdt0: watchdog@40049000`
- The node uses a **rate-only** clock selector:
  - `clocks = <&clk 0x15 0>` -> query **PCLK3**
- Board enables the node and exposes alias `wwdg0 = &wdt0`

## Driver design
- File: `drivers/watchdog/wdt_hc32.c`
- The driver searches the HC32 hardware combinations:
  - count period: `256 / 4096 / 16384 / 65536`
  - divider: `4 .. 8192`
  - contiguous refresh ranges derived from the HC32 remaining-count windows
- Requested Zephyr windows are mapped to the nearest HC32 window that fully contains the requested `[min, max]` interval

## Interrupt routing
- HC32 routes watchdog events through the INTC SEL matrix before NVIC
- The board maps:
  - `INT_SRC_WDT_REFUDF = 439`
  - to NVIC IRQ **24**
- `wdt_setup(WDT_OPT_PAUSE_HALTED_BY_DBG)` sets the HC32 debug-stop bit so GDB halt does not trip the watchdog

## Runtime tests
- Boot self-tests:
  - `wdt-setup`
  - `wdt-window`
- Shell commands:
  - `watchdog info`
  - `watchdog feed wdt`
  - `test watchdog`

## Current limitations
- One watchdog channel only
- Non-contiguous HC32 refresh windows are intentionally not exposed through Zephyr’s single contiguous window API
- A destructive reset-window test is deferred; current validation checks start/count/feed behavior on hardware
