/*
 * imu_walk_detector.h — rhythmic-gait ("walk-away") detector (F5).
 *
 * Detects that the hub is being carried at a walking cadence, the signature
 * that separates a theft-in-progress from incidental handling. Walking is
 * PERIODIC: each footstep is an impact spike in the accelerometer magnitude
 * at a regular ~1.5-2.5 Hz cadence. Random shaking or a single bump does not
 * produce several consecutive impacts whose spacing all falls in the
 * walking-cadence band; sustained walking does. That "N consecutive
 * in-cadence steps" test is the whole idea -- the classic pedometer
 * approach, cheap enough to run per-sample on the ESP32.
 *
 * Distinct from imu_detection.c's F3 motion trigger on purpose: that fires
 * at a hard 0.5 g shake, but a hub gently lifted and carried off produces
 * much softer (~0.1-0.2 g) accelerations that stay UNDER it -- so a careful
 * walk-away would otherwise ride out the whole grace period undetected.
 *
 * Stack-agnostic: no FreeRTOS/BLE/IDF, only stdint/stdbool. Fed the accel
 * magnitude in g; the caller owns thresholds' meaning by state.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     above;              /* hysteresis: currently above the peak threshold */
    bool     primed;             /* last_step_sample holds a real step yet */
    uint32_t sample_index;       /* monotonic per-update counter (step-interval clock) */
    uint32_t last_step_sample;   /* sample_index of the last counted step */
    uint32_t consecutive_steps;  /* run of in-cadence steps; the walk-confidence measure */
} imu_walk_detector_t;

void imu_walk_detector_init(imu_walk_detector_t *w);

/* Clears the step run and interval clock. Call when starting a fresh
 * detection episode (e.g. entering TIER2) so steps from a prior episode
 * don't carry over. */
void imu_walk_detector_reset(imu_walk_detector_t *w);

/* Feed one sample's accelerometer magnitude ||a|| in g (NOT the |a|-1g
 * deviation). Returns true once a full run of in-cadence steps has been
 * confirmed -- i.e. walking is happening now. Must be called on contiguous
 * samples for the interval clock to be meaningful. */
bool imu_walk_detector_update(imu_walk_detector_t *w, float accel_norm_g);

/* Current confirmed-step run length, for logging. */
uint32_t imu_walk_detector_step_count(const imu_walk_detector_t *w);

#ifdef __cplusplus
}
#endif
