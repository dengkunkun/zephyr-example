# FMR_CC — Zephyr port

A rewrite of the original HC32F4A0 bare-metal FMR_CC project on top of
Zephyr RTOS, using the `hc32f/` out-of-tree SoC/board support layer in the
sibling directory.

## Hardware

Target board: **HC32F4A0PGTB** (HDSC HC32F4A0 LQFP176, Cortex-M4F @240MHz).
Business I/O matches the original wiring — see
`../hc32f/doc/fmr_cc_business.md` for the full pin map.  TL;DR:

| Peripheral | Pins                   | Purpose                               |
|------------|------------------------|---------------------------------------|
| USART3     | PE14/PE15              | Debug console (`zephyr,console`)      |
| USART1     | PD10/PD11              | IMU-5115 uplink                       |
| USART2     | PD8/PD9                | Radio/WBUS receiver (100k 8E2 inv.)   |
| USART5     | PA8/PA11 + PA9/PA10    | 485 lift (Modbus-RTU)                 |
| USART6     | PD4/PD7 + PD5/PD6      | Motor (Modbus-RTU)                    |
| USART7     | PE2/PE5 + PE3/PE4      | Feetech servo                         |
| PB7/8/9    |                        | 74HC595 LED shift register (bit-bang) |
| PA0        |                        | Battery ADC (**driver TODO**)         |
| PB13/14/15 |                        | USB HS (**driver TODO**)              |

## Build & flash

```sh
source /home/firebot/zephyrproject/.venv/bin/activate
cd /home/firebot/zephyrproject/zephyr-example/fmr_cc
west build -b hc32f4a0pgtb . --pristine=always
pyocd load -t hc32f4a0xi build/zephyr/zephyr.hex
```

## Project layout

```
fmr_cc/
├── CMakeLists.txt
├── prj.conf
├── Kconfig               # CONFIG_APP_FMR_* toggles per module
├── boards/
│   └── hc32f4a0pgtb.overlay   # enables USART1/2/5/6/7 on the board
└── src/
    ├── main.c            # startup & module wiring
    ├── app_msg.c/h       # k_msgq-backed app event bus
    ├── crc16_modbus.c/h  # ModBus CRC16
    ├── doraemon_pack.c/h # framing header/CRC for the PC uplink protocol
    ├── led_statu.c/h     # 74HC595 bit-bang LED status
    ├── wbus.c/h          # SBUS/WBUS RC receiver
    ├── imu5115.c/h       # IMU frame decoder
    ├── moto.c/h          # Motor Modbus-RTU master
    ├── servo.c/h         # Feetech servo master
    ├── comm_485_lift.c/h # Lift Modbus-RTU master
    ├── manual_ctrl.c/h   # RC → actuators mapping
    ├── battery.c/h       # ADC sampler (stub, needs driver)
    └── usb_uplink.c/h    # CDC uplink (stub, falls back to console)
```

## Module status

- ✅ compile-clean, logic faithfully ported
- 🟡 compile-clean, simplified vs. original (see TODOs in source)
- ❌ stubbed, pending driver support
- ⏳ on-target validation still required for every module

| Module         | Status | Notes                                                 |
|----------------|--------|-------------------------------------------------------|
| app_msg        | ✅     | `k_msgq` with `struct app_msg { enum type; union; }`  |
| led_statu      | ✅     | bit-bang @ ~100 Hz via `k_work_delayable`             |
| crc16 / pack   | ✅     | straight port of original                             |
| wbus           | 🟡⏳   | UART IRQ mode; 100 kbps 8E2 inverted needs confirm    |
| imu5115        | 🟡⏳   | UART IRQ mode; parser port                            |
| moto           | 🟡⏳   | thread + msgq; RS485 DE/RE via GPIO                   |
| servo          | 🟡⏳   | thread + msgq                                         |
| comm_485_lift  | 🟡⏳   | thread + msgq                                         |
| manual_ctrl    | ✅     | pure logic                                            |
| battery        | ❌     | awaits `adc_hc32` driver                              |
| usb_uplink     | ❌     | awaits HC32F4A0 USB device-controller driver          |

## Known gaps vs. the original

1. **No ADC driver** — battery monitoring is disabled in `prj.conf`.  The
   Zephyr `adc` API glue to the HC32 `hc32_ll_adc` DDL is a future task
   (`drv-adc-hc32`).
2. **No USB driver** — CDC uplink is disabled in `prj.conf`; the uplink
   currently falls back to the debug UART (USART3).  Bringing up the HC32
   USB device controller (`OTG_HS`) is the largest remaining driver task
   (`drv-usb-hc32`).
3. **UART parity/inversion** — the shared USART driver does not yet expose
   the 8E2 and RX-inversion bits required by the WBUS receiver.  The
   overlay currently configures plain 8N1 at 100000 baud; on real hardware
   you will need to extend the driver bindings or do a one-off DDL config
   call in `wbus_init()`.
4. **DMA** — UART driver supports DMA internally on F460 but the Zephyr
   `dma` subsystem isn't wired in.  All ports of the original here use IRQ
   mode which is functionally equivalent at the tested data rates.
5. **TMRA timer-trigger** — the original used Timer0/TMRA for Modbus
   inter-frame timing.  The Zephyr ports use `k_timer` / `k_timeout_t` on
   the system tick which is fine for 9600–115200 baud Modbus-RTU but gives
   up ~40 µs resolution.  Revisit if a higher-baud bus is ever added.

## Where to look first during code review

1. `src/main.c` — boot order and thread startup
2. `src/app_msg.{c,h}` — message bus shape, keeps the rest of the modules
   decoupled
3. `src/wbus.c` — proves the UART IRQ approach works under unusual line
   settings
4. `boards/hc32f4a0pgtb.overlay` — peripheral enable + pinctrl wiring

For the SoC port itself see `../hc32f/doc/review_hc32f460.md` and
`../hc32f/doc/add_new_soc.md`.
