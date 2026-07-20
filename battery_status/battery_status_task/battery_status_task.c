/*
 * battery_status_task.c
 *
 * Polls the battery level and maintains the shared battery_state. On a
 * high->low transition it wakes the LED task and requests an alarm chirp; on a
 * low->high transition it wakes the LED task.
 */

#include <stdio.h>
#include "battery_status_task.h"
#include "globals.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Battery State Consts */
const int LOW_BATTERY_THRESHOLD = 20;

void battery_status_task(void *arg)
{
    printf("Starting task battery_status_task on core %d\n", xPortGetCoreID());

    int battery_percentage;
    while (1) {
        // TODO: Check battery %
        battery_percentage = 22;

        if (battery_percentage <= LOW_BATTERY_THRESHOLD) {
            if (battery_state == BATTERY_HIGH) {
                wake_up_led_task();
                request_chirp();
            }
            battery_state = BATTERY_LOW;
        } else {
            if (battery_state == BATTERY_LOW) {
                wake_up_led_task();
            }
            battery_state = BATTERY_HIGH;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
