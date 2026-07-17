/*
 * alarm_task.c
 *
 * Drives the alarm output according to the security state and services chirp
 * requests from other tasks.
 */

#include "alarm_task.h"
#include "globals.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

void alarm_task(void *arg)
{
    /* Notification Bits */
    /* Bits 31-2 | Bit 1         | Bit 0           */
    /* Unused    | Chirp request | General wake up */

    uint32_t notification_value;
    bool chirp_requested = false;
    while (1) {
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        chirp_requested = notification_value & ALARM_CHIRP_BIT;

        if (security_state == SECURITY_DISARMED || security_state == SECURITY_ARMED_QUIET) {
            // TODO: drive alarm off
        } else if (security_state == SECURITY_ARMED_TIER2) {
            // TODO: drive alarm half
        } else if (security_state == SECURITY_ARMED_TIER3) {
            // TODO: drive alarm full
        } else {
            // LOG: Unknown security state
        }

        if (chirp_requested && (security_state == SECURITY_DISARMED || security_state == SECURITY_ARMED_QUIET)) {
            // TODO: chirp the alarm
        }
    }
}
