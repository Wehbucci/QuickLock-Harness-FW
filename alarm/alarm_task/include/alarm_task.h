/*
 * alarm_task.h
 *
 * Public interface for the alarm task. The task drives the alarm output based
 * on the current security state and services chirp requests. To request a
 * chirp from another task, call request_chirp() (declared in globals.h).
 */

#pragma once

void alarm_task(void *arg);
