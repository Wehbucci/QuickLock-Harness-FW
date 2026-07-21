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

proximity_event_t proximity_evaluate(proximity_t *p)
{
    if (!p->seeded) {
        return PROX_NO_CHANGE; /* nothing to decide until we have a sample */
    }

    /* Hysteresis: two thresholds with a dead band between them. We only flip a
     * decision when the filtered RSSI crosses the FAR threshold in the relevant
     * direction, so noise inside the band cannot cause chatter. */
    if (!p->out_of_range && p->filt < p->cfg.out_threshold_dbm) {
        p->out_of_range = true;
        return PROX_WENT_OUT;
    }
    if (p->out_of_range && p->filt > p->cfg.in_threshold_dbm) {
        p->out_of_range = false;
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
