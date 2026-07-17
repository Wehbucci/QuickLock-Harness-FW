/*
 * battery_status_task.c
 *
 * Polls the battery level and maintains the shared battery_state. On a
 * high->low transition it wakes the LED task and requests an alarm chirp; on a
 * low->high transition it wakes the LED task.
 */

#include "battery_status_task.h"
#include "globals.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Battery State Consts */
const int LOW_BATTERY_THRESHOLD = 20;

void battery_status_task(void *arg)
{
    int battery_percentage;
    while (1) {
        // TODO: Check battery %
        battery_percentage = 22;

        if (battery_percentage <= LOW_BATTERY_THRESHOLD) {
            if (battery_state == BATTERY_HIGH) {
                xTaskNotifyGive(led_task_handle);
                request_chirp();
            }
            battery_state = BATTERY_LOW;
        } else {
            if (battery_state == BATTERY_LOW) {
                xTaskNotifyGive(led_task_handle);
            }
            battery_state = BATTERY_HIGH;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
