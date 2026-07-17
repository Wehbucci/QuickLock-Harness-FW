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

/* ------------------------------------------------------------------ */
/* Notification bit constants                                         */
/* ------------------------------------------------------------------ */

extern const uint32_t ALARM_CHIRP_BIT;
extern const uint32_t SECURITY_BLE_BIT;
extern const uint32_t SECURITY_BELT_DETECTION_BIT;

/* ------------------------------------------------------------------ */
/* Global helper functions                                            */
/* ------------------------------------------------------------------ */

/* Notify the alarm task to emit a short chirp. Safe to call from any task. */
void request_chirp(void);
