/*
 * security_core_task.h
 *
 * Public interface for the security core task. The task is the owner of
 * security_state and drives arm/disarm/tier transitions in response to BLE
 * commands and belt-detection events.
 */

#pragma once

void security_core_task(void *arg);
