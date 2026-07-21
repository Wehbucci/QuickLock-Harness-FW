/*
 * led_task.c
 *
 * Drives the on-board status LED. The task blocks until notified, then sets the
 * LED according to the current security and battery state.
 */

#include "led_task.h"
#include "globals.h"
#include "ql_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

QL_LOG_TAG("led");

/* Built-in LED on most ESP32 dev boards. */
#define BUILTIN_LED_GPIO GPIO_NUM_2

void led_task(void *arg)
{
    QL_LOGI("task started on core %d", xPortGetCoreID());

    gpio_reset_pin(BUILTIN_LED_GPIO);
    gpio_set_direction(BUILTIN_LED_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        QL_LOGI("woke up;");

        if (security_state != SECURITY_DISARMED && battery_state == BATTERY_HIGH) {
            gpio_set_level(BUILTIN_LED_GPIO, 1);  // turn builtin LED on
        } else if (security_state != SECURITY_DISARMED && battery_state == BATTERY_LOW) {
            gpio_set_level(BUILTIN_LED_GPIO, 1);  // turn builtin LED on
            // start a timer to blink on for 500ms every 1000ms
        } else if (security_state == SECURITY_DISARMED && battery_state == BATTERY_HIGH) {
            gpio_set_level(BUILTIN_LED_GPIO, 0);  // turn builtin LED off
        } else if (security_state == SECURITY_DISARMED && battery_state == BATTERY_LOW) {
            gpio_set_level(BUILTIN_LED_GPIO, 1);  // turn builtin LED on
            // start a timer to blink on for 500ms every 5000ms
        } else {
            QL_LOGW("unhandled security_state=%d battery_state=%d",
                    (int)security_state, (int)battery_state);
        }
    }
}
