/*
 * QuickLock harness firmware entry point.
 *
 * The ESP32 has two CPU cores (core 0 = PRO_CPU, core 1 = APP_CPU). ESP-IDF
 * ships a version of FreeRTOS with Symmetric Multiprocessing (SMP) support, so
 * a task can be pinned to a specific core with xTaskCreatePinnedToCore().
 *
 * app_main() creates the application tasks (each defined in its own component)
 * and then returns; the tasks keep running on their cores.
 */

#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "globals.h"
#include "led_task.h"
#include "alarm_task.h"
#include "battery_status_task.h"
#include "security_core_task.h"

void app_main(void)
{
    printf("Starting dual-core FreeRTOS QuickLock FW on %s (%d cores)\n",
           CONFIG_IDF_TARGET, CONFIG_FREERTOS_NUMBER_OF_CORES);

    xTaskCreatePinnedToCore(led_task,           /* task function            */
                            "led_task",         /* task name                */
                            1024,               /* stack depth in bytes     */
                            NULL,               /* argument passed to task  */
                            2,                  /* priority                 */
                            &led_task_handle,   /* task handle              */
                            0);                 /* core to pin to: PRO_CPU  */

    xTaskCreatePinnedToCore(alarm_task,
                            "alarm_task",
                            1024,
                            NULL,
                            4,
                            &alarm_task_handle,
                            0);

    xTaskCreatePinnedToCore(battery_status_task,
                            "battery_status_task",
                            1024,
                            NULL,
                            1,
                            &battery_status_task_handle,
                            0);

    xTaskCreatePinnedToCore(security_core_task,
                            "security_core_task",
                            1024,
                            NULL,
                            5,
                            &security_core_task_handle,
                            0);

    /* app_main() returns here; the tasks keep running on their cores. */
}
