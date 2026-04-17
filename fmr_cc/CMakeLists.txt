# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Point Zephyr at the hc32f/ out-of-tree SoC + board tree (sibling directory).
set(HC32F_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hc32f)
list(APPEND SOC_ROOT   ${HC32F_ROOT})
list(APPEND BOARD_ROOT ${HC32F_ROOT})
list(APPEND DTS_ROOT   ${HC32F_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fmr_cc)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
	src/main.c
	src/app_msg.c
	src/crc16_modbus.c
	src/doraemon_pack.c
	src/led_statu.c
	src/wbus.c
	src/imu5115.c
	src/moto.c
	src/servo.c
	src/comm_485_lift.c
	src/manual_ctrl.c
	src/battery.c
	src/usb_uplink.c
)
