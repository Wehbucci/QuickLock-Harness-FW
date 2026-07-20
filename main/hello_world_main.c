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

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "imu_hal.h"
#include "imu_detection.h"

/* Dummy placeholders until the tasks that really own these exist:
 * g_armed will be written by the Security Core / BLE task (arm/disarm
 * commands), g_buckle_open by the Belt Detection task (Section 3.2.2).
 * Defaulting g_armed to true keeps the IMU actively sampling for bench
 * testing; flip it to exercise the disarmed/idle path. */
volatile bool g_armed = true;
volatile bool g_buckle_open = false;

/* Each task prints "Hello world" once per second, forever. */
static void hello_task(void *arg)
{
    const char *name = (const char *)arg;
    uint32_t count = 0;
    (void)name;
    (void)count;

    for (;;) {
        // printf("Hello world from %s running on core %d (count %" PRIu32 ")\n",
        //        name, xPortGetCoreID(), count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    printf("Starting dual-core FreeRTOS hello world on %s (%d cores)\n",
           CONFIG_IDF_TARGET, CONFIG_FREERTOS_NUMBER_OF_CORES);

    /* Pin one task to core 0 (PRO_CPU) ... */
    xTaskCreatePinnedToCore(hello_task,      /* task function            */
                            "hello_core0",   /* task name                */
                            2048,            /* stack depth in words     */
                            "task A",        /* argument passed to task  */
                            5,               /* priority                 */
                            NULL,            /* task handle (unused)     */
                            0);              /* core to pin to: PRO_CPU  */

    /* ... and another to core 1 (APP_CPU). */
    xTaskCreatePinnedToCore(hello_task,
                            "hello_core1",
                            2048,
                            "task B",
                            5,
                            NULL,
                            1);              /* core to pin to: APP_CPU  */

    ESP_ERROR_CHECK(imu_hal_init());

    /* IMU task: priority 7 (highest), core 1, per Table 4. Stack is larger
     * than it looks like it should need -- two 256-float ring buffers (2KB),
     * I2C driver call depth, and printf/ESP_LOGW formatting several floats
     * through newlib add up faster than expected; 4096 overflowed under
     * sustained load. */
    xTaskCreatePinnedToCore(imu_detection_task,
                            "imu_detection",
                            8192,
                            NULL,
                            7,
                            NULL,
                            1);
}
