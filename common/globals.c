/*
 * globals.c
 *
 * The single definition point for every shared global declared `extern` in
 * globals.h. Keeping the definitions here (rather than in a header) avoids
 * multiple-definition linker errors, since a file-scope object/const in C has
 * external linkage.
 */

#include "globals.h"

/* Global task handles (populated by main.c at task-creation time). */
TaskHandle_t led_task_handle;
TaskHandle_t alarm_task_handle;
TaskHandle_t battery_status_task_handle;
TaskHandle_t security_core_task_handle;

/* Global communication state. */
enum SECURITY_STATE security_state = SECURITY_DISARMED;   /* Owned by Security Core Task   */
enum BATTERY_STATE  battery_state  = BATTERY_HIGH;        /* Owned by Battery Status Task  */
enum BLE_COMMANDS   ble_command    = BLE_NO_COMMAND;      /* Owned by BLE Task             */
enum BELT_STATE     belt_state     = BELT_UNKNOWN;        /* Owned by Belt Detection Task  */

/* Notification bit constants. */
const uint32_t ALARM_CHIRP_BIT            = 1UL << 1;
const uint32_t SECURITY_BLE_BIT           = 1UL << 1;
const uint32_t SECURITY_BELT_DETECTION_BIT = 1UL << 2;

/* Notify the alarm task to emit a short chirp. Safe to call from any task. */
void request_chirp(void)
{
    xTaskNotify(alarm_task_handle, ALARM_CHIRP_BIT, eSetBits);
}
