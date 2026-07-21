/*
 * globals.h
 *
 * Cross-task shared types and state for the QuickLock harness firmware.
 *
 * This header only *declares* the shared globals (with the `extern` keyword);
 * every variable is *defined* exactly once in globals.c. The enum types below
 * are pure type definitions and are safe to include from any translation unit.
 */

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ------------------------------------------------------------------ */
/* Communication enums (types shared across tasks)                    */
/* ------------------------------------------------------------------ */

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

enum IMU_STATE {
    IMU_QUIET,
    IMU_TIER2_GRACE,
    IMU_TIER3_ALARM
};

/* ------------------------------------------------------------------ */
/* Global task handles (assigned in main.c when tasks are created)    */
/* ------------------------------------------------------------------ */

extern TaskHandle_t led_task_handle;
extern TaskHandle_t alarm_task_handle;
extern TaskHandle_t battery_status_task_handle;
extern TaskHandle_t security_core_task_handle;

/* ------------------------------------------------------------------ */
/* Global communication state                                         */
/* ------------------------------------------------------------------ */

extern enum SECURITY_STATE security_state;   /* Owned by Security Core Task   */
extern enum BATTERY_STATE  battery_state;    /* Owned by Battery Status Task  */
extern enum BLE_COMMANDS   ble_command;      /* Owned by BLE Task             */
extern enum BELT_STATE     belt_state;       /* Owned by Belt Detection Task  */

/*
 * imu_state is the IMU Detection task's own detection-tier output -- a plain
 * `volatile` global rather than a mutex-protected one, since it has exactly
 * one writer (imu_detection_task) and single-word reads/writes are atomic on
 * the ESP32. It's distinct from `security_state` above (owned by Security
 * Core): imu_detection_task reads security_state to decide whether to sample
 * at all -- reconciling the two is Security Core's job, not implemented yet.
 */
extern volatile enum IMU_STATE imu_state;    /* Owned by IMU Detection Task   */

/* ------------------------------------------------------------------ */
/* Notification bit constants                                         */
/* ------------------------------------------------------------------ */

extern const uint32_t ALARM_WAKE_BIT;
extern const uint32_t ALARM_CHIRP_BIT;
extern const uint32_t SECURITY_BLE_BIT;
extern const uint32_t SECURITY_BELT_DETECTION_BIT;
extern const uint32_t SECURITY_IMU_BIT;

/* ------------------------------------------------------------------ */
/* Global communication functions                                     */
/*   Should only be called from tasks designated to communicate with  */
/*   the target task.                                                 */
/* ------------------------------------------------------------------ */

void request_chirp(void);
void wake_up_alarm_task(void);
void wake_up_led_task(void);
void ble_wake_up_security_task(void);
void belt_detection_wake_up_security_task(void);
void imu_wake_up_security_task(void);
