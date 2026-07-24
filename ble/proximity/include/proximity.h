/*
 * proximity.h — pure RSSI-to-range decision module (no BLE, no IDF, no hardware).
 *
 * Purpose: turn a stream of noisy raw RSSI samples into a stable in-range /
 * out-of-range decision for the Mechanism-B soft auto-arm trigger
 * (design doc 3.4.4, HARNESS_BLE_TASK.md section 6, F14).
 *
 * This module is deliberately dependency-free — it includes only <stdint.h> and
 * <stdbool.h> — so the log-distance model, the EMA filter, and the hysteresis
 * can be reasoned about and unit-tested on a host or under Unity, with no radio
 * in the loop. The HAL calls ble_gap_conn_rssi() and hands the raw value here;
 * this module never touches the stack.
 *
 * Serves: F14.
 */

#ifndef QUICKLOCK_PROXIMITY_H
#define QUICKLOCK_PROXIMITY_H

#include <stdint.h>
#include <stdbool.h>

/* Tuning constants for the model + filter. Values come from config.h at the
 * call site; kept as a struct so tests can inject their own without config.h. */
typedef struct {
    float    c_dbm;             /* RSSI at reference distance d0 = 1 m */
    float    n;                 /* path-loss exponent */
    float    alpha;             /* EMA weight on the newest raw sample, 0..1 */
    float    out_threshold_dbm; /* filtered RSSI below this -> candidate out of range */
    float    in_threshold_dbm;  /* filtered RSSI above this -> back in range */
    uint32_t out_confirm_ms;    /* filtered RSSI must stay below out_threshold_dbm
                                 * CONTINUOUSLY for this long before out-of-range is
                                 * DECLARED. Debounces transient RSSI misreads so a
                                 * momentary dip cannot auto-arm (F14). 0 = declare
                                 * immediately on crossing (legacy behaviour). */
} proximity_config_t;

/* Edge reported by proximity_evaluate(): only the transitions, not the level. */
typedef enum {
    PROX_NO_CHANGE = 0, /* still in whatever state it was */
    PROX_WENT_OUT,      /* crossed from in-range to out-of-range */
    PROX_CAME_IN,       /* crossed from out-of-range back to in-range */
} proximity_event_t;

/* Filter/decision state. Treat as opaque; use the functions below. */
typedef struct {
    proximity_config_t cfg;
    float    filt;         /* current filtered RSSI (EMA output), dBm */
    bool     seeded;       /* false until the first sample seeds the EMA */
    bool     out_of_range; /* current latched decision */
    bool     out_pending;  /* filtered RSSI is below out_threshold and being timed */
    uint32_t out_since_ms; /* timestamp the current candidate-out dip began */
} proximity_t;

/*
 * proximity_init — reset a proximity instance with the given tuning.
 * Side effects: clears filter/hysteresis state; starts life IN range and
 * unseeded (the first sample seeds the EMA directly rather than easing up from
 * an arbitrary zero).
 */
void proximity_init(proximity_t *p, const proximity_config_t *cfg);

/*
 * proximity_update — feed one raw RSSI sample through the EMA filter.
 * Param raw_rssi: newest raw sample in dBm (NimBLE reports 127 when unavailable;
 *                 the caller should drop those before calling — see is_valid()).
 * Returns: the updated filtered RSSI in dBm.
 * Side effects: advances the internal filter state. Does NOT itself change the
 * hysteresis decision — call proximity_evaluate() for that, so "filter" and
 * "decide" stay separable.
 */
float proximity_update(proximity_t *p, int8_t raw_rssi);

/*
 * proximity_evaluate — apply hysteresis + the out-of-range dwell to the current
 * filtered RSSI and report any in/out edge. Call once per sample, after
 * proximity_update().
 *
 * Param now_ms: a monotonic millisecond timestamp (any epoch; only differences
 *               are used, and the difference is computed rollover-safe). Needed
 *               so the out_confirm_ms dwell is measured in real time rather than
 *               in sample counts, which keeps it correct if the sample cadence
 *               changes.
 * Returns: PROX_WENT_OUT only after the filtered RSSI has stayed below
 *          out_threshold for out_confirm_ms CONTINUOUSLY; PROX_CAME_IN
 *          immediately on crossing back above in_threshold; else PROX_NO_CHANGE.
 * Side effects: updates the latched out_of_range state and the dwell timer.
 *
 * Note: coming back in range is deliberately NOT dwell-debounced — recovery
 * should be prompt, and a brief dip that started the timer is simply cancelled
 * the moment the filtered value climbs back above out_threshold.
 */
proximity_event_t proximity_evaluate(proximity_t *p, uint32_t now_ms);

/* Current latched decision (true == out of range). No side effects. */
bool proximity_is_out_of_range(const proximity_t *p);

/* Current filtered RSSI in dBm. No side effects. */
float proximity_filtered_rssi(const proximity_t *p);

/*
 * proximity_distance_m — invert the log-distance path-loss model for a given
 * RSSI: d = 10 ^ ((C - RSSI) / (10 * n)). Pure helper for logging/telemetry;
 * the range decision uses thresholds on RSSI directly (cheaper, and avoids
 * amplifying noise through the exponential). No side effects.
 */
float proximity_distance_m(const proximity_t *p, float rssi_dbm);

/*
 * proximity_rssi_is_valid — true if a raw sample is usable. NimBLE returns 127
 * when RSSI is unavailable; such samples must not enter the filter.
 */
bool proximity_rssi_is_valid(int8_t raw_rssi);

#endif /* QUICKLOCK_PROXIMITY_H */
