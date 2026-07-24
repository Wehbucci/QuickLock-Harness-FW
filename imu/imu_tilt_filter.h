/*
 * imu_tilt_filter.h — complementary filter tracking the gravity direction
 * in the body frame, and the angle-from-reference (tilt) derived from it.
 *
 * This is the tilt sub-problem of a Mahony AHRS, which is the right size
 * here: with no magnetometer, yaw is unobservable anyway, so a full
 * quaternion filter (Madgwick/EKF) would add cost without adding
 * observability. (Known, accepted blind spot: rotation ABOUT the gravity
 * axis -- a flat yaw twist -- never moves the estimate off the reference,
 * so it cannot be detected here; imu_detection.c's accel trigger window
 * still sees the handling that accompanies it.)
 *
 * Per sample: propagate the estimate with the gyro (dg/dt = g x omega in
 * the body frame -- exact, and immune to the linear acceleration that
 * corrupts the accelerometer while the unit is being carried), then, only
 * when ||a|| is close to 1 g (quasi-static, so the accelerometer is
 * measuring gravity and nothing else), blend it toward the measured
 * direction. The blend both fixes integration error and continuously
 * cancels gyro bias, so cheap/clone MPU-6050s with poor bias behave;
 * conversely a wrong gyro *scale* on a clone die only distorts the angle
 * during motion and is repaired at the next still moment.
 *
 * Stack-agnostic: only depends on imu_hal.h's imu_data_t, no FreeRTOS/BLE.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "imu_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;
} imu_vec3_t;

typedef struct {
    imu_vec3_t g_est;          /* current gravity-direction estimate (unit vector, body frame) */
    imu_vec3_t g_ref;          /* locked reference the tilt angle is measured against */
    bool       g_est_valid;
    bool       g_ref_valid;
    uint32_t   settle_samples; /* consecutive TRUSTED samples since the reference was invalidated */
} imu_tilt_filter_t;

void imu_tilt_filter_init(imu_tilt_filter_t *f);

/* Discards the running estimate entirely -- use when the unit could have
 * moved arbitrarily while not being sampled (e.g. re-arming from DISARMED). */
void imu_tilt_filter_reset_estimate(imu_tilt_filter_t *f);

/* Invalidates just the locked reference, so it re-captures from the
 * current estimate after IMU_TILT_BASELINE_SETTLE_SAMPLES of quasi-static
 * samples (e.g. re-entering ARMED_QUIET). The running estimate itself is
 * left alone. */
void imu_tilt_filter_reset_reference(imu_tilt_filter_t *f);

/* Feed one new sample: propagates the estimate, blends toward the
 * accelerometer when trusted, and locks the reference once settled. Call
 * once per sample while armed (QUIET/TIER2/TIER3). */
void imu_tilt_filter_update(imu_tilt_filter_t *f, const imu_data_t *sample);

/* True once a reference has locked -- imu_tilt_filter_get_tilt_deg() is
 * meaningless before this. */
bool imu_tilt_filter_reference_locked(const imu_tilt_filter_t *f);

/* Angle in degrees between the current estimate and the locked reference.
 * Returns 0 if no reference is locked yet -- check
 * imu_tilt_filter_reference_locked() first if that distinction matters. */
float imu_tilt_filter_get_tilt_deg(const imu_tilt_filter_t *f);

/* True once the running estimate itself is valid (independent of whether a
 * reference has locked) -- gates imu_tilt_filter_get_direction(). */
bool imu_tilt_filter_direction_valid(const imu_tilt_filter_t *f);

/* Current gravity-direction estimate, for a caller that wants to track its
 * own secondary reference (e.g. imu_detection_task's TIER2 "additional
 * tilt since the blind window ended" baseline) using the same estimate
 * this filter is already maintaining. */
imu_vec3_t imu_tilt_filter_get_direction(const imu_tilt_filter_t *f);

/* Angle in degrees between two arbitrary unit directions -- shared by both
 * the armed-reference tilt above and any caller-held secondary baseline. */
float imu_tilt_angle_deg(imu_vec3_t a, imu_vec3_t b);

#ifdef __cplusplus
}
#endif
