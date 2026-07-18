/*
 * main.c — QuickLock harness entry point (BLE communication subsystem bring-up).
 *
 * Purpose: app_main wiring only. Bring up the outbound event queue and its stub
 * Security consumer, initialise the BLE HAL (NVS + NimBLE host), then start the
 * BLE Communication task. All real work happens in those modules; this file just
 * composes them in the right order (HARNESS_BLE_TASK.md section 8).
 *
 * Scope: only the BLE communication task and its stub consumer. Detection,
 * alarm, LED, battery, and scheduling subsystems are intentionally absent.
 *
 * Serves: F13, F14, F15, F17, F18, F22, F24, F36 (via the modules it starts).
 */

#include "esp_log.h"
#include "esp_err.h"

#include "ble_events.h"
#include "ble_hal.h"
#include "ble_task.h"
#include "ql_console.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "QuickLock harness BLE subsystem starting");

    /* 1) Outbound event contract to the (future) Security task, plus the stub
     *    consumer that drains + logs it so we can verify without the fob.
     *    TODO(security-core): replace the stub with the real Security task. */
    ESP_ERROR_CHECK(ble_events_init());
    ble_events_start_stub_consumer();

    /* 2) Bring up NVS + the NimBLE host and configure LE Secure Connections +
     *    bonding + Just Works. The stack finishes syncing asynchronously; the
     *    BLE task waits for that before it scans. */
    ESP_ERROR_CHECK(ble_hal_init());

    /* 3) Start the BLE Communication task (priority 3, core 0). It registers the
     *    inbound queue as the HAL event sink and runs the block-on-queue loop. */
    ESP_ERROR_CHECK(ble_task_start());

    /* 4) Bring-up console on the monitor UART (core 1, off the radio's core).
     *    The harness has no buttons yet, so this is the only way to reach the
     *    pairing window and bond management from outside. Stands in for the
     *    real UI; see ql_console.h. */
    ESP_ERROR_CHECK(ql_console_start());

    ESP_LOGI(TAG, "init complete; app_main returning (tasks run on)");
    /* app_main returns; the BLE task, NimBLE host task, stub consumer, and the
     * console REPL run on.
     *
     * Pairing: on first boot (no stored bond) the harness opens its pairing
     * window automatically. To force re-pairing later, use the console
     * (`bonds clear` then `pair`), or erase NVS (idf.py erase-flash).
     * TODO(ui): move `pair` onto the physical pairing button when the board HAL
     * exists — ble_task_enter_pairing_mode() will not change. */
}
