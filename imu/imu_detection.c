#include "imu_detection.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "imu_hal.h"
#include "globals.h"

static const char *TAG = "imu_detection";

/* 256 samples = 2.56 s at the 100 Hz design point (T1). Sized for the full
 * algorithm in Section 3.3.3, even though only the F3/F4 trigger window and
 * the F12 auto-silence timer are implemented so far -- F5 (carry-away /
 * walking_for) and F6 (pickup-vs-bump) are not yet implemented. */
#define RING_BUFFER_SIZE        256
#define TRIGGER_WINDOW_SAMPLES  100    /* 1 s window for F3 */
#define TRIGGER_THRESHOLD_G     0.50f  /* F3 trigger level */
#define TRIGGER_SAMPLE_COUNT    7      /* >=7/100 samples over threshold (was 90; too strict for shaking) */
#define STILL_THRESHOLD_G       0.10f  /* F12 elevated-sensitivity level */
#define GRACE_PERIOD_SAMPLES    (5 * 100)   /* 5 s grace period, F10 */
#define AUTO_SILENCE_SAMPLES    (20 * 100)  /* 20 s continuous stillness, F12 */

/* Tilt detection (F8): catches slowly tilting the hub to loosen the strap,
 * which a translational-motion trigger (over_trig above) can miss since a
 * slow rotation doesn't necessarily move |a| far from 1g. Tracked via the
 * gyroscope, not the accelerometer, because angular rate stays valid even
 * while the unit is also being accelerated/carried -- an accelerometer-only
 * tilt angle would be corrupted by that. Same duration-gated shape as the
 * motion trigger above (N of the last 100 samples over a rate threshold),
 * but unlike TRIGGER_SAMPLE_COUNT this stays strict/high: a deliberate tilt
 * holds an elevated rate for nearly the whole 1 s window, while a knock or
 * bump only makes the sensor rock briefly (a handful of samples), so a low
 * count (as used for motion, to tolerate shaking's oscillation) would let
 * bumps through here. Thresholds are starting assumptions, to be tuned at
 * the bench like F6/F14. */
#define TILT_WINDOW_SAMPLES      100    /* 1 s window */
#define TILT_RATE_THRESHOLD_DPS  20.0f  /* instantaneous angular rate trigger level */
#define TILT_SAMPLE_COUNT        70     /* >=70/100 samples over threshold */

#define PRINT_EVERY_N_SAMPLES   10     /* 100 Hz / 10 = 10 Hz readout */

typedef struct {
    float samples[RING_BUFFER_SIZE];
    size_t head;   /* index the next sample will be written to */
    size_t count;  /* valid samples so far, caps at RING_BUFFER_SIZE */
} mag_ring_buffer_t;

static void ring_buffer_init(mag_ring_buffer_t *rb)
{
    memset(rb, 0, sizeof(*rb));
}

static void ring_buffer_push(mag_ring_buffer_t *rb, float value)
{
    rb->samples[rb->head] = value;
    rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
    if (rb->count < RING_BUFFER_SIZE) {
        rb->count++;
    }
}

/* Counts samples exceeding threshold within the most recent `window`
 * samples (or fewer, if the buffer isn't full yet). */
static uint32_t ring_buffer_count_over(const mag_ring_buffer_t *rb, size_t window, float threshold)
{
    size_t n = (rb->count < window) ? rb->count : window;
    size_t idx = rb->head;
    uint32_t over = 0;

    for (size_t i = 0; i < n; i++) {
        idx = (idx == 0) ? RING_BUFFER_SIZE - 1 : idx - 1;
        if (rb->samples[idx] > threshold) {
            over++;
        }
    }
    return over;
}

static const char *state_name(enum IMU_STATE state)
{
    switch (state) {
    case IMU_QUIET:       return "QUIET";
    case IMU_TIER2_GRACE:  return "TIER2_GRACE";
    case IMU_TIER3_ALARM:  return "TIER3_ALARM";
    default:               return "?";
    }
}

static void set_state(enum IMU_STATE new_state)
{
    if (new_state == imu_state) {
        return;
    }
    ESP_LOGW(TAG, "state change: %s -> %s", state_name(imu_state), state_name(new_state));
    imu_state = new_state;
    /* TODO: imu_wake_up_security_task() once security_core_task_handle is
     * actually assigned (T4: <=100ms budget) -- calling it now would notify
     * a NULL handle, since Security Core doesn't exist/run yet. */
}

void imu_detection_task(void *arg)
{
    (void)arg;

    mag_ring_buffer_t buf;
    ring_buffer_init(&buf);

    mag_ring_buffer_t gyro_buf; /* per-sample rotation (deg) for the tilt check */
    ring_buffer_init(&gyro_buf);

    uint32_t grace_samples = 0;
    uint32_t quiet_samples = 0;
    uint32_t sample_num = 0;
    bool was_armed = false;

    for (;;) {
        if (security_state == SECURITY_DISARMED) {
            /* Disarmed: idle instead of sampling. Blocks here until
             * something calls xTaskNotifyGive() on this task -- there's no
             * such call anywhere yet (arming isn't wired up), so this will
             * simply block indefinitely until that exists. */
            if (was_armed) {
                ESP_LOGI(TAG, "disarmed, idling");
                was_armed = false;
            }
            imu_state = IMU_QUIET;
            grace_samples = 0;
            quiet_samples = 0;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (!was_armed) {
            ESP_LOGI(TAG, "armed, resuming sampling");
            was_armed = true;
        }

        if (!imu_hal_wait_for_data_ready(pdMS_TO_TICKS(50))) {
            continue; /* no interrupt within timeout; keep waiting */
        }

        imu_data_t sample;
        if (imu_hal_read(&sample) != ESP_OK) {
            continue;
        }

        /* Gravity-compensated acceleration magnitude, |a| - 1g (Section 3.3.3). */
        float accel_norm = sqrtf(sample.accel_x_g * sample.accel_x_g +
                                  sample.accel_y_g * sample.accel_y_g +
                                  sample.accel_z_g * sample.accel_z_g);
        float mag = fabsf(accel_norm - 1.0f);

        ring_buffer_push(&buf, mag);

        uint32_t over_trig = ring_buffer_count_over(&buf, TRIGGER_WINDOW_SAMPLES, TRIGGER_THRESHOLD_G);
        bool over_still = (mag > STILL_THRESHOLD_G);

        /* Tilt (F8): total angular rate magnitude, ignoring axis/direction --
         * any sustained rotation counts, which keeps this simple and avoids
         * needing full orientation tracking. */
        float gyro_rate_dps = sqrtf(sample.gyro_x_dps * sample.gyro_x_dps +
                                     sample.gyro_y_dps * sample.gyro_y_dps +
                                     sample.gyro_z_dps * sample.gyro_z_dps);
        ring_buffer_push(&gyro_buf, gyro_rate_dps);

        uint32_t over_tilt = ring_buffer_count_over(&gyro_buf, TILT_WINDOW_SAMPLES, TILT_RATE_THRESHOLD_DPS);
        bool tilt_trig = (over_tilt >= TILT_SAMPLE_COUNT);

        if (++sample_num >= PRINT_EVERY_N_SAMPLES) {
            sample_num = 0;
            printf("accel [g]: x=%.2f y=%.2f z=%.2f | mag=%.2f | over_trig=%2" PRIu32 "/100 "
                   "| gyro [dps]: x=%.1f y=%.1f z=%.1f | rate=%.1f | over_tilt=%2" PRIu32 "/100 "
                   "| belt_state=%d | state=%s\n",
                   sample.accel_x_g, sample.accel_y_g, sample.accel_z_g, mag, over_trig,
                   sample.gyro_x_dps, sample.gyro_y_dps, sample.gyro_z_dps, gyro_rate_dps, over_tilt,
                   belt_state, state_name(imu_state));
        }

        if (belt_state == BELT_OPEN) {
            /* Strap cut/unplugged (F1, F2): unmistakable, bypasses the grace
             * period and holds TIER3_ALARM for as long as the loop is open,
             * regardless of the motion/tilt state machine below. */
            quiet_samples = 0;
            set_state(IMU_TIER3_ALARM);
            continue;
        }

        switch (imu_state) {
        case IMU_QUIET:
            if (over_trig >= TRIGGER_SAMPLE_COUNT) {
                grace_samples = 0;
                set_state(IMU_TIER2_GRACE); /* F3 met -> F10 chirp */
            } else if (tilt_trig) {
                grace_samples = 0;
                ESP_LOGW(TAG, "tilt trigger: %" PRIu32 "/100 samples over %.0f dps", over_tilt, TILT_RATE_THRESHOLD_DPS);
                set_state(IMU_TIER2_GRACE); /* F8 -> F10 chirp */
            }
            /* F5 carry-away (walking_for(5s)) intentionally not implemented yet. */
            break;

        case IMU_TIER2_GRACE:
            if (++grace_samples >= GRACE_PERIOD_SAMPLES) {
                set_state(IMU_TIER3_ALARM);
            }
            /* Disarm cancelling this grace period happens via the
             * security_state gate above, not a separate check here. */
            break;

        case IMU_TIER3_ALARM:
            if (over_still) {
                quiet_samples = 0;
            } else if (++quiet_samples >= AUTO_SILENCE_SAMPLES) {
                set_state(IMU_QUIET);
            }
            break;
        }
    }
}
