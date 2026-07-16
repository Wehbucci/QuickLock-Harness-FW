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

/* Each task prints "Hello world" once per second, forever. */
static void hello_task(void *arg)
{
    const char *name = (const char *)arg;
    uint32_t count = 0;

    for (;;) {
        printf("Hello world from %s running on core %d (count %" PRIu32 ")\n",
               name, xPortGetCoreID(), count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// enum LED_QUEUE_INPUTS {
//     ARM,
//     DISARM,
//     LOW_BATTERY,
//     HIGH_BATTERY
// };

enum SECURITY_STATE {
    SECURITY_ARMED,
    SECURITY_DISARMED
};

enum BATTERY_STATE {
    BATTERY_LOW,
    BATTERY_HIGH
};

enum ALARM_STATE {
    ALARM_OFF,
    ALARM_HALF,
    ALARM_FULL
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
enum ALARM_STATE alarm_state = ALARM_OFF;                // Owned by Security Core Task
enum BATTERY_STATE battery_state = BATTERY_HIGH;         // Owned by Battery Status Task
enum BLE_COMMANDS ble_command = BLE_NO_COMMAND;          // Owned by BLE Task
enum BELT_STATE belt_state = BELT_UNKNOWN;               // Owned by Belt Detection Task

/* Battery State Consts */
const int LOW_BATTERY_THRESHOLD = 20;

/* Alarm Consts */
const uint32_t ALARM_CHIRP_BIT = 1UL << 1;

/* Security Core Consts */
const uint32_t SECURITY_BLE_BIT = 1UL << 1;
const uint32_t SECURITY_BELT_DETECTION_BIT = 1UL << 2;


// void led_task(void *arg)
// {
//     enum LED_QUEUE_INPUTS battery_state = HIGH_BATTERY;
//     enum LED_QUEUE_INPUTS arm_disarm_state = ARM;

//     enum LED_QUEUE_INPUTS queue_value;
//     while (1) {
//         xQueueReceive(led_queue, &queue_value, portMAX_DELAY);
//         gpio_set_level(BUILTIN_LED_GPIO, 0);  // turn builtin LED on
//         vTaskDelay(pdMS_TO_TICKS(500));       // 500ms delay
//         gpio_set_level(BUILTIN_LED_GPIO, 0);  // turn builtin LED off
//     }
// }

void request_chirp()
{
    xTaskNotify(alarm_task_handle, CHIRP_BIT, eSetBits);
}

void led_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (security_state == SECURITY_ARMED && battery_state == BATTERY_HIGH) {
            gpio_set_level(BUILTIN_LED_GPIO, 1);  // turn builtin LED on
        } else if (security_state == SECURITY_ARMED && battery_state == BATTERY_LOW) {
            gpio_set_level(BUILTIN_LED_GPIO, 1);  // turn builtin LED on
            // start a timer to blink on for 500ms every 1000ms
        } else if (security_state == SECURITY_DISARMED && battery_state == BATTERY_HIGH) {
            gpio_set_level(BUILTIN_LED_GPIO, 0);  // turn builtin LED off
        } else {
            gpio_set_level(BUILTIN_LED_GPIO, 1);  // turn builtin LED on
            // start a timer to blink on for 500ms every 5000ms
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

        if (alarm_state == ALARM_HALF) {
            // TODO: drive alarm half
        } else if (alarm_state == ALARM_FULL) {
            // TODO: drive alarm full
        } else {
            // TODO: drive alarm off
        }

        if (chirp_requested && alarm_state == ALARM_OFF) {
            // TODO: chirp the alarm
        }
    }
}

/* Private Security Core Functions */
static void disarm()
{

}

static void arm()
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
            if (security_state == SECURITY_ARMED) {
                switch (ble_command) {
                    case BLE_ARM:
                        break;
                    case BLE_DISARM:
                        turn_alarm_off();
                        disarm();
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
                        arm();
                        break;
                    case BLE_DISARM:
                        break;
                    case BLE_OOR:
                        arm_test();
                        arm();
                        break;
                    default:
                        // LOG: Received unknown request from BLE Task
                }
            }
        }

        if (notification_value & SECURITY_BELT_DETECTION_BIT) {
            if (security_state == SECURITY_ARMED) {
                switch (belt_state) {
                    case BELT_OPEN:
                        break;
                    case BELT_CLOSED:
                        break;
                    default:
                        // LOG: Received unknown request from Belt Detection Task
                }
            } else if (security_state == SECURITY_DISARMED) {
                switch (belt_state) {
                    case BELT_OPEN:
                        break;
                    case BELT_CLOSED:
                        break;
                    default:
                        // LOG: Received unknown request from Belt Detection Task
                }
            }
        }
    }
}

// void sample_task(void *arg)
// {   
//     enum LED_QUEUE_INPUTS send_value = ARM;
//     while (1) {
//         xQueueSend(led_queue, &send_value, 0);
//         vTaskDelay(pdMS_TO_TICKS(5000));  // 5s delay
//     }
// }

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
