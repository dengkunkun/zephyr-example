/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_msg.h
 * @brief Application-wide inter-module message bus.
 *
 * Replaces the bare-metal msg.c circular queue.  Each module publishes typed
 * messages; subscribers register a k_msgq and receive copies via
 * app_msg_subscribe() / app_msg_publish().
 *
 * Usage:
 *   // Publisher (e.g. wbus.c):
 *   struct app_msg m = { .type = MSG_RC_FRAME, .rc = frame };
 *   app_msg_publish(&m);
 *
 *   // Subscriber (e.g. manual_ctrl.c):
 *   K_MSGQ_DEFINE(my_q, sizeof(struct app_msg), 8, 4);
 *   app_msg_subscribe(&my_q, BIT(MSG_RC_FRAME));
 *   struct app_msg m;
 *   k_msgq_get(&my_q, &m, K_FOREVER);
 */

#ifndef APP_MSG_H
#define APP_MSG_H

#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Message type identifiers */
enum app_msg_type {
	MSG_RC_FRAME = 0,   /**< WBUS/SBUS decoded channel data */
	MSG_IMU_FRAME,      /**< IMU5115 decoded frame */
	MSG_MOTO_STATUS,    /**< Motor speed feedback */
	MSG_SERVO_STATUS,   /**< Servo position feedback */
	MSG_LIFT_STATUS,    /**< Lift position feedback */
	MSG_BAT_VOLT,       /**< Battery voltage (mV) */
	MSG_MOTO_CMD,       /**< Motor speed command (from manual_ctrl) */
	MSG_SERVO_CMD,      /**< Servo position command */
	MSG_LIFT_CMD,       /**< Lift position command */
	MSG_TYPE_MAX
};

/** RC frame payload – decoded SBUS/WBUS channels */
struct app_msg_rc {
	int16_t ch[10];   /**< Channels 1-10, normalised ±100 */
	uint8_t  status;  /**< 0=normal, 1=no-signal, 2=no-device */
};

/** IMU frame payload */
struct app_msg_imu {
	uint32_t gx, gy, gz;
	uint32_t ax, ay, az;
	uint16_t frame_seq;
};

/** Motor status / command payload */
struct app_msg_moto {
	int16_t l_speed; /**< Left wheel speed (RPM*factor) */
	int16_t r_speed; /**< Right wheel speed */
};

/** Servo command/status payload */
struct app_msg_servo {
	uint8_t  id;       /**< Servo ID 1-3 */
	int16_t  pos;      /**< Target / current position */
	int16_t  speed;    /**< Speed limit (0=max) */
	uint16_t time_ms;  /**< Move time (ms) */
};

/** Lift command/status payload */
struct app_msg_lift {
	int32_t  pos;      /**< Target / current position (encoder counts) */
	uint8_t  cmd;      /**< 0=set_pos, 1=calibrate, 2=go_zero */
};

/** Battery voltage payload */
struct app_msg_bat {
	uint16_t voltage_mv; /**< Battery voltage in millivolts */
};

/** Application message container */
struct app_msg {
	enum app_msg_type type;
	union {
		struct app_msg_rc    rc;
		struct app_msg_imu   imu;
		struct app_msg_moto  moto;
		struct app_msg_servo servo;
		struct app_msg_lift  lift;
		struct app_msg_bat   bat;
	};
};

/** Maximum number of simultaneous subscribers */
#define APP_MSG_MAX_SUBSCRIBERS 8

/**
 * @brief Initialise the message bus.
 * @return 0 on success.
 */
int app_msg_init(void);

/**
 * @brief Subscribe a message queue to receive messages of the given types.
 *
 * @param q       Caller-owned k_msgq (must hold sizeof(struct app_msg) items).
 * @param type_mask Bitmask of (1 << enum app_msg_type) values to receive.
 * @return 0 on success, -ENOMEM if subscriber table is full.
 */
int app_msg_subscribe(struct k_msgq *q, uint32_t type_mask);

/**
 * @brief Publish a message to all subscribers interested in its type.
 *
 * May be called from any context (including ISR) — uses k_msgq_put with
 * K_NO_WAIT so it never blocks.
 *
 * @param msg Message to broadcast.
 */
void app_msg_publish(const struct app_msg *msg);

#ifdef __cplusplus
}
#endif

#endif /* APP_MSG_H */
