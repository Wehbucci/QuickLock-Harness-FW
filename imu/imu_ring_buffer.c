#include "imu_ring_buffer.h"

#include <string.h>

void imu_ring_buffer_init(imu_ring_buffer_t *rb)
{
    memset(rb, 0, sizeof(*rb));
}

void imu_ring_buffer_push(imu_ring_buffer_t *rb, float value)
{
    rb->samples[rb->head] = value;
    rb->head = (rb->head + 1) % IMU_RING_BUFFER_SIZE;
    if (rb->count < IMU_RING_BUFFER_SIZE) {
        rb->count++;
    }
}

uint32_t imu_ring_buffer_count_over(const imu_ring_buffer_t *rb, size_t window, float threshold)
{
    size_t n = (rb->count < window) ? rb->count : window;
    size_t idx = rb->head;
    uint32_t over = 0;

    for (size_t i = 0; i < n; i++) {
        idx = (idx == 0) ? IMU_RING_BUFFER_SIZE - 1 : idx - 1;
        if (rb->samples[idx] > threshold) {
            over++;
        }
    }
    return over;
}
