/*
 * battery_status_task.h
 *
 * Public interface for the battery status task. The task polls the battery
 * level and updates the shared battery_state, notifying the LED and alarm
 * tasks on transitions.
 */

#pragma once

void battery_status_task(void *arg);
