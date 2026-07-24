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
 * algorithm in Section 3.3.3, even though only the F3/F4 trigger window is
 * implemented so far -- F5 (carry-away / walking_for) and F6 (pickup-vs-bump)
 * are not yet implemented. Grace-period and auto-silence timing now live in
 * security_core_task (xGracePeriodTimer / xTier3Timer); this task only
 * reports edges via imu_command. */
#define RING_BUFFER_SIZE        256
#define TRIGGER_WINDOW_SAMPLES  100    /* 1 s window for F3 */
#define TRIGGER_THRESHOLD_G     0.50f  /* F3 trigger level */
#define TRIGGER_SAMPLE_COUNT    7      /* >=7/100 samples over threshold (was 90; too strict for shaking) */
#define STILL_THRESHOLD_G       0.10f  /* F12 elevated-sensitivity level, used only in TIER3 */

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

/* Tier2 requires motion_trig to hold for a continuous 2 s before it's
 * reported as sustained, not just re-observed once. A thief who bumps the
 * unit and then lets go breaks this run before it completes, so the grace
 * timer lapses back to ARMED_QUIET instead of escalating to tier3. */
#define TIER2_SUSTAIN_CONFIRM_SAMPLES  (2 * 100)  /* 2 s of unbroken motion_trig */

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

static void send_imu_command(enum IMU_COMMANDS command)
{
    imu_command = command;
    imu_wake_up_security_task();
}

void imu_detection_task(void *arg)
{
    (void)arg;

    mag_ring_buffer_t buf;
    ring_buffer_init(&buf);

    mag_ring_buffer_t gyro_buf; /* per-sample rotation (deg) for the tilt check */
    ring_buffer_init(&gyro_buf);

    enum SECURITY_STATE prev_security_state = SECURITY_DISARMED;
    /* F13-ish debounce: IMU_TIER2_MOVEMENT_SUSTAINED is a one-shot flag for
     * Security Core (tier2_movement_sustained), not a repeated event, so it
     * must fire at most once per grace window -- otherwise every trigger
     * sample during the 5 s grace period would re-notify Security Core. */
    bool tier2_sustained_reported = false;
    uint32_t tier2_motion_run_samples = 0;
    uint32_t sample_num = 0;

    for (;;) {
        enum SECURITY_STATE state = security_state;
        bool entering_tier2 = (state == SECURITY_ARMED_TIER2 && prev_security_state != SECURITY_ARMED_TIER2);
        bool entering_disarmed = (state == SECURITY_DISARMED && prev_security_state != SECURITY_DISARMED);
        bool leaving_disarmed = (state != SECURITY_DISARMED && prev_security_state == SECURITY_DISARMED);
        prev_security_state = state;

        if (entering_tier2) {
            tier2_sustained_reported = false;
            tier2_motion_run_samples = 0;
        }

        if (state == SECURITY_DISARMED) {
            /* Disarmed: idle instead of sampling. Blocks here until
             * something calls xTaskNotifyGive() on this task. */
            if (entering_disarmed) {
                ESP_LOGI(TAG, "disarmed, idling");
            }
            ring_buffer_init(&buf);
            ring_buffer_init(&gyro_buf);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (leaving_disarmed) {
            ESP_LOGI(TAG, "armed, resuming sampling");
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
        bool motion_trig = (over_trig >= TRIGGER_SAMPLE_COUNT) || tilt_trig;

        if (++sample_num >= PRINT_EVERY_N_SAMPLES) {
            sample_num = 0;
            printf("accel [g]: x=%.2f y=%.2f z=%.2f | mag=%.2f | over_trig=%2" PRIu32 "/100 "
                   "| gyro [dps]: x=%.1f y=%.1f z=%.1f | rate=%.1f | over_tilt=%2" PRIu32 "/100 "
                   "| security_state=%d\n",
                   sample.accel_x_g, sample.accel_y_g, sample.accel_z_g, mag, over_trig,
                   sample.gyro_x_dps, sample.gyro_y_dps, sample.gyro_z_dps, gyro_rate_dps, over_tilt,
                   (int)state);
        }

        switch (state) {
        case SECURITY_ARMED_QUIET:
            if (motion_trig) {
                if (tilt_trig) {
                    ESP_LOGW(TAG, "tilt trigger: %" PRIu32 "/100 samples over %.0f dps", over_tilt, TILT_RATE_THRESHOLD_DPS);
                }
                send_imu_command(IMU_QUIET_TO_TIER2); /* F3/F8 met -> F10 chirp */
            }
            /* F5 carry-away (walking_for(5s)) and the walk-away fast paths
             * (IMU_QUIET_TO_TIER3 / IMU_TIER2_TO_TIER3) intentionally not
             * implemented yet. */
            break;

        case SECURITY_ARMED_TIER2:
            /* Same detection/thresholds as QUIET, but motion must hold for
             * TIER2_SUSTAIN_CONFIRM_SAMPLES straight before it's reported --
             * letting go at any point resets the run, so a single bump
             * followed by stillness rides out the grace timer instead of
             * escalating. Only the first confirmed run matters to Security
             * Core (tier2_movement_sustained is a one-shot flag). */
            if (!tier2_sustained_reported) {
                if (motion_trig) {
                    if (++tier2_motion_run_samples >= TIER2_SUSTAIN_CONFIRM_SAMPLES) {
                        send_imu_command(IMU_TIER2_MOVEMENT_SUSTAINED);
                        tier2_sustained_reported = true;
                    }
                } else {
                    tier2_motion_run_samples = 0;
                }
            }
            break;

        case SECURITY_ARMED_TIER3:
            /* More alert: the lower STILL_THRESHOLD_G bar, not the trigger
             * window. Reported on every over-threshold sample so Security
             * Core's tier3 auto-silence timer keeps resetting while motion
             * continues. */
            if (over_still) {
                send_imu_command(IMU_TIER3_MOVEMENT_DETECTED);
            }
            break;

        default:
            break;
        }
    }
}
