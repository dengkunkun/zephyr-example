/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file usb_uplink.h
 * @brief USB/console uplink driver header.
 *
 * Transmits periodic status frames and receives command frames.
 * Original used USB CDC; Zephyr port falls back to printk/shell UART
 * when CONFIG_APP_FMR_USB_UPLINK is not set or when a USB CDC class is
 * unavailable.
 *
 * Frame format (0x39 0x93 uplink — from original usb_uplink.c):
 *   TX: 0x39 0x93 | CMD(1) | LEN_H | LEN_L | DATA(LEN) | CRC_H | CRC_L
 *   RX: same layout
 *   CRC16-Modbus over DATA only.
 *
 * Command IDs (from original usb_uplink.h):
 *   0x01  UPLOAD_REP   — periodic status telemetry
 *   0x02  UPLOAD_IMU   — IMU frame
 *   0x10  LIFT_CALIBRATE
 *   0x11  LIFT_SET_POS
 *   0x12  LIFT_GET_MAX_REP
 *   0x20  SERVO_SET_POS_1..3
 */

#ifndef USB_UPLINK_H
#define USB_UPLINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Uplink command IDs (wire-compatible with original) */
enum usblink_cmd {
	USBLINK_PACK_CMD_IDEL           = 0x00,
	USBLINK_PACK_CMD_UPLOAD_REP     = 0x01,
	USBLINK_PACK_CMD_UPLOAD_IMU_REP = 0x02,
	USBLINK_PACK_CMD_LIFT_CALIBRATE = 0x10,
	USBLINK_PACK_CMD_LIFT_SET_POS   = 0x11,
	USBLINK_PACK_CMD_LIFT_GET_MAX_REP = 0x12,
	USBLINK_PACK_CMD_SERVO_SET_POS_1 = 0x20,
	USBLINK_PACK_CMD_SERVO_SET_POS_2 = 0x21,
	USBLINK_PACK_CMD_SERVO_SET_POS_3 = 0x22,
};

#define USBUL_RX_BUFFER_SIZE 64

/**
 * @brief Initialise the uplink (subscribes to status messages, starts TX timer).
 * @return 0 on success.
 */
int usb_uplink_init(void);

/**
 * @brief Pack and transmit a status frame immediately.
 * Called by the periodic timer / work item.
 */
void usbul_send_status(void);

/**
 * @brief Pack and transmit an IMU frame immediately.
 */
void usbul_send_imu(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_UPLINK_H */
