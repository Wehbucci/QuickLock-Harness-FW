#include "imu_detection.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "imu_hal.h"
#include "imu_ring_buffer.h"
#include "imu_tilt_filter.h"
#include "imu_walk_detector.h"
#include "globals.h"
#include "common_config.h"

static const char *TAG = "imu_detection";

/* F3 translational-motion trigger: N of the last 100 samples over a |a|-1g
 * threshold. TRIGGER_SAMPLE_COUNT is deliberately low (not "most of the
 * window") to tolerate the oscillation of shaking, where any single sample
 * can dip back under threshold. */
#define TRIGGER_WINDOW_SAMPLES  100    /* 1 s window for F3 */
#define TRIGGER_THRESHOLD_G     0.50f  /* F3 trigger level */
#define TRIGGER_SAMPLE_COUNT    5      /* >=5/100 samples over threshold */
#define STILL_THRESHOLD_G       0.10f  /* F12 elevated-sensitivity level, used only in TIER3 */

/* F8 tilt trigger, built on imu_tilt_filter's angle-from-armed-reference.
 * Crossing this is an immediate, single-sample QUIET -> TIER2 trigger -- no
 * windowing -- because a real tilt of this size cannot be sensor noise: the
 * filter's own time constant (tau = dt/gain = 0.5 s, see imu_tilt_filter.c)
 * already smooths anything a vibration could inject. Starting assumption,
 * to be tuned at the bench like F6/F14. */
#define TILT_TRIGGER_DEG  15.0f

/* Tier2 requires motion_trig to hold for a continuous 2 s before it's
 * reported as sustained, not just re-observed once. A thief who bumps the
 * unit and then lets go breaks this run before it completes, so the grace
 * timer lapses back to ARMED_QUIET instead of escalating to tier3. */
#define TIER2_SUSTAIN_CONFIRM_SAMPLES  (2 * 100)  /* 2 s of unbroken motion_trig */

/* TIER2's second, independent evidence path: ADDITIONAL tilt beyond
 * wherever the unit was at the end of a short blind window. The armed-
 * reference tilt_over can't be used directly here: it would still read
 * true for a unit that was tilted once, set down, and left -- that's a
 * static condition, not evidence of continued handling.
 *
 * The first TIER2_TILT_BLIND_SAMPLES (2 s) after entering TIER2 are
 * ignored outright -- the tilt that just triggered the escalation is, by
 * definition, a change in that instant, so measuring from the armed pose
 * immediately would trivially always pass. Right as that window ends, the
 * current attitude is captured as a fresh baseline (tier2_tilt_baseline
 * below). From then on, same as the QUIET trigger: a single sample more
 * than TILT_TRIGGER_DEG away from THAT baseline fires immediately -- no
 * further windowing or waiting. A unit that settles and is left alone,
 * even at a steep angle, never moves again relative to its
 * post-blind-window baseline, so it correctly never fires this path. */
#define TIER2_TILT_BLIND_SAMPLES  (2 * 100)  /* 2 s, ignored outright */

#define PRINT_EVERY_N_SAMPLES   10     /* 100 Hz / 10 = 10 Hz readout */

static void send_imu_command(enum IMU_COMMANDS command)
{
    imu_command = command;
    imu_wake_up_security_task();
}

void imu_detection_task(void *arg)
{
    (void)arg;

    imu_ring_buffer_t buf;
    imu_ring_buffer_init(&buf);

    imu_tilt_filter_t tilt;
    imu_tilt_filter_init(&tilt);

    imu_walk_detector_t walk;
    imu_walk_detector_init(&walk);

    enum SECURITY_STATE prev_security_state = SECURITY_DISARMED;
    /* F13-ish debounce: IMU_TIER2_MOVEMENT_SUSTAINED is a one-shot flag for
     * Security Core (tier2_movement_sustained), not a repeated event, so it
     * must fire at most once per grace window -- otherwise every trigger
     * sample during the 5 s grace period would re-notify Security Core. */
    bool tier2_sustained_reported = false;
    uint32_t tier2_motion_run_samples = 0;
    uint32_t tier2_elapsed_samples = 0;  /* time-in-TIER2, for the tilt-change blind window */
    uint32_t sample_num = 0;

    /* TIER2's post-blind-window baseline: captured once, the instant the
     * blind window ends, then compared against on every later sample. */
    imu_vec3_t tier2_tilt_baseline = { 0.0f, 0.0f, 0.0f };
    bool tier2_tilt_baseline_valid = false;

    /* TIER2 walk-away one-shot: guards against re-sending IMU_TIER2_TO_TIER3
     * in the brief window before Security Core processes it and leaves TIER2
     * (IMU runs at a higher priority, so it isn't preempted on the send). */
    bool walk_reported = false;

    for (;;) {
        enum SECURITY_STATE state = security_state;
        bool entering_tier2 = (state == SECURITY_ARMED_TIER2 && prev_security_state != SECURITY_ARMED_TIER2);
        bool entering_quiet = (state == SECURITY_ARMED_QUIET && prev_security_state != SECURITY_ARMED_QUIET);
        bool entering_disarmed = (state == SECURITY_DISARMED && prev_security_state != SECURITY_DISARMED);
        bool leaving_disarmed = (state != SECURITY_DISARMED && prev_security_state == SECURITY_DISARMED);
        prev_security_state = state;

        if (entering_tier2) {
            tier2_sustained_reported = false;
            tier2_motion_run_samples = 0;
            tier2_elapsed_samples = 0;
            tier2_tilt_baseline_valid = false;
            walk_reported = false;
            imu_walk_detector_reset(&walk);
        }

        if (entering_quiet) {
            /* (Re)capture the tilt reference ONLY on (re-)entering QUIET --
             * deliberately NOT on QUIET->TIER2 or TIER2->TIER3. Escalating
             * into TIER2 happens precisely because the unit is mid-incident
             * (still tilted, possibly still moving); relocking the reference
             * there would capture that bad, transient attitude as the new
             * "zero", and then the thief setting the unit back down -- the
             * safe, correct outcome -- would itself read as a fresh tilt
             * away from that bad reference and could falsely escalate to
             * TIER3. Keeping the original armed-time reference through
             * TIER2/TIER3 means tilt_deg keeps meaning "distance from the
             * genuinely-at-rest armed pose" the whole time: it stays over
             * threshold for as long as the item is actually held away from
             * that pose (correctly counting as sustained evidence) and drops
             * back toward 0 the moment it's returned (correctly not
             * escalating). Only on lapsing back to QUIET (grace/tier3 timer
             * elapsed without a full disarm) do we accept wherever the unit
             * is now resting as the new baseline -- otherwise QUIET would
             * immediately re-trigger against the stale pre-incident one. */
            imu_tilt_filter_reset_reference(&tilt);
        }

        if (state == SECURITY_DISARMED) {
            /* Disarmed: idle instead of sampling. Blocks here until
             * something calls xTaskNotifyGive() on this task. */
            if (entering_disarmed) {
                ESP_LOGI(TAG, "disarmed, idling");
            }
            imu_ring_buffer_init(&buf);
            /* The unit can be moved arbitrarily while we're not sampling, so
             * the estimate is stale on re-arm; drop it and re-seed. */
            imu_tilt_filter_reset_estimate(&tilt);
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

        imu_ring_buffer_push(&buf, mag);

        uint32_t over_trig = imu_ring_buffer_count_over(&buf, TRIGGER_WINDOW_SAMPLES, TRIGGER_THRESHOLD_G);
        bool over_still = (mag > STILL_THRESHOLD_G);
        bool motion_trig = (over_trig >= TRIGGER_SAMPLE_COUNT);

        imu_tilt_filter_update(&tilt, &sample);
        float tilt_deg = imu_tilt_filter_get_tilt_deg(&tilt);
        bool tilt_over = imu_tilt_filter_reference_locked(&tilt) && (tilt_deg > TILT_TRIGGER_DEG);

        if (++sample_num >= PRINT_EVERY_N_SAMPLES) {
            sample_num = 0;
#if IMU_DATA_PRINT_ENABLED
            printf("accel [g]: x=%5.2f y=%5.2f z=%5.2f | mag=%4.2f | over_trig=%2" PRIu32 "/100 "
                   "| gyro [dps]: x=%6.1f y=%6.1f z=%6.1f "
                   "| tilt=%5.1f deg%s | security_state=%d\n",
                   sample.accel_x_g, sample.accel_y_g, sample.accel_z_g, mag, over_trig,
                   sample.gyro_x_dps, sample.gyro_y_dps, sample.gyro_z_dps,
                   tilt_deg, imu_tilt_filter_reference_locked(&tilt) ? "" : " (ref settling)", (int)state);
#endif
        }

        switch (state) {
        case SECURITY_ARMED_QUIET:
            if (tilt_over) {
                /* F8: a single sample past the angle threshold is enough --
                 * the filter's 0.5 s time constant already guarantees this
                 * is a real attitude change, not vibration. Immediate
                 * QUIET -> TIER2, no windowing. */
                ESP_LOGW(TAG, "tilt trigger: %.1f deg from armed reference (> %.0f deg)",
                         tilt_deg, TILT_TRIGGER_DEG);
                send_imu_command(IMU_QUIET_TO_TIER2);
            } else if (motion_trig) {
                send_imu_command(IMU_QUIET_TO_TIER2); /* F3 met -> F10 chirp */
            }
            /* Walk-away is handled in the TIER2 case (Path 3), not here:
             * S0->S1 already needs only a single motion/tilt trigger, so
             * there's nothing a QUIET walk detector would add. The QUIET->
             * TIER3 fast path (IMU_QUIET_TO_TIER3) is still unimplemented. */
            break;

        case SECURITY_ARMED_TIER2:
            /* Path 3 (F5): walk-away. Fed every TIER2 sample. Escalates
             * IMMEDIATELY via IMU_TIER2_TO_TIER3 -- not the deferred
             * IMU_TIER2_MOVEMENT_SUSTAINED the paths below use -- because
             * being carried off at a walking cadence is unambiguous theft in
             * progress and should not wait out the grace timer. Independent
             * of tier2_sustained_reported: a walk should override even if a
             * slower path already logged deferred evidence. Its own
             * walk_reported one-shot prevents duplicate sends. */
            if (!walk_reported && imu_walk_detector_update(&walk, accel_norm)) {
                ESP_LOGW(TAG, "walk-away trigger: %" PRIu32 " steps at walking cadence -> TIER3",
                         imu_walk_detector_step_count(&walk));
                send_imu_command(IMU_TIER2_TO_TIER3);
                walk_reported = true;
            }

            /* Two more independent evidence paths; tier2_sustained_reported
             * is a shared one-shot flag, so whichever confirms first is the
             * only one that reaches Security Core. */
            if (!tier2_sustained_reported) {
                /* Path 1: sustained translational motion (F3 window,
                 * unchanged). Must hold for TIER2_SUSTAIN_CONFIRM_SAMPLES
                 * straight -- letting go at any point resets the run, so a
                 * single bump followed by stillness rides out the grace
                 * timer instead of escalating. */
                if (motion_trig) {
                    if (++tier2_motion_run_samples >= TIER2_SUSTAIN_CONFIRM_SAMPLES) {
                        send_imu_command(IMU_TIER2_MOVEMENT_SUSTAINED);
                        tier2_sustained_reported = true;
                    }
                } else {
                    tier2_motion_run_samples = 0;
                }
            }

            if (!tier2_sustained_reported) {
                /* Path 2: ADDITIONAL tilt beyond wherever the unit was at
                 * the end of the blind window -- see TIER2_TILT_BLIND_
                 * SAMPLES' comment. Single-sample trigger past the blind
                 * window, same as the QUIET case; a unit left resting
                 * (even at a steep angle) never moves relative to this
                 * baseline again, so it correctly never fires. */
                if (++tier2_elapsed_samples <= TIER2_TILT_BLIND_SAMPLES) {
                    tier2_tilt_baseline_valid = false;
                } else if (imu_tilt_filter_direction_valid(&tilt)) {
                    if (!tier2_tilt_baseline_valid) {
                        tier2_tilt_baseline = imu_tilt_filter_get_direction(&tilt);
                        tier2_tilt_baseline_valid = true;
                    } else {
                        float tilt_change_deg = imu_tilt_angle_deg(imu_tilt_filter_get_direction(&tilt),
                                                                     tier2_tilt_baseline);
                        if (tilt_change_deg > TILT_TRIGGER_DEG) {
                            ESP_LOGW(TAG, "tier2 tilt-change trigger: %.1f deg since blind window (> %.0f deg)",
                                     tilt_change_deg, TILT_TRIGGER_DEG);
                            send_imu_command(IMU_TIER2_MOVEMENT_SUSTAINED);
                            tier2_sustained_reported = true;
                        }
                    }
                }
            }
            break;

        case SECURITY_ARMED_TIER3:
            /* More alert: the lower STILL_THRESHOLD_G bar, not the trigger
             * window. Reported on every over-threshold sample so Security
             * Core's tier3 auto-silence timer keeps resetting while motion
             * continues. tilt_over is deliberately NOT included here: it's a
             * static condition, and a unit left resting past the angle
             * threshold would reset the timer forever -- the siren would
             * never auto-silence (F12). Re-entering QUIET after the timer
             * lapses re-captures the tilt reference, accepting the new
             * resting attitude. */
            if (over_still) {
                send_imu_command(IMU_TIER3_MOVEMENT_DETECTED);
            }
            break;

        default:
            break;
        }
    }
}
