/*
 * MPU-6050 HAL: I2C register access + data-ready interrupt, nothing else.
 * Detection logic lives in imu_detection.c, not here.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float temp_c;
} imu_data_t;

/*
 * Brings up the I2C bus, configures the MPU-6050 (+/-4g accel, +/-500 dps
 * gyro, 44 Hz DLPF, 100 Hz sample rate per T1) and arms the data-ready
 * interrupt on IMU_HAL_INT_GPIO.
 */
esp_err_t imu_hal_init(void);

/*
 * Blocks until the data-ready ISR signals a new sample, or the timeout
 * elapses. Returns true if a sample is ready to read.
 */
bool imu_hal_wait_for_data_ready(TickType_t timeout_ticks);

/* Burst-reads accel + gyro + temp and converts to physical units. */
esp_err_t imu_hal_read(imu_data_t *out);

/*
 * Liveness + readiness check for the pre-arm health check (arm_test() in
 * security_core_task.c): re-reads WHO_AM_I over I2C to confirm the MPU-6050
 * is still responding, then re-applies the wake/configure register sequence
 * so a chip that lost power since imu_hal_init() (unplugged, not just an I2C
 * hiccup) comes back sampling instead of silently asleep. Returns false only
 * on an I2C transaction failure -- an unexpected WHO_AM_I value is logged
 * but not treated as failure, same as imu_hal_init(), since some GY-521
 * clones report a non-datasheet value.
 */
bool imu_hal_self_test(void);

#ifdef __cplusplus
}
#endif
