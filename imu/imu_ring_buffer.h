/*
 * imu_ring_buffer.h — fixed-size float history + windowed threshold count.
 *
 * Generic: no IMU/security_state knowledge. Used by imu_detection.c to ask
 * "how many of the last N samples exceeded X", which is the shape of both
 * the F3 translational-motion trigger and (previously) the tilt-rate
 * trigger.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 256 samples = 2.56 s at the 100 Hz design point (T1). Sized for the full
 * algorithm in Section 3.3.3, even though only the F3/F4 trigger window is
 * implemented so far -- F5 (carry-away / walking_for) and F6 (pickup-vs-bump)
 * are not yet implemented. */
#define IMU_RING_BUFFER_SIZE 256

typedef struct {
    float samples[IMU_RING_BUFFER_SIZE];
    size_t head;   /* index the next sample will be written to */
    size_t count;  /* valid samples so far, caps at IMU_RING_BUFFER_SIZE */
} imu_ring_buffer_t;

void imu_ring_buffer_init(imu_ring_buffer_t *rb);
void imu_ring_buffer_push(imu_ring_buffer_t *rb, float value);

/* Counts samples exceeding threshold within the most recent `window`
 * samples (or fewer, if the buffer isn't full yet). */
uint32_t imu_ring_buffer_count_over(const imu_ring_buffer_t *rb, size_t window, float threshold);

#ifdef __cplusplus
}
#endif
