/*
 * Motion-based theft detection state machine (Section 3.3.3). Consumes
 * imu_hal.h, produces a security state.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This module's detection-tier output is `imu_state` (enum IMU_STATE), a
 * cross-task global defined in common/globals.h/.c alongside security_state,
 * battery_state, etc. */

/* FreeRTOS task entry point. Intended to run at priority 7 (highest) on
 * core 1, per Table 4. */
void imu_detection_task(void *arg);

#ifdef __cplusplus
}
#endif
