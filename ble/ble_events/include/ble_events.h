/*
 * ble_events.h — decoupled event interface from the BLE task to the Security task.
 *
 * Purpose: the BLE Communication task never calls the Security Core task
 * directly. Instead it POSTS typed events onto a FreeRTOS queue and the Security
 * task drains them (HARNESS_BLE_TASK.md sections 3, 4, 11). This queue + enum is
 * the integration contract between the two subsystems, so the BLE task is fully
 * testable in isolation today and Security can attach later with no change here.
 *
 * For this first version a STUB consumer task drains the queue and logs each
 * event; the real Security task replaces it at the TODO below.
 *
 * Serves: F13, F14, F15, F22 (these decisions are surfaced as events).
 */

#ifndef QUICKLOCK_BLE_EVENTS_H
#define QUICKLOCK_BLE_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Events the BLE task raises for the Security task (HARNESS_BLE_TASK.md sec 4). */
typedef enum {
    BLE_EVT_FOB_CONNECTED,         /* link up, bonded, identity-verified */
    BLE_EVT_FOB_DISCONNECTED,      /* link down; detail = raw disconnect reason */
    BLE_EVT_ARM_REQUESTED,         /* Command 0x01 from the fob */
    BLE_EVT_DISARM_REQUESTED,      /* Command 0x02 from the fob */
    BLE_EVT_SILENCE_REQUESTED,     /* Command 0x03 from the fob */
    BLE_EVT_FOB_OUT_OF_RANGE,      /* Mechanism B soft trigger; detail = filt RSSI */
    BLE_EVT_FOB_IN_RANGE,          /* Mechanism B recovery;   detail = filt RSSI */
    BLE_EVT_LINK_LOST_SUPERVISION, /* Mechanism A hard trigger -> auto-arm (F22) */
    BLE_EVT_PAIRING_SUCCEEDED,     /* new bond established in the pairing window */
    BLE_EVT_PAIRING_FAILED,        /* pairing/encryption failed; detail = reason */
    BLE_EVT_IDENTITY_REJECTED,     /* peer failed the Identity token check (F15) */
} ble_event_kind_t;

/* One event on the wire between BLE and Security. 'detail' is kind-dependent:
 * a disconnect reason, a filtered RSSI, a pairing status code, etc. */
typedef struct {
    ble_event_kind_t kind;
    int32_t          detail;
} ble_event_t;

/*
 * ble_events_init — create the outbound event queue.
 * Returns ESP_OK, or ESP_ERR_NO_MEM if the queue could not be allocated.
 * Must be called once (from app_main) before any task posts or consumes.
 */
esp_err_t ble_events_init(void);

/* Handle of the outbound queue, so the future Security task can receive on it. */
QueueHandle_t ble_events_queue(void);

/*
 * ble_events_post — post one event to the Security queue (called by the BLE task).
 * Non-blocking (zero timeout): the BLE task must never stall on a full Security
 * queue. Returns true if enqueued, false if the queue was full (logged by caller).
 * Safe to call from task context; not intended for ISRs.
 */
bool ble_events_post(ble_event_kind_t kind, int32_t detail);

/* Human-readable name for a kind, for logging. Never NULL. */
const char *ble_event_name(ble_event_kind_t kind);

/*
 * ble_events_start_stub_consumer — spawn the placeholder consumer task that
 * drains the queue and logs each event with a timestamp.
 *
 * TODO(security-core): delete this stub and have the real Security task receive
 * on ble_events_queue() instead. The queue + ble_event_t are the contract; the
 * BLE task does not change when that swap happens.
 */
void ble_events_start_stub_consumer(void);

#endif /* QUICKLOCK_BLE_EVENTS_H */
