/*
 * belt_detection_task.h
 *
 * Public interface for the belt detection task. Monitors the belt-loop
 * circuit via GPIO39 (VN) and notifies the security core task when the
 * loop opens while armed. The interrupt is enabled/disabled by the
 * security core task on arm/disarm.
 */

#pragma once

void belt_detection_init(void);
void belt_detection_enable_interrupt(void);
void belt_detection_disable_interrupt(void);