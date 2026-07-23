/*
 * security_core_task.c
 *
 * Owns security_state. Reacts to BLE commands and belt-detection notifications
 * to arm, disarm, or escalate the alarm tier.
 */

#include "security_core_task.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "globals.h"
#include "ql_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

QL_LOG_TAG("security_core");

/* Variables */
bool tier2_movement_sustained = false;

/* Timers Setup */
static TimerHandle_t xGracePeriodTimer;
static TimerHandle_t xTier3Timer;

static void vGraceTimerCallback(TimerHandle_t xTimer)
{
    xTaskNotify(security_core_task_handle, SECURITY_GRACE_TIMER_BIT, eSetBits);
}

static void vTier3TimerCallback(TimerHandle_t xTimer)
{
    xTaskNotify(security_core_task_handle, SECURITY_TIER3_TIMER_BIT, eSetBits);
}


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

    /* Disarm cancels any pending tier escalation timers */
    if (xTimerStop(xGracePeriodTimer, 0) != pdPASS) {
        QL_LOGW("failed to stop grace period timer");
    }
    if (xTimerStop(xTier3Timer, 0) != pdPASS) {
        QL_LOGW("failed to stop tier3 timer");
    }
}

static void security_armed_quiet(void)
{
    enum SECURITY_STATE prev_security_state = security_state;
    security_state = SECURITY_ARMED_QUIET;

    if (prev_security_state == SECURITY_DISARMED) {
        // TODO: Wake up Belt Detection task
        wake_up_imu_task();
        request_chirp();
        wake_up_led_task();
    }

    wake_up_alarm_task();
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

    /* Unconditional Timer Reset */
    if (xTimerReset(xGracePeriodTimer, 0) != pdPASS) {
        QL_LOGW("failed to reset grace period timer");
    }
    tier2_movement_sustained = false;
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

    /* Unconditional Timer Reset */
    if (xTimerReset(xTier3Timer, 0) != pdPASS) {
        QL_LOGW("failed to reset tier3 timer");
    }
}

static void arm_test(void)
{
    // TODO
}

void security_core_task(void *arg)
{
    QL_LOGI("task started on core %d", xPortGetCoreID());

    /* Timer Setup */
    xGracePeriodTimer = xTimerCreate(
        "Grace",                 // name (debug only)
        pdMS_TO_TICKS(5000),     // period
        pdFALSE,                 // pdFALSE = one-shot, pdTRUE = auto-reload
        (void *)0,               // timer ID — stash context here if you want
        vGraceTimerCallback);

    configASSERT(xGracePeriodTimer != NULL);

    xTier3Timer = xTimerCreate(
        "Grace",                 // name (debug only)
        pdMS_TO_TICKS(20000),     // period
        pdFALSE,                 // pdFALSE = one-shot, pdTRUE = auto-reload
        (void *)0,               // timer ID — stash context here if you want
        vTier3TimerCallback);

    configASSERT(xTier3Timer != NULL);


    /* Notification Bits */
    /* Bits 31-6 | Bit 5        | Bit 4              | Bit 3 | Bit 2          | Bit 1 | Bit 0           */
    /* Unused    | Tier3 Timer  | Grace Period Timer | IMU   | Belt Detection | BLE   | General wake up */

    uint32_t notification_value;
    while (1) {
        QL_LOGI("sleeping; current state=%d", (int)security_state);
        xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY);
        QL_LOGI("woke up; notification=0x%08x", (unsigned)notification_value);

        /* General Wake Up */
        if (notification_value & 1UL) {
            QL_LOGW("unhandled general wake up notification");
        }

        /* BLE Command */
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
                        security_armed_quiet();
                        break;
                    case BLE_DISARM:
                        break;
                    case BLE_OOR:
                        arm_test();
                        security_armed_quiet();
                        break;
                    default:
                        QL_LOGW("unknown ble_command %d while disarmed", (int)ble_command);
                }
            }
        }

        /* Belt Detection Command */
        // TODO: we may need to change the BELT->SC communication to command based instead of state based
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
                        security_tier3();
                        break;
                    case BELT_CLOSED:
                        break;
                    default:
                        QL_LOGW("unknown belt_state %d while armed in tier3", (int)belt_state);
                }
            }
        }

        /* IMU Command */
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
                        tier2_movement_sustained = true;
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
                        security_tier3();
                        break;
                    default:
                        QL_LOGW("unknown imu_command %d while armed in tier3", (int)ble_command);
                }
            }
        }

        /* Grace Timer Elapsed */
        if (notification_value & SECURITY_GRACE_TIMER_BIT) {
            if (security_state == SECURITY_DISARMED || security_state == SECURITY_ARMED_QUIET || security_state == SECURITY_ARMED_TIER3) {
                QL_LOGD("grace timer elapsed; ignored in state %d", (int)security_state);
            } else if (security_state == SECURITY_ARMED_TIER2) {
                QL_LOGD("grace timer elapsed in tier2; tier2_movement_sustained=%d", (int)tier2_movement_sustained);
                if (tier2_movement_sustained) {
                    security_tier3();
                } else {
                    security_armed_quiet();
                }
            }
        }

        /* Tier3 Timer Elapsed */
        if (notification_value & SECURITY_TIER3_TIMER_BIT) {
            if (security_state == SECURITY_DISARMED || security_state == SECURITY_ARMED_QUIET || security_state == SECURITY_ARMED_TIER2) {
                QL_LOGD("tier3 timer elapsed; ignored in state %d", (int)security_state);
            } else if (security_state == SECURITY_ARMED_TIER3) {
                QL_LOGD("tier3 timer elapsed in tier3; returning to armed quiet");
                security_armed_quiet();
            }
        }
    }
}
