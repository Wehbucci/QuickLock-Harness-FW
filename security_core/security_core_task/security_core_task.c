/*
 * security_core_task.c
 *
 * Owns security_state. Reacts to BLE commands and belt-detection notifications
 * to arm, disarm, or escalate the alarm tier.
 */

#include "security_core_task.h"
#include "globals.h"
#include "ql_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

QL_LOG_TAG("security_core");

/* Private Security Core Functions */
static void security_disarm(void)
{   
    enum SECURITY_STATE prev_security_state = security_state;
    security_state = SECURITY_DISARMED;

    if (prev_security_state == SECURITY_ARMED_TIER2 || prev_security_state == SECURITY_ARMED_TIER3) {
        wake_up_alarm_task();
    }

    if (prev_security_state == SECURITY_ARMED_QUIET || prev_security_state == SECURITY_ARMED_TIER2 || prev_security_state == SECURITY_ARMED_TIER3) {
        wake_up_led_task();
    }
}

static void security_arm(void)
{
    enum SECURITY_STATE prev_security_state = security_state;

    /* Only move to armed quiet state if not already armed (do not silence an active alarm) */
    if (prev_security_state == SECURITY_DISARMED) {
        security_state = SECURITY_ARMED_QUIET;
        // TODO: Wake up Belt Detection task
        wake_up_imu_task();
        request_chirp();
        wake_up_led_task();
    }

}

static void security_tier2(void)
{
    enum SECURITY_STATE prev_security_state = security_state;
    security_state = SECURITY_ARMED_TIER2;

    if (prev_security_state == SECURITY_DISARMED) {
        QL_LOGE("tier2 escalation from DISARMED; unreachable state transition");
    } else if (prev_security_state == SECURITY_ARMED_QUIET || prev_security_state == SECURITY_ARMED_TIER3) {
        wake_up_alarm_task();
    }
}

static void security_tier3(void)
{
    enum SECURITY_STATE prev_security_state = security_state;
    security_state = SECURITY_ARMED_TIER3;

    if (prev_security_state == SECURITY_DISARMED) {
        QL_LOGE("tier3 escalation from DISARMED; unreachable state transition");
    } else if (prev_security_state == SECURITY_ARMED_QUIET || prev_security_state == SECURITY_ARMED_TIER2) {
        wake_up_alarm_task();
    }
}

static void arm_test(void)
{
    // TODO
}

void security_core_task(void *arg)
{
    QL_LOGI("task started on core %d", xPortGetCoreID());

    /* Notification Bits */
    /* Bits 31-4 | Bit 3 | Bit 2          | Bit 1 | Bit 0           */
    /* Unused    | IMU   | Belt Detection | BLE   | General wake up */


    uint32_t notification_value;
    while (1) {
        QL_LOGI("sleeping; current state=%d", (int)security_state);
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        QL_LOGI("woke up; notification=0x%08x", (unsigned)notification_value);

        if (notification_value & SECURITY_BLE_BIT) {
            if (security_state == SECURITY_ARMED_QUIET || security_state == SECURITY_ARMED_TIER2 || security_state == SECURITY_ARMED_TIER3) {
                switch (ble_command) {
                    case BLE_ARM:
                        break;
                    case BLE_DISARM:
                        security_disarm();
                        break;
                    case BLE_OOR:
                        break;
                    default:
                        QL_LOGW("unknown ble_command %d while armed", (int)ble_command);
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
                        QL_LOGW("unknown ble_command %d while disarmed", (int)ble_command);
                }
            }
        }

        if (notification_value & SECURITY_BELT_DETECTION_BIT) {
            if (security_state == SECURITY_DISARMED) {
                QL_LOGW("belt_state %d received while disarmed", (int)belt_state);
            } else if (security_state == SECURITY_ARMED_QUIET || security_state == SECURITY_ARMED_TIER2) {
                switch (belt_state) {
                    case BELT_OPEN:
                        security_tier3();
                        break;
                    case BELT_CLOSED:
                        break;
                    default:
                        QL_LOGW("unknown belt_state %d while armed", (int)belt_state);
                }
            } else if (security_state == SECURITY_ARMED_TIER3) {
                switch (belt_state) {
                    case BELT_OPEN:
                        // TODO: Reset 20s timer (if belt just opened)
                        // TODO: we may need to change the BELT->SC communication to command based instead of state based
                        break;
                    case BELT_CLOSED:
                        break;
                    default:
                        QL_LOGW("unknown belt_state %d while armed in tier3", (int)belt_state);
                }
            }
        }

        if (notification_value & SECURITY_IMU_BIT) {
            if (security_state == SECURITY_DISARMED) {
                QL_LOGW("imu_command %d received while disarmed", (int)ble_command);
            } else if (security_state == SECURITY_ARMED_QUIET) {
                switch (imu_command) {
                    case IMU_QUIET_TO_TIER2:
                        security_tier2();
                        break;
                    case IMU_QUIET_TO_TIER3:
                        security_tier3();
                        break;
                    case IMU_TIER2_TO_TIER3:
                        QL_LOGI("ignoring imu_command %d received while armed and quiet", (int)ble_command);
                        break;
                    case IMU_TIER2_MOVEMENT_SUSTAINED:
                        QL_LOGI("ignoring imu_command %d received while armed and quiet", (int)ble_command);
                        break;
                    case IMU_TIER3_MOVEMENT_DETECTED:
                        QL_LOGI("ignoring imu_command %d received while armed and quiet", (int)ble_command);
                        break;
                    default:
                        QL_LOGW("unknown imu_command %d while disarmed", (int)ble_command);
                }
            } else if (security_state == SECURITY_ARMED_TIER2) {
                switch (imu_command) {
                    case IMU_QUIET_TO_TIER2:
                        QL_LOGI("ignoring imu_command %d received while armed in tier2", (int)ble_command);
                        break;
                    case IMU_QUIET_TO_TIER3:
                        QL_LOGI("ignoring imu_command %d received while armed in tier2", (int)ble_command);
                        break;
                    case IMU_TIER2_TO_TIER3:
                        security_tier3();
                        break;
                    case IMU_TIER2_MOVEMENT_SUSTAINED:
                        // TODO: set flag so that at the end of 5s timer we go to QUIET state. Make sure this flag is unset when the timer is started.
                        break;
                    case IMU_TIER3_MOVEMENT_DETECTED:
                        QL_LOGI("ignoring imu_command %d received while armed in tier2", (int)ble_command);
                        break;
                    default:
                        QL_LOGW("unknown imu_command %d while armed in tier2", (int)ble_command);
                }
            } else if (security_state == SECURITY_ARMED_TIER3) {
                switch (imu_command) {
                    case IMU_QUIET_TO_TIER2:
                        QL_LOGI("ignoring imu_command %d received while armed in tier3", (int)ble_command);
                        break;
                    case IMU_QUIET_TO_TIER3:
                        QL_LOGI("ignoring imu_command %d received while armed in tier3", (int)ble_command);
                        break;
                    case IMU_TIER2_TO_TIER3:
                        QL_LOGI("ignoring imu_command %d received while armed in tier3", (int)ble_command);
                        break;
                    case IMU_TIER2_MOVEMENT_SUSTAINED:
                        QL_LOGI("ignoring imu_command %d received while armed in tier3", (int)ble_command);
                        break;
                    case IMU_TIER3_MOVEMENT_DETECTED:
                        // TODO: Reset 20s timer
                        break;
                    default:
                        QL_LOGW("unknown imu_command %d while armed in tier3", (int)ble_command);
                }
            }
        }
    }
}
