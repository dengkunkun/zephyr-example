# HC32F460PETB IWDG (SWDT) Bring-up

## Scope
- Zephyr `watchdog` support for the HC32 **SWDT**
- HC32 `SWDT` is the closest match to an **independent watchdog**
- Timeout is fixed at build time through the HC32 **ICG** words in flash

## Why SWDT maps to IWDG
- `SWDT` runs from the dedicated **10 kHz SWDTLRC** clock, not from the PLL/PCLK tree
- It keeps the watchdog independent from the main system clock
- Runtime control is intentionally narrow:
  - start on first feed
  - read count/status
  - feed periodically
  - timeout mode decided by ICG preload

## ICG integration
- The vendor DDL placed the ICG words in `.icg_sec`, but the Zephyr linker was not emitting that section at flash **0x400**
- This port now:
  - replaces vendor `hc32_ll_icg.c` with `soc/hdsc/hc32f460/icg_config.c`
  - adds `soc/hdsc/hc32f460/icg_rom_start.ld`
  - keeps `.icg_sec` at **0x400** before `.text`
- The board uses:
  - reset-stop mode after reset
  - reset on timeout
  - `counter-cycles = <16384>`
  - `clock-divider = <16>`
  - `run-in-sleep`
  - the driver starts SWDT on the first `wdt_setup()` feed
- Using SWDT auto-start from reset caused HC32 WKTM power-down wake to fail on this board, so the final ICG payload uses `ICG_SWDT_RST_STOP`

## Devicetree model
- Binding: `hdsc,hc32-swdt`
- SoC node: `swdt0: watchdog@40049400`
- Board enables the node and exposes alias `iwdg0 = &swdt0`

## Driver behavior
- File: `drivers/watchdog/wdt_hc32_swdt.c`
- Supports one Zephyr watchdog channel
- `wdt_install_timeout()` validates the requested timeout against the fixed ICG preload
- `wdt_setup()`:
  - optionally freezes the watchdog while the CPU is halted by the debugger
  - applies debugger-stop behavior
  - feeds SWDT once to start it from the reset-stop ICG state
- `wdt_disable()` returns `-EPERM` because the hardware watchdog cannot be stopped after it has been started

## Runtime tests
- Boot self-tests:
  - `swdt-setup`
  - `swdt-iwdg`
- Shell commands:
  - `watchdog info`
  - `watchdog feed swdt`
  - `test watchdog`

## Current limitations
- Timeout is fixed by devicetree + ICG and is not runtime-programmable
- Only the reset-path configuration is used on the current board
- A destructive timeout/reset validation is deferred; current tests are non-destructive count/feed checks
