/*
 * Motion-based theft detection state machine (Section 3.3.3). Consumes
 * imu_hal.h, produces a security state.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SECURITY_STATE_QUIET = 0,
    SECURITY_STATE_TIER2_GRACE,
    SECURITY_STATE_TIER3_ALARM,
} security_state_t;

/*
 * Cross-task shared state. Each variable has exactly one writer, so plain
 * `volatile` globals are used instead of a mutex -- single-word reads/writes
 * are atomic on the ESP32, and there's no multi-field consistency to
 * protect since each one stands alone.
 *
 * g_imu_state: written only by imu_detection_task, read by anyone.
 *
 * g_armed / g_buckle_open: read by imu_detection_task, but NOT written by
 * it. These are dummy placeholders defined in main for now -- g_armed will
 * eventually be written by the Security Core / BLE task, and g_buckle_open
 * by the Belt Detection task, neither of which exist yet.
 */
extern volatile security_state_t g_imu_state;
extern volatile bool g_armed;
extern volatile bool g_buckle_open;

/* FreeRTOS task entry point. Intended to run at priority 7 (highest) on
 * core 1, per Table 4. */
void imu_detection_task(void *arg);

#ifdef __cplusplus
}
#endif
