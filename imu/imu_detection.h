/*
 * Motion-based theft detection state machine (Section 3.3.3). Consumes
 * imu_hal.h (raw samples) via imu_ring_buffer.h (windowed motion trigger),
 * imu_tilt_filter.h (fused tilt angle), and imu_walk_detector.h (rhythmic
 * gait / walk-away, F5), and produces a security state.
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
