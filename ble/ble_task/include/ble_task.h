/*
 * ble_task.h — the BLE Communication task (application state machine).
 *
 * Purpose: own the BLE central role's connection state machine and the proximity
 * decision. This task NEVER calls NimBLE or touches GPIO directly — it goes
 * through ble_hal — and NEVER calls the Security task directly — it posts to the
 * ble_events queue. It runs the block-on-queue loop from HARNESS_BLE_TASK.md
 * section 3: sleep on the inbound queue, wake on a HAL event or a 20 ms
 * housekeeping tick.
 *
 * Placement (design doc Table 4, F35): FreeRTOS priority 3, pinned to core 0.
 *
 * Serves: F13, F14, F15, F17, F22, F24.
 */

#ifndef QUICKLOCK_BLE_TASK_H
#define QUICKLOCK_BLE_TASK_H

#include <stdint.h>
#include "esp_err.h"
#include "config.h"   /* QL_TEST_HOOKS_ENABLED */

/*
 * ble_task_start — create the inbound queue, register it as the HAL event sink,
 * and spawn the BLE Communication task (priority 3, core 0). Call once from
 * app_main AFTER ble_hal_init() and ble_events_init(). Returns ESP_OK, or an
 * esp_err_t on allocation failure. The task waits internally for the NimBLE host
 * to finish syncing before it starts scanning.
 */
esp_err_t ble_task_start(void);

/*
 * ble_task_enter_pairing_mode — open the pairing-mode window (F15) so the harness
 * will accept a NEW bond for PAIRING_WINDOW_MS. Thread-safe to call from another
 * task/ISR-free context (it posts an internal request). Intended for a future
 * "pair" button or console command; the harness also opens this window
 * automatically on first boot when no bond is stored yet.
 *
 * TODO(ui): wire this to the physical pairing button when the board HAL exists.
 * Wired to the serial console's `pair` command in the meantime (ql_console).
 */
void ble_task_enter_pairing_mode(void);

/*
 * ble_task_log_status — dump the state machine's current state, connection
 * handle, bond count, pairing window, and latest filtered RSSI to the log.
 * Read-only; safe to call from another task (the console). The values are read
 * without a lock: each is a single aligned word owned by the BLE task, so a
 * snapshot can be one tick stale but never torn — which is fine for a human
 * asking "where are we?".
 */
void ble_task_log_status(void);

#if QL_TEST_HOOKS_ENABLED
/*
 * ble_task_inject_rssi — force every subsequent RSSI sample to `dbm` instead of
 * reading the radio, until ble_task_clear_rssi_override().
 *
 * Why this exists: Mechanism B (F14) is a filter and a hysteresis band feeding
 * two events, and verifying it by walking away tests the UNCALIBRATED path-loss
 * constants at the same time — so a failure is ambiguous. Injecting the sample
 * makes the decision logic deterministic and testable at a desk. It does NOT
 * replace the walk test, which is the only thing that validates the constants.
 *
 * The injected value still flows through the real EMA and the real thresholds;
 * only the source of the raw sample changes.
 */
void ble_task_inject_rssi(int8_t dbm);

/* Return to reading real RSSI from the radio. */
void ble_task_clear_rssi_override(void);
#endif /* QL_TEST_HOOKS_ENABLED */

#endif /* QUICKLOCK_BLE_TASK_H */
