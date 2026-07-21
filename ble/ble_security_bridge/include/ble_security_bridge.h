/*
 * ble_security_bridge.h — adapter from the BLE event queue to the Security Core
 * task's global-variable + task-notification protocol.
 *
 * Purpose: the two subsystems were built against different integration styles.
 * The BLE task posts typed `ble_event_t`s onto a queue and never names its
 * consumer (HARNESS_BLE_TASK.md sections 3, 4, 11). The Security Core task
 * expects a producer to set the shared `ble_command` global and then notify it
 * (globals.h, `ble_wake_up_security_task()`). This module is the one place those
 * two conventions meet: it drains the queue and republishes each actionable
 * event as a command + notification.
 *
 * Keeping the translation here rather than inside `ble_events` preserves both
 * halves: `ble_events` stays pure FreeRTOS plumbing with no dependency on the
 * project-wide `globals.h`, and `ble_task` is unchanged and still testable with
 * no Security task present.
 *
 * Serves: F13, F14, F15, F22 (delivers those BLE decisions to Security).
 */

#ifndef QUICKLOCK_BLE_SECURITY_BRIDGE_H
#define QUICKLOCK_BLE_SECURITY_BRIDGE_H

#include "esp_err.h"

/*
 * ble_security_bridge_start — spawn the adapter task that consumes
 * ble_events_queue() and drives the Security Core task.
 *
 * Call once from app_main AFTER ble_events_init() and AFTER the Security Core
 * task has been created (the bridge notifies `security_core_task_handle`, which
 * xTaskCreatePinnedToCore populates). Returns ESP_OK, or ESP_ERR_INVALID_STATE
 * if the event queue does not exist yet, or ESP_ERR_NO_MEM if the task could not
 * be created.
 */
esp_err_t ble_security_bridge_start(void);

#endif /* QUICKLOCK_BLE_SECURITY_BRIDGE_H */
