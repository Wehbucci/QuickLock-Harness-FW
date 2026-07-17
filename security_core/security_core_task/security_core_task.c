/*
 * security_core_task.c
 *
 * Owns security_state. Reacts to BLE commands and belt-detection notifications
 * to arm, disarm, or escalate the alarm tier.
 */

#include "security_core_task.h"
#include "globals.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

/* Private Security Core Functions */
static void security_disarm(void)
{

}

static void security_arm(void)
{

}

static void security_tier2(void)
{

}

static void security_tier3(void)
{

}

static void turn_alarm_off(void)
{

}

static void arm_test(void)
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
