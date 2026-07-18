/*
 * Motion-based theft detection state machine (Section 3.3.3). Consumes
 * imu_hal.h, produces a security state.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SECURITY_STATE_QUIET = 0,
    SECURITY_STATE_TIER2_GRACE,
    SECURITY_STATE_TIER3_ALARM,
} security_state_t;

/* FreeRTOS task entry point. Intended to run at priority 7 (highest) on
 * core 1, per Table 4. */
void imu_detection_task(void *arg);

security_state_t imu_detection_get_state(void);

/*
 * Placeholder for the future Security Core / BLE task (F13): call this to
 * acknowledge a disarm command while SUSPICIOUS (TIER2_GRACE).
 */
void imu_detection_disarm(void);

#ifdef __cplusplus
}
#endif
