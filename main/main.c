/*
 * main.c — QuickLock harness entry point.
 *
 * Purpose: app_main wiring only. Create the application tasks (each defined in
 * its own component), bring up the BLE subsystem, and connect the two with the
 * BLE->Security bridge. All real work happens in those modules; this file just
 * composes them in the right order (HARNESS_BLE_TASK.md section 8).
 *
 * The ESP32 has two CPU cores (core 0 = PRO_CPU, core 1 = APP_CPU). ESP-IDF
 * ships FreeRTOS with SMP support, so a task is pinned with
 * xTaskCreatePinnedToCore(). Placement follows design doc Table 4 / F35: the
 * radio (controller, NimBLE host, ble_task) lives on core 0 so it can never
 * delay the safety-critical detection/alarm chain on core 1.
 *
 * Serves: F13, F14, F15, F17, F18, F22, F24, F35, F36 (via the modules it starts).
 */

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"

#include "ql_log.h"
#include "globals.h"
#include "led_task.h"
#include "alarm_task.h"
#include "battery_status_task.h"
#include "security_core_task.h"
#include "belt_detection_task.h"

#include "ble_events.h"
#include "ble_security_bridge.h"
#include "ble_hal.h"
#include "ble_task.h"
#include "ql_console.h"

QL_LOG_TAG("main");

/*
 * Stack depths, in BYTES (ESP-IDF's xTaskCreatePinnedToCore takes bytes, not the
 * words vanilla FreeRTOS uses). Every one of these tasks logs at least once, and
 * esp_log formats through a vprintf-style path that alone wants roughly a
 * kilobyte of stack, so these are sized with headroom rather than trimmed to the
 * minimum.
 */
#define LED_TASK_STACK            2048
#define ALARM_TASK_STACK          2048
#define BATTERY_STATUS_TASK_STACK 2048
#define SECURITY_CORE_TASK_STACK  3072

/* Priorities. SECURITY_CORE_TASK_PRIO must stay ABOVE
 * BLE_SECURITY_BRIDGE_PRIO (config.h) — see the bridge's concurrency note. */
#define LED_TASK_PRIO             2
#define ALARM_TASK_PRIO           4
#define BATTERY_STATUS_TASK_PRIO  1
#define SECURITY_CORE_TASK_PRIO   5

void app_main(void)
{
    QL_LOGI("Starting dual-core FreeRTOS QuickLock FW on %s (%d cores)",
            CONFIG_IDF_TARGET, CONFIG_FREERTOS_NUMBER_OF_CORES);

    gpio_install_isr_service(0);
    alarm_task_init();

    /* 1) Outbound event contract from BLE to Security. Created before anything
     *    can post to it. */
    ESP_ERROR_CHECK(ble_events_init());

    /* 2) Application tasks. The Security Core task must exist before the bridge
     *    starts, because the bridge notifies security_core_task_handle — which
     *    xTaskCreatePinnedToCore populates here. */
    xTaskCreatePinnedToCore(led_task,           /* task function            */
                            "led_task",         /* task name                */
                            LED_TASK_STACK,     /* stack depth in bytes     */
                            NULL,               /* argument passed to task  */
                            LED_TASK_PRIO,      /* priority                 */
                            &led_task_handle,   /* task handle              */
                            0);                 /* core to pin to: PRO_CPU  */

    xTaskCreatePinnedToCore(alarm_task,
                            "alarm_task",
                            ALARM_TASK_STACK,
                            NULL,
                            ALARM_TASK_PRIO,
                            &alarm_task_handle,
                            1);

    xTaskCreatePinnedToCore(battery_status_task,
                            "battery_status_task",
                            BATTERY_STATUS_TASK_STACK,
                            NULL,
                            BATTERY_STATUS_TASK_PRIO,
                            &battery_status_task_handle,
                            0);

    xTaskCreatePinnedToCore(security_core_task,
                            "security_core_task",
                            SECURITY_CORE_TASK_STACK,
                            NULL,
                            SECURITY_CORE_TASK_PRIO,
                            &security_core_task_handle,
                            1);

    belt_detection_init();
    /* 3) Bring up NVS + the NimBLE host and configure LE Secure Connections +
     *    bonding + Just Works. The stack finishes syncing asynchronously; the
     *    BLE task waits for that before it scans. */
    ESP_ERROR_CHECK(ble_hal_init());

    /* 4) Bridge the BLE event queue onto Security's ble_command + notify
     *    protocol. Started BEFORE ble_task so no event can sit unattended.
     *    This is the single point where the two subsystems meet. */
    ESP_ERROR_CHECK(ble_security_bridge_start());

    /* 5) Start the BLE Communication task (priority 3, core 0). It registers the
     *    inbound queue as the HAL event sink and runs the block-on-queue loop. */
    ESP_ERROR_CHECK(ble_task_start());

    /* 6) Bring-up console on the monitor UART (core 1, off the radio's core).
     *    The harness has no buttons yet, so this is the only way to reach the
     *    pairing window and bond management from outside. Stands in for the
     *    real UI; see ql_console.h. */
    ESP_ERROR_CHECK(ql_console_start());

    QL_LOGI("init complete; app_main returning (tasks run on)");
    /* app_main returns; the application tasks, the NimBLE host task, the BLE
     * task, the bridge, and the console REPL run on.
     *
     * Pairing: on first boot (no stored bond) the harness opens its pairing
     * window automatically. To force re-pairing later, use the console
     * (`bonds clear` then `pair`), or erase NVS (idf.py erase-flash).
     * TODO(ui): move `pair` onto the physical pairing button when the board HAL
     * exists — ble_task_enter_pairing_mode() will not change. */
}
