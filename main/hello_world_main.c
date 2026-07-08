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

    /* app_main() returns here; the two tasks keep running on their cores. */
}
