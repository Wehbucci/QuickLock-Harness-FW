/*
 * config.h — QuickLock harness BLE tuning constants (one place, no magic numbers).
 *
 * Purpose: every threshold, interval, and task parameter used by the BLE
 * subsystem, expressed in HUMAN units (milliseconds, dBm, plain integers).
 * Raw BLE unit conversions (1.25 ms / 10 ms) happen in exactly one clearly
 * commented place — see ble_hal.c. HARNESS_BLE_TASK.md section 7.
 *
 * Stack-agnostic: no NimBLE / IDF includes, so the pure proximity module and
 * the application logic can share these without pulling in the radio stack.
 *
 * Serves: F14, F16, F18, F22, F24, F26, F35.
 */

#ifndef QUICKLOCK_CONFIG_H
#define QUICKLOCK_CONFIG_H

/* -------------------------------------------------------------------------- */
/* Task loop cadence (HARNESS_BLE_TASK.md section 3)                           */
/* -------------------------------------------------------------------------- */

/* Housekeeping fallback period: the BLE task blocks on its inbound queue for at
 * most this long, then wakes to do periodic work (RSSI, range, pairing timer).
 * This is a fallback tick, NOT a poll — events unblock the task immediately. */
#define HOUSEKEEP_MS 20

/* -------------------------------------------------------------------------- */
/* Connection parameters (BLE_CONTRACT.md section 4) — HUMAN units             */
/* Converted to raw BLE units in ble_hal.c only.                              */
/* -------------------------------------------------------------------------- */

#define CONN_INTERVAL_MS   1000 /* ~1 Hz; fob power budget (F26) + RSSI cadence */
#define PERIPHERAL_LATENCY 0    /* start at 0; fob may negotiate up to 4        */
#define SUPERVISION_MS     4000 /* hard link-loss trigger; 4 s stays inside F14 */

/* -------------------------------------------------------------------------- */
/* Proximity model + filter (design doc 3.4.4, HARNESS_BLE_TASK.md section 6)  */
/* Bench-calibration constants. All consumed by the pure proximity module.     */
/* -------------------------------------------------------------------------- */

#define RSSI_C_DBM (-60.0f) /* measured RSSI at d0 = 1 m */
#define RSSI_N     (2.0f)   /* path-loss exponent */
#define RSSI_ALPHA (0.15f)  /* first-order EMA weight on the newest raw sample */

/* Hysteresis band (dBm on the FILTERED RSSI) to stop chatter at the boundary.
 * With C=-60, n=2, ~5 m separation lands near -74 dBm. */
#define OUT_THRESHOLD_DBM (-74.0f) /* below -> out of range */
#define IN_THRESHOLD_DBM  (-70.0f) /* above -> back in range */

/* How often the harness samples connected-link RSSI (~1 Hz by nature). */
#define RSSI_SAMPLE_MS 1000

/* -------------------------------------------------------------------------- */
/* Link-loss / re-acquire / pairing window                                    */
/* -------------------------------------------------------------------------- */

/* Grace after a supervision-timeout disconnect before we declare the user gone
 * and post the hard auto-arm event (F22/F14). Kept short so 4 s + grace < 10 s. */
#define REACQUIRE_GRACE_MS 3000

/* How long the harness accepts a NEW bond after being put into pairing mode
 * (F15). Outside this window it reconnects only to known bonds. */
#define PAIRING_WINDOW_MS 30000

/* Stored-bond capacity (F16, non-essential). Must match BT_NIMBLE_MAX_BONDS in
 * sdkconfig.defaults. */
#define MAX_BONDS 2

/* Reach CONNECTED from SCANNING within this budget on a known bond (F24). Used
 * for logging/assertions during bring-up. */
#define CONNECT_BUDGET_MS 3000

/* -------------------------------------------------------------------------- */
/* BLE Communication task placement (design doc Table 4, F35)                  */
/* -------------------------------------------------------------------------- */

#define BLE_TASK_PRIO  3    /* per Table 4 */
#define BLE_TASK_CORE  0    /* pinned to core 0 with the controller + host task */
#define BLE_TASK_STACK 4096 /* words; state machine + logging headroom */

/* Depth of the inbound queue NimBLE callbacks post into. A handful of events
 * can burst (connect -> enc -> disc -> subscribe); this leaves slack. */
#define BLE_INBOUND_QUEUE_LEN 16

/* Depth of the outbound queue to the (future) Security task. */
#define BLE_EVENT_QUEUE_LEN 16

/* -------------------------------------------------------------------------- */
/* Bench test hooks (bring-up only)                                            */
/* -------------------------------------------------------------------------- */

/* Enables the test affordances the serial console drives — today just the RSSI
 * override in ble_task (see ble_task_inject_rssi), which lets Mechanism B be
 * exercised at a desk rather than by walking 5 m away against an UNCALIBRATED
 * path-loss model (RSSI_C_DBM / RSSI_N are still their untuned defaults, so a
 * walk test measures the constants as much as the code).
 *
 * Set to 0 for a production build: the override then cannot exist at runtime,
 * because the code that reads it is not compiled. Read-only console commands
 * (status/bonds) and the pairing window are NOT gated by this — they call
 * ordinary API the firmware already exposes. */
#define QL_TEST_HOOKS_ENABLED 1

/* Console task placement. Core 1 on purpose: core 0 carries the controller,
 * the NimBLE host, and the BLE task, and a human typing at a terminal must
 * never contend with the radio (F35). */
#define QL_CONSOLE_TASK_PRIO  2
#define QL_CONSOLE_TASK_CORE  1
#define QL_CONSOLE_TASK_STACK 4096

#endif /* QUICKLOCK_CONFIG_H */
