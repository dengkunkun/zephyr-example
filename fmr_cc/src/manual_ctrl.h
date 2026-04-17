/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file manual_ctrl.h
 * @brief RC → motor/servo/lift command translator header.
 *
 * Pure logic module — subscribes to MSG_RC_FRAME and publishes
 * MSG_MOTO_CMD, MSG_SERVO_CMD, MSG_LIFT_CMD.  No hardware access.
 * Guarded by CONFIG_APP_FMR_MANUAL_CTRL.
 *
 * Robot parameters (from original robot_parament.h):
 *   Axle length : 0.52 m
 *   Wheel radius: 0.20 m
 *   Max wheel speed: 200 (RPM * 9.549724 = RPM converted to rad/s*R)
 */

#ifndef MANUAL_CTRL_H
#define MANUAL_CTRL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the manual control module.
 *
 * Starts a thread that reads from the RC msgq and translates to commands.
 * @return 0 on success.
 */
int manual_ctrl_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MANUAL_CTRL_H */
