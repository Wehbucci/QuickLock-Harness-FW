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

#include "esp_err.h"

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
 */
void ble_task_enter_pairing_mode(void);

#endif /* QUICKLOCK_BLE_TASK_H */
