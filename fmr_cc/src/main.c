/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file main.c
 * @brief FMR_CC main entry point — Zephyr RTOS port.
 *
 * Initialises all modules then returns (Zephyr keeps threads running).
 * Replaces the original bare-metal superloop in main.c.
 *
 * Periodic tasks previously handled by soft_timer.c are now implemented
 * with k_work_delayable inside each module.  The timers started here
 * match the original schedule:
 *   - Motor speed poll  : every 200 ms
 *   - Servo/lift poll   : every 1000 ms
 *   - Status upload     : every 200 ms (in usb_uplink.c)
 *   - IMU upload        : every 50 ms  (in usb_uplink.c)
 *   - Battery ADC       : every 10 s   (battery.c TODO)
 */

#include "app_msg.h"
#include "led_statu.h"
#include "wbus.h"
#include "imu5115.h"
#include "moto.h"
#include "servo.h"
#include "comm_485_lift.h"
#include "manual_ctrl.h"
#include "battery.h"
#include "usb_uplink.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Periodic work items for timed polling commands */
static struct k_work_delayable read_speed_work;
static struct k_work_delayable read_servo_lift_work;

static void read_speed_work_fn(struct k_work *w)
{
	moto_cmd_get_all_speed();
	k_work_reschedule((struct k_work_delayable *)w, K_MSEC(200));
}

static void read_servo_lift_work_fn(struct k_work *w)
{
	servo_cmd_get_all_pos();
	comm_485_1_cmd_get_lift_pos();
	k_work_reschedule((struct k_work_delayable *)w, K_MSEC(1000));
}

int main(void)
{
	printk("\n");
	printk("========================================\n");
	printk("  FMR_CC Zephyr Port — starting up      \n");
	printk("  Build: %s %s                           \n", __DATE__, __TIME__);
	printk("========================================\n");

	LOG_INF("Initialising modules...");

	/* Message bus first — everything else uses it */
	app_msg_init();

	/* LED status (optional) */
	if (led_statu_init() != 0) {
		LOG_WRN("led_statu init failed (non-fatal)");
	}

	/* RC receiver */
	if (wbus_init() != 0) {
		LOG_WRN("wbus init failed");
	}

	/* IMU */
	if (imu_init() != 0) {
		LOG_WRN("imu init failed");
	}

	/* Motor controller */
	if (moto_init() != 0) {
		LOG_WRN("moto init failed");
	}

	/* Servo controller */
	if (servo_init() != 0) {
		LOG_WRN("servo init failed");
	}

	/* Lift controller */
	if (comm_485_lift_init() != 0) {
		LOG_WRN("comm_485_lift init failed");
	}

	/* Manual control (RC → motor) */
	if (manual_ctrl_init() != 0) {
		LOG_WRN("manual_ctrl init failed");
	}

	/* Battery ADC (stub) */
	if (battery_init() != 0) {
		LOG_WRN("battery init failed");
	}

	/* USB / console uplink */
	if (usb_uplink_init() != 0) {
		LOG_WRN("usb_uplink init failed");
	}

	/* Start periodic polling work items */
	k_work_init_delayable(&read_speed_work, read_speed_work_fn);
	k_work_reschedule(&read_speed_work, K_MSEC(500)); /* initial delay */

	k_work_init_delayable(&read_servo_lift_work, read_servo_lift_work_fn);
	k_work_reschedule(&read_servo_lift_work, K_MSEC(1000));

	LOG_INF("All modules initialised — system running");

	/*
	 * Returning from main() is valid in Zephyr; the kernel continues to
	 * schedule threads.  No superloop needed.
	 */
	return 0;
}
