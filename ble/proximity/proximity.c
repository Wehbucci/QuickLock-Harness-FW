/*
 * proximity.c — implementation of the pure RSSI-to-range decision module.
 *
 * No BLE / IDF / hardware includes on purpose (HARNESS_BLE_TASK.md section 6/8).
 * Only <math.h> for log10/pow. Serves: F14.
 */

#include "proximity.h"
#include <math.h>

/* NimBLE's sentinel for "RSSI unavailable" on ble_gap_conn_rssi(). Defined here
 * (not pulled from a stack header) to keep this module dependency-free. */
#define RSSI_UNAVAILABLE_SENTINEL 127

bool proximity_rssi_is_valid(int8_t raw_rssi)
{
    /* Valid link RSSI is strongly negative; 127 means "no reading". Reject the
     * sentinel and any nonnegative value, which cannot be a real link RSSI. */
    return raw_rssi != RSSI_UNAVAILABLE_SENTINEL && raw_rssi < 0;
}

void proximity_init(proximity_t *p, const proximity_config_t *cfg)
{
    p->cfg = *cfg;
    p->filt = 0.0f;
    p->seeded = false;
    /* Start life IN range: on boot the fob is next to the harness, and we do not
     * want a spurious out-of-range before the first real sample lands. */
    p->out_of_range = false;
    p->out_pending = false;
    p->out_since_ms = 0;
}

float proximity_update(proximity_t *p, int8_t raw_rssi)
{
    const float raw = (float)raw_rssi;

    if (!p->seeded) {
        /* Seed the EMA with the first sample instead of easing up from 0 dBm,
         * which would otherwise take several samples to converge and could trip
         * a false out-of-range at startup. */
        p->filt = raw;
        p->seeded = true;
    } else {
        /* First-order EMA: R_filt = alpha*R_raw + (1-alpha)*R_filt_prev. */
        p->filt = p->cfg.alpha * raw + (1.0f - p->cfg.alpha) * p->filt;
    }
    return p->filt;
}

proximity_event_t proximity_evaluate(proximity_t *p, uint32_t now_ms)
{
    if (!p->seeded) {
        return PROX_NO_CHANGE; /* nothing to decide until we have a sample */
    }

    /* Hysteresis: two thresholds with a dead band between them, so noise inside
     * the band cannot cause chatter. On top of that, the OUT direction is
     * dwell-debounced: the filtered RSSI must stay below out_threshold for
     * out_confirm_ms CONTINUOUSLY before we declare out-of-range. This is what
     * stops a transient misread from auto-arming the system (F14). The genuine
     * "walked away" case is still caught quickly by the supervision-timeout path
     * (Mechanism A in ble_task.c), so this dwell does not weaken fail-secure. */
    if (!p->out_of_range) {
        if (p->filt < p->cfg.out_threshold_dbm) {
            /* Candidate out-of-range. Start the dwell timer on the first dip, or
             * check whether it has now been held long enough. */
            if (!p->out_pending) {
                p->out_pending = true;
                p->out_since_ms = now_ms;
            }
            /* Unsigned subtraction is wrap-safe across the ~49-day now_ms rollover. */
            if ((uint32_t)(now_ms - p->out_since_ms) >= p->cfg.out_confirm_ms) {
                p->out_pending = false;
                p->out_of_range = true;
                return PROX_WENT_OUT;
            }
        } else {
            /* Climbed back above out_threshold before the dwell elapsed: the dip
             * was transient, so cancel the pending timer. Next dip starts fresh. */
            p->out_pending = false;
        }
        return PROX_NO_CHANGE;
    }

    /* Currently out of range: recovery is prompt (no dwell). */
    if (p->filt > p->cfg.in_threshold_dbm) {
        p->out_of_range = false;
        p->out_pending = false;
        return PROX_CAME_IN;
    }
    return PROX_NO_CHANGE;
}

bool proximity_is_out_of_range(const proximity_t *p)
{
    return p->out_of_range;
}

float proximity_filtered_rssi(const proximity_t *p)
{
    return p->filt;
}

float proximity_distance_m(const proximity_t *p, float rssi_dbm)
{
    /* Invert RSSI(d) = -10*n*log10(d/d0) + C, with d0 = 1 m:
     *     d = 10 ^ ((C - RSSI) / (10 * n))
     * Guard against a zero/negative n from a bad config (would divide by 0). */
    if (p->cfg.n <= 0.0f) {
        return -1.0f;
    }
    return powf(10.0f, (p->cfg.c_dbm - rssi_dbm) / (10.0f * p->cfg.n));
}
