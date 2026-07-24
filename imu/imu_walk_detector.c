#include "imu_walk_detector.h"

/* Peak detection with hysteresis: one step is counted per rising crossing of
 * (1 g + STEP_PEAK_HI_G); the signal must fall back below (1 g + STEP_PEAK_LO_G)
 * before another can count. The HI/LO gap debounces noise sitting on the
 * threshold, so a single footstep impact yields exactly one count. Gentle
 * carry accelerations, so these sit well below imu_detection.c's 0.5 g F3
 * trigger. Bench-tunable like F6/F14. */
#define STEP_PEAK_HI_G   0.12f   /* rising past 1g + this = candidate step */
#define STEP_PEAK_LO_G   0.05f   /* must drop below 1g + this to re-arm */

/* Cadence gate, in samples at the 100 Hz design point (T1). A crossing is a
 * valid next step only if its spacing from the previous step falls in
 * [MIN, MAX]:
 *   - below MIN (~3.3 Hz): too fast for a stride -- vibration, or the second
 *     sub-peak of one footstep. Ignored WITHOUT disturbing the run or the
 *     clock, so the following genuine step is still measured from the last
 *     real one.
 *   - above MAX (1 Hz): the rhythm broke (a pause, an isolated bump). The
 *     crossing still counts, but as the start of a fresh run (length 1).
 * Requiring STEP_COUNT_FOR_WALK consecutive in-band steps is what makes this
 * specific to walking: random handling essentially never lands 5 impacts in
 * a row all within the stride band. */
#define STEP_MIN_INTERVAL_SAMPLES  30   /* 0.30 s -> 3.3 Hz cadence ceiling */
#define STEP_MAX_INTERVAL_SAMPLES  100  /* 1.00 s -> 1.0 Hz cadence floor */

/* Consecutive in-cadence steps to declare walking. At a ~2 Hz cadence this
 * confirms in ~2-2.5 s -- decisive without being trigger-happy. Raising it
 * hardens against false positives at the cost of a slower escalation. */
#define STEP_COUNT_FOR_WALK  5

void imu_walk_detector_init(imu_walk_detector_t *w)
{
    imu_walk_detector_reset(w);
}

void imu_walk_detector_reset(imu_walk_detector_t *w)
{
    w->above = false;
    w->primed = false;
    w->sample_index = 0;
    w->last_step_sample = 0;
    w->consecutive_steps = 0;
}

bool imu_walk_detector_update(imu_walk_detector_t *w, float accel_norm_g)
{
    w->sample_index++;

    if (!w->above && accel_norm_g > 1.0f + STEP_PEAK_HI_G) {
        /* Rising crossing = candidate step (a footstep impact peak). */
        w->above = true;

        if (!w->primed) {
            /* First step this episode: nothing to measure a cadence against. */
            w->primed = true;
            w->last_step_sample = w->sample_index;
            w->consecutive_steps = 1;
        } else {
            uint32_t interval = w->sample_index - w->last_step_sample;
            if (interval < STEP_MIN_INTERVAL_SAMPLES) {
                /* Too soon: refractory / vibration. Ignore entirely -- leave
                 * the clock on the previous genuine step. */
            } else {
                if (interval <= STEP_MAX_INTERVAL_SAMPLES) {
                    w->consecutive_steps++;
                } else {
                    w->consecutive_steps = 1; /* rhythm broke; start a new run */
                }
                w->last_step_sample = w->sample_index;
            }
        }
    } else if (w->above && accel_norm_g < 1.0f + STEP_PEAK_LO_G) {
        /* Falling crossing: re-arm for the next peak. */
        w->above = false;
    }

    return w->consecutive_steps >= STEP_COUNT_FOR_WALK;
}

uint32_t imu_walk_detector_step_count(const imu_walk_detector_t *w)
{
    return w->consecutive_steps;
}
