/*
 * common_config.h — project-wide tuning/mode constants shared across
 * subsystems.
 *
 * Lives in common/ (not a per-subsystem config.h, and not named config.h
 * itself -- ble/config/include/config.h already owns that name and some
 * components REQUIRES both) so there's one place for constants meant to be
 * checked from more than one subsystem, instead of each subsystem growing
 * its own similarly-named flag.
 */

#pragma once

/* -------------------------------------------------------------------------- */
/* Debug / production mode                                                    */
/* -------------------------------------------------------------------------- */

/*
 * 1 (debug): failed pre-condition/health checks are logged only and the
 *   system proceeds anyway. First consumer: arm_test() in
 *   security_core_task.c -- a failed subsystem check (currently the IMU
 *   self-test) is logged but does not block arming. Useful on the bench with
 *   a known-flaky/disconnected sensor.
 * 0 (production): the same checks are enforced -- e.g. arm_test() blocks the
 *   arm request on a failed check instead of arming anyway. This is the
 *   fail-secure behavior the harness should ship with.
 */
#define DEBUG_MODE 0

/* -------------------------------------------------------------------------- */
/* IMU init-failure behavior                                                  */
/* -------------------------------------------------------------------------- */

/*
 * Controls what app_main does if imu_hal_init() fails at boot (e.g. the
 * MPU6050 isn't wired up yet).
 *
 * 1: force a reset -- ESP_ERROR_CHECK(imu_hal_init()) aborts the boot, same
 *    as every other ESP_ERROR_CHECK'd bring-up step. This is what the
 *    harness should ship with: a missing safety-critical sensor should not
 *    silently boot into a degraded state.
 * 0: log the failure and skip creating imu_detection_task so the rest of the
 *    harness (BLE, Security Core, etc.) still comes up. For bring-up work on
 *    another subsystem when the IMU isn't physically on the bus yet.
 */
#define IMU_RESET_ON_INIT_FAILURE 1

/* -------------------------------------------------------------------------- */
/* Add other cross-subsystem constants here as they come up.                  */
/* -------------------------------------------------------------------------- */
