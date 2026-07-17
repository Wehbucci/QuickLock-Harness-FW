/*
 * led_task.h
 *
 * Public interface for the LED task. The task drives the on-board status LED
 * based on the shared security and battery state.
 */

#pragma once

void led_task(void *arg);
