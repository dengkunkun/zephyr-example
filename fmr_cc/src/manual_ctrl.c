/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file manual_ctrl.c
 * @brief RC → motor/servo/lift command translator — Zephyr port of original manual_ctrl.c.
 *
 * Subscribes to MSG_RC_FRAME via app_msg.  When a frame arrives, converts
 * channel data to differential drive wheel speeds using the same unicycle
 * kinematic model as the original.
 *
 * Channel mapping (faithful to original):
 *   Ch1  → angular velocity (yaw)
 *   Ch2  → linear velocity (forward/back)
 *   Ch5  → arm switch (< 0 → disable)
 *   Ch6  → speed level (-50..50 = LOW/MID, >50 = HIGH, <-50 = LOW)
 *
 * Speed level constants match original:
 *   LOW:  Lv=0.5 m/s,  Av=1.0 rad/s
 *   MID:  Lv=1.0 m/s,  Av=2.0 rad/s
 *   HIGH: Lv=2.5 m/s,  Av=3.0 rad/s
 *
 * Only compiled when CONFIG_APP_FMR_MANUAL_CTRL=y.
 */

#include "manual_ctrl.h"

#ifdef CONFIG_APP_FMR_MANUAL_CTRL

#include "app_msg.h"
#include "moto.h"
#include "wbus.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(manual_ctrl, LOG_LEVEL_INF);

/* Robot geometry (from robot_parament.h) */
#define ROBOT_P_AXLE_LEN       0.52f
#define ROBOT_P_WHEEL_R        0.20f
#define ROBOT_P_WHELL_MAX_SPEED 200

/* Speed levels */
#define LV_LOW    0.5f
#define LV_MID    1.0f
#define LV_HIGH   2.5f
#define AV_LOW    1.0f
#define AV_MID    2.0f
#define AV_HIGH   3.0f

/* Task period: 100 ms (original MANUAL_CTRL_PERIOD = 1000 ticks of 100 µs) */
#define CTRL_PERIOD_MS 100

K_MSGQ_DEFINE(rc_msgq, sizeof(struct app_msg), 4, 4);

static int16_t f_2_i16(float v)
{
	if (v > 0.0f) {
		v += 0.5f;
	} else if (v < 0.0f) {
		v -= 0.5f;
	}
	return (int16_t)v;
}

#define CTRL_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(ctrl_stack, CTRL_STACK_SIZE);
static struct k_thread ctrl_thread_data;

static void ctrl_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	float max_lv   = LV_LOW;
	float max_av   = AV_LOW;
	bool  stop_sent = true;

	while (1) {
		struct app_msg msg;
		int ret = k_msgq_get(&rc_msgq, &msg, K_MSEC(CTRL_PERIOD_MS));

		if (ret != 0) {
			/* Timeout — no new RC frame; check device status */
			if (wbus_get_statu() != WBUS_STATU_NORMAL) {
				if (!stop_sent) {
					moto_cmd_set_all_speed(0, 0);
					moto_cmd_clr_err();
					stop_sent = true;
				}
			}
			continue;
		}

		if (msg.rc.status != WBUS_STATU_NORMAL) {
			if (!stop_sent) {
				moto_cmd_set_all_speed(0, 0);
				moto_cmd_clr_err();
				stop_sent = true;
			}
			continue;
		}

		/* Ch5 (arm switch): if < 0 → disabled */
		if (msg.rc.ch[4] < 0) {
			continue;
		}

		/* Ch6 → speed level */
		if (msg.rc.ch[5] > 50) {
			max_lv = LV_HIGH;
			max_av = AV_HIGH;
		} else if (msg.rc.ch[5] < -50) {
			max_lv = LV_LOW;
			max_av = AV_LOW;
		} else {
			max_lv = LV_MID;
			max_av = AV_MID;
		}

		/* Ch2 = linear, Ch1 = angular (normalised ±100 → fraction) */
		float lv = max_lv * (float)msg.rc.ch[1] / 100.0f;
		float av = max_av * (float)msg.rc.ch[0] / 100.0f;

		float l_f = (lv - ROBOT_P_AXLE_LEN / 2.0f * av) / ROBOT_P_WHEEL_R;
		float r_f = (lv + ROBOT_P_AXLE_LEN / 2.0f * av) / ROBOT_P_WHEEL_R;

		int16_t l_out = f_2_i16(l_f * 9.549724f);
		int16_t r_out = -f_2_i16(r_f * 9.549724f);

		if (l_out > ROBOT_P_WHELL_MAX_SPEED) {
			l_out = ROBOT_P_WHELL_MAX_SPEED;
		} else if (l_out < -ROBOT_P_WHELL_MAX_SPEED) {
			l_out = -ROBOT_P_WHELL_MAX_SPEED;
		}
		if (r_out > ROBOT_P_WHELL_MAX_SPEED) {
			r_out = ROBOT_P_WHELL_MAX_SPEED;
		} else if (r_out < -ROBOT_P_WHELL_MAX_SPEED) {
			r_out = -ROBOT_P_WHELL_MAX_SPEED;
		}

		moto_cmd_set_all_speed(r_out, l_out);
		stop_sent = false;
	}
}

int manual_ctrl_init(void)
{
	app_msg_subscribe(&rc_msgq, BIT(MSG_RC_FRAME));

	k_thread_create(&ctrl_thread_data, ctrl_stack, CTRL_STACK_SIZE,
			ctrl_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&ctrl_thread_data, "manual_ctrl");

	LOG_INF("manual_ctrl init OK");
	return 0;
}

#else /* !CONFIG_APP_FMR_MANUAL_CTRL */

int manual_ctrl_init(void) { return 0; }

#endif /* CONFIG_APP_FMR_MANUAL_CTRL */
