/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/*
 * Dual-core FreeRTOS "Hello World".
 *
 * The ESP32 has two CPU cores (core 0 = PRO_CPU, core 1 = APP_CPU). ESP-IDF
 * ships a version of FreeRTOS with Symmetric Multiprocessing (SMP) support, so
 * a task can be pinned to a specific core with xTaskCreatePinnedToCore().
 *
 * This example starts one task on each core. Both tasks print "Hello world"
 * together with the core they are actually running on, so you can watch the
 * two cores run in parallel.
 */

#include <climits>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

/* Built-in LED on most ESP32 dev boards. */
#define BUILTIN_LED_GPIO GPIO_NUM_2

enum SECURITY_STATE {
    SECURITY_DISARMED,
    SECURITY_ARMED_QUIET,
    SECURITY_ARMED_TIER2,
    SECURITY_ARMED_TIER3
};

enum BATTERY_STATE {
    BATTERY_LOW,
    BATTERY_HIGH
};

enum BLE_COMMANDS {
    BLE_NO_COMMAND,
    BLE_ARM,
    BLE_DISARM,
    BLE_OOR
};

enum BELT_STATE {
    BELT_OPEN,
    BELT_CLOSED,
    BELT_UNKNOWN
};

/* Global Task Handles */
TaskHandle_t led_task_handle;
TaskHandle_t alarm_task_handle;

/* Global Queue Declarations */
QueueHandle_t led_queue;

/* Global Communication Enums */
enum SECURITY_STATE security_state = SECURITY_DISARMED;  // Owned by Security Core Task
enum BATTERY_STATE battery_state = BATTERY_HIGH;         // Owned by Battery Status Task
enum BLE_COMMANDS ble_command = BLE_NO_COMMAND;          // Owned by BLE Task
enum BELT_STATE belt_state = BELT_UNKNOWN;               // Owned by Belt Detection Task

/* Battery State Consts */
const int LOW_BATTERY_THRESHOLD = 20;

/* Notification Bit Consts */
const uint32_t ALARM_CHIRP_BIT = 1UL << 1;
const uint32_t SECURITY_BLE_BIT = 1UL << 1;
const uint32_t SECURITY_BELT_DETECTION_BIT = 1UL << 2;


void request_chirp()
{
    xTaskNotify(alarm_task_handle, ALARM_CHIRP_BIT, eSetBits);
}

void led_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

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
            // LOG: Unknown LED input combination
        }
    }
}

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

/* Private Security Core Functions */
static void security_disarm()
{

}

static void security_arm()
{

}

static void security_tier2()
{

}

static void security_tier3()
{

}

static void turn_alarm_off()
{

}

static void arm_test()
{

}

void security_core_task(void *arg)
{
    /* Notification Bits */
    /* Bits 31-4 | Bit 2          | Bit 1 | Bit 0           */
    /* Unused    | Belt Detection | BLE   | General wake up */


    uint32_t notification_value;
    while (1) {
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);

        if (notification_value & SECURITY_BLE_BIT) {
            if (security_state == SECURITY_ARMED_QUIET || security_state == SECURITY_ARMED_TIER2 || security_state == SECURITY_ARMED_TIER3) {
                switch (ble_command) {
                    case BLE_ARM:
                        break;
                    case BLE_DISARM:
                        turn_alarm_off();
                        security_disarm();
                        break;
                    case BLE_OOR:
                        break;
                    default:
                        // LOG: Received unknown request from BLE Task
                }
            } else if (security_state == SECURITY_DISARMED) {
                switch (ble_command) {
                    case BLE_ARM:
                        arm_test();
                        security_arm();
                        break;
                    case BLE_DISARM:
                        break;
                    case BLE_OOR:
                        arm_test();
                        security_arm();
                        break;
                    default:
                        // LOG: Received unknown request from BLE Task
                }
            }
        }

        if (notification_value & SECURITY_BELT_DETECTION_BIT) {
            if (security_state == SECURITY_DISARMED) {
                switch (belt_state) {
                    case BELT_OPEN:
                        break;
                    case BELT_CLOSED:
                        break;
                    default:
                        // LOG: Received unknown request from Belt Detection Task
                }
            } else if (security_state == SECURITY_ARMED_QUIET || security_state == SECURITY_ARMED_TIER2) {
                switch (belt_state) {
                    case BELT_OPEN:
                        security_tier3();
                        break;
                    case BELT_CLOSED:
                        break;
                    default:
                        // LOG: Received unknown request from Belt Detection Task
                }
            } else if (security_state == SECURITY_ARMED_TIER3) {
                // Do nothing
            }
        }
    }
}

void app_main(void)
{
    /* LED Task Setup */
    const int LED_QUEUE_LEN = 10;
    const enum LED_QUEUE_INPUTS sample_led_queue_input = ARM;
    led_queue = xQueueCreate(LED_QUEUE_LEN, sizeof(sample_led_queue_input));

    gpio_reset_pin(BUILTIN_LED_GPIO);
    gpio_set_direction(BUILTIN_LED_GPIO, GPIO_MODE_OUTPUT);

    printf("Starting dual-core FreeRTOS QuickLock FW on %s (%d cores)\n",
           CONFIG_IDF_TARGET, CONFIG_FREERTOS_NUMBER_OF_CORES);

    xTaskCreatePinnedToCore(led_task,           /* task function            */
                            "led_task",         /* task name                */
                            1024,               /* stack depth in words     */
                            NULL,               /* argument passed to task  */
                            5,                  /* priority                 */
                            &led_task_handle,   /* task handle (unused)     */
                            0);                 /* core to pin to: PRO_CPU  */
    
    xTaskCreatePinnedToCore(sample_task,           /* task function            */
                            "sample_task",         /* task name                */
                            1024,               /* stack depth in words     */
                            NULL,               /* argument passed to task  */
                            3,                  /* priority                 */
                            NULL,   /* task handle (unused)     */
                            0);                 /* core to pin to: PRO_CPU  */

    /* app_main() returns here; the two tasks keep running on their cores. */
}
