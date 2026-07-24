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

/* PROVISIONAL calibration from sample_data/rssi_calibration_all.csv (2026-07-22),
 * fitted by sample_data/analyze_rssi.py excluding the 1.5 m point (a multipath
 * null, sigma 5.7 dB). Least-squares over the remaining points: R2 = 0.92.
 *
 * !! MEASURED AGAINST A COMPROMISED LINK. The whole sweep sat ~25-33 dB below
 * free-space theory (1 m read -77 dBm, not the expected -45 to -55), so N came
 * out at 1.32 -- below free space, physically implausible -- and beyond ~6 m
 * the readings are within noise of each other. Re-calibrate after the antenna
 * placement / TX-power fix (fob TX is only -4 dBm; harness sets no TX power at
 * all). These values make the harness usable now; they are NOT the final ones. */
#define RSSI_C_DBM (-76.2f) /* fitted RSSI at d0 = 1 m (direct 1 m mean was -77.0) */
#define RSSI_N     (1.32f)  /* fitted path-loss exponent (implausibly low; see above) */
#define RSSI_ALPHA (0.15f)  /* first-order EMA weight on the newest raw sample */

/* Hysteresis band (dBm on the FILTERED RSSI) to stop chatter at the boundary.
 * Set from measured means, NOT by inverting the model above. With this data the
 * only clean split is near/far: means were ~-80 dBm at 3 m and ~-86 at 4.5 m,
 * so OUT=-84 reliably declares out-of-range beyond ~4 m while staying in-range
 * at <=3 m; IN=-80 requires closing back to ~3 m to re-enter. The 4 dB gap is
 * comfortably wider than the ~1.5 dB filtered noise, so it will not chatter.
 *
 * !! EFFECTIVE TRIGGER IS ~3-4 m, NOT the 10 m target: with this data 9/10.5/12 m
 * are statistically indistinguishable, so no threshold can trigger at 10 m.
 * Revisit together with RSSI_C_DBM/RSSI_N after the RF fix. */
#define OUT_THRESHOLD_DBM (-84.0f) /* below -> candidate out of range (~beyond 4 m here) */
#define IN_THRESHOLD_DBM  (-80.0f) /* above -> back in range (~within 3 m here) */

/* Out-of-range DWELL: the filtered RSSI must stay below OUT_THRESHOLD_DBM
 * continuously for this long before the harness declares the fob out of range
 * (Mechanism B). A single misread or a short run of bad samples resets the
 * timer, so a transient dip can no longer auto-arm the system.
 *
 * F14 note: this only slows the SOFT RSSI trigger. Genuine departure still trips
 * the hard supervision-timeout path (Mechanism A: SUPERVISION_MS + REACQUIRE_
 * GRACE_MS = 7 s) well inside F14's 10 s, so fail-secure is not weakened.
 * At the ~1 Hz RSSI cadence this is ~10 consecutive out-of-range samples. */
#define OUT_CONFIRM_MS 10000

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

/* Depth of the outbound queue to the Security task. */
#define BLE_EVENT_QUEUE_LEN 16

/* -------------------------------------------------------------------------- */
/* BLE -> Security Core bridge placement                                       */
/* -------------------------------------------------------------------------- */

/* These three values are a correctness constraint, not a tuning knob.
 *
 * The bridge hands commands to the Security Core task through the unlocked
 * global `ble_command`, so it must not enqueue a second command before Security
 * has read the first. Pinning the bridge to Security's core (1) at a priority
 * BELOW Security's (5, set in main.c) makes ble_wake_up_security_task() preempt
 * the bridge immediately, so Security consumes the command before the bridge
 * runs again. Moving the bridge to core 0, or raising it to >= Security's
 * priority, reintroduces a lost-command race. See ble_security_bridge.c. */
#define BLE_SECURITY_BRIDGE_PRIO  2
#define BLE_SECURITY_BRIDGE_CORE  1
#define BLE_SECURITY_BRIDGE_STACK 3072

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
