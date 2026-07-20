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
 * g_imu_state is this module's own detection-tier output -- a plain
 * `volatile` global rather than a mutex-protected one, since it has exactly
 * one writer (imu_detection_task) and single-word reads/writes are atomic
 * on the ESP32. It's distinct from the system-wide `security_state`
 * (enum SECURITY_STATE, in common/include/globals.h, owned by the Security
 * Core task) that imu_detection_task reads to decide whether to sample at
 * all -- reconciling the two is Security Core's job, not implemented yet.
 */
extern volatile security_state_t g_imu_state;

/* FreeRTOS task entry point. Intended to run at priority 7 (highest) on
 * core 1, per Table 4. */
void imu_detection_task(void *arg);

#ifdef __cplusplus
}
#endif
