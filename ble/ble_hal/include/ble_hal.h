/*
 * ble_hal.h — stack-agnostic Hardware Abstraction Layer for the BLE central.
 *
 * Purpose: wrap every NimBLE-specific call behind a small, clearly named C
 * interface so the application logic (ble_task) never touches the stack. All
 * ble_gap_* / ble_gattc_* / ble_hs_* / ble_store_* usage lives in the single
 * translation unit ble_hal.c. If the IDF or NimBLE version changes, only
 * ble_hal.c changes; the state machine above it is untouched
 * (HARNESS_BLE_TASK.md section 8).
 *
 * The invariant that makes this worth doing:
 *     grep -r "nimble/" components/   ->   matches only ble_hal.c
 *
 * Contract with the app layer: NimBLE delivers GAP/GATT events on the NimBLE
 * HOST task. The HAL's callbacks do the minimum — translate each event into a
 * small ble_inbound_msg_t and POST it to the sink queue (set via
 * ble_hal_set_event_sink). That post wakes the BLE task, which owns the state
 * machine. The HAL never calls back into the state machine directly.
 *
 * Serves: F13, F15, F17, F18, F21, F22, F23, F24, F36.
 */

#ifndef QUICKLOCK_BLE_HAL_H
#define QUICKLOCK_BLE_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Max bytes carried in an inbound message payload (Command opcode, Identity
 * token, etc.). 20 covers a default-MTU ATT payload, plenty for our 1/16-byte
 * values. */
#define BLE_INBOUND_PAYLOAD_MAX 20

#define BLE_HAL_ADDR_LEN 6

/*
 * A BLE device address, mirrored from NimBLE's ble_addr_t but kept free of any
 * stack type so ble_task can hold and pass it back to ble_hal_connect() without
 * including a NimBLE header. Byte order matches NimBLE (val[0] is the LSB).
 */
typedef struct {
    uint8_t type;                     /* NimBLE addr type (public/random) */
    uint8_t val[BLE_HAL_ADDR_LEN];
} ble_hal_addr_t;

/*
 * Kinds of inbound message the HAL posts to the app task. Each corresponds to a
 * NimBLE event or the completion of an async GATT procedure, already reduced to
 * what the state machine cares about.
 */
typedef enum {
    BLE_IN_SCAN_MATCH,    /* an advertiser carrying the QuickLock service UUID */
    BLE_IN_CONNECTED,     /* connection established/failed; .status is the code */
    BLE_IN_DISCONNECTED,  /* link down; .status is the raw NimBLE reason */
    BLE_IN_ENC_CHANGED,   /* encryption result; .status is the code (0 == up) */
    BLE_IN_DISC_DONE,     /* service+char+CCCD discovery finished OK */
    BLE_IN_DISC_FAILED,   /* discovery failed; .status is the code */
    BLE_IN_IDENTITY_READ, /* Identity read completed; payload holds the token */
    BLE_IN_SUBSCRIBED,    /* CCCD write completed; .status is the code */
    BLE_IN_WRITE_DONE,    /* state write-back completed; .status is the code */
    BLE_IN_NOTIFY_RX,     /* Command notification; payload holds the opcode(s) */
    BLE_IN_CONN_UPDATED,  /* connection params updated; .status is the code */
} ble_inbound_kind_t;

/*
 * The message NimBLE callbacks post to our task. Small and copyable by value so
 * it rides the FreeRTOS queue without heap ownership questions.
 */
typedef struct {
    ble_inbound_kind_t kind;
    uint16_t conn_handle;
    int      status;                          /* NimBLE reason/status, kind-dependent */
    ble_hal_addr_t addr;                       /* valid for BLE_IN_SCAN_MATCH */
    int8_t   rssi;                             /* valid for BLE_IN_SCAN_MATCH */
    uint8_t  payload[BLE_INBOUND_PAYLOAD_MAX]; /* NOTIFY_RX / IDENTITY_READ data */
    uint8_t  payload_len;
} ble_inbound_msg_t;

/* ----------------------------- lifecycle -------------------------------- */

/*
 * ble_hal_init — bring up NVS + NimBLE host, configure the Security Manager for
 * LE Secure Connections + bonding + Just Works, wire NVS bond persistence, and
 * start the NimBLE host task. Returns after init is requested; the stack becomes
 * usable asynchronously — poll ble_hal_is_ready() or wait for the first
 * BLE_IN_* message. Returns ESP_OK or an esp_err_t on failure.
 * Side effects: creates the NimBLE host FreeRTOS task (pinned per menuconfig).
 */
esp_err_t ble_hal_init(void);

/*
 * ble_hal_set_event_sink — register the queue that HAL callbacks post
 * ble_inbound_msg_t into. Must be called before scanning so no early event is
 * dropped. Call once from the BLE task at startup.
 */
void ble_hal_set_event_sink(QueueHandle_t sink);

/* True once the NimBLE host has synced and an identity address is set. */
bool ble_hal_is_ready(void);

/* ------------------------------- GAP ------------------------------------ */

/*
 * ble_hal_start_scan — begin passive discovery. The HAL filters advertisements
 * on the QuickLock service UUID and posts BLE_IN_SCAN_MATCH only for matches, so
 * the app never parses raw adv data. Returns a NimBLE status (0 == success).
 */
int ble_hal_start_scan(void);

/* Stop an in-progress scan. Returns a NimBLE status (0 == success). */
int ble_hal_stop_scan(void);

/*
 * ble_hal_connect — connect to a previously matched advertiser. Cancels scanning
 * first. On completion a BLE_IN_CONNECTED message is posted. Returns a NimBLE
 * status for the request itself (0 == accepted).
 */
int ble_hal_connect(const ble_hal_addr_t *peer);

/* Terminate a connection. Returns a NimBLE status (0 == success). */
int ble_hal_disconnect(uint16_t conn_handle);

/*
 * ble_hal_start_security — initiate pairing/encryption on a connection
 * (ble_gap_security_initiate). If a bond exists the link is simply encrypted
 * with the stored keys; otherwise a fresh LE Secure Connections + bonding is
 * performed (the app must only do this inside the pairing window). Result
 * arrives as BLE_IN_ENC_CHANGED. Returns a NimBLE status (0 == accepted).
 */
int ble_hal_start_security(uint16_t conn_handle);

/*
 * ble_hal_request_conn_params — ask the peer to adopt the BLE_CONTRACT.md
 * section 4 connection parameters (interval/latency/timeout). Result arrives as
 * BLE_IN_CONN_UPDATED. The peripheral MAY negotiate or ignore the request
 * (e.g. a phone mock often will). Returns a NimBLE status (0 == accepted).
 */
int ble_hal_request_conn_params(uint16_t conn_handle);

/*
 * ble_hal_reason_is_supervision_timeout — classify a raw disconnect reason (as
 * delivered in BLE_IN_DISCONNECTED.status) as an LE supervision timeout, the
 * hard link-loss trigger for Mechanism A (F22). Kept here so the NimBLE-specific
 * encoding BLE_HS_HCI_ERR(BLE_ERR_CONN_SPVN_TMO) never leaks into ble_task.
 */
bool ble_hal_reason_is_supervision_timeout(int reason);

/* ------------------------------- GATT ----------------------------------- */

/*
 * ble_hal_discover — discover the QuickLock service, its Command and Identity
 * characteristics, and the Command CCCD, caching their handles in the HAL. This
 * is an async multi-step chain; when it finishes the HAL posts BLE_IN_DISC_DONE
 * (or BLE_IN_DISC_FAILED). Returns a NimBLE status for kicking it off.
 */
int ble_hal_discover(uint16_t conn_handle);

/*
 * ble_hal_read_identity — read the Identity characteristic over the (encrypted)
 * link. Only meaningful after BLE_IN_ENC_CHANGED with status 0. Completion
 * arrives as BLE_IN_IDENTITY_READ with the token bytes in the payload. Returns a
 * NimBLE status (0 == accepted).
 */
int ble_hal_read_identity(uint16_t conn_handle);

/*
 * ble_hal_subscribe_command — enable Command notifications by writing
 * QL_CCCD_NOTIFY_ENABLE (0x0001) to the Command CCCD. Completion arrives as
 * BLE_IN_SUBSCRIBED. Returns a NimBLE status (0 == accepted), or a negative
 * value if the CCCD handle was never discovered.
 */
int ble_hal_subscribe_command(uint16_t conn_handle);

/*
 * ble_hal_write_state — write one confirmed-state byte (QL_STATE_*) back to the
 * fob's Command characteristic with response (F17). Completion arrives as
 * BLE_IN_WRITE_DONE. Returns a NimBLE status (0 == accepted).
 */
int ble_hal_write_state(uint16_t conn_handle, uint8_t state_byte);

/*
 * ble_hal_get_rssi — read the current connection RSSI (ble_gap_conn_rssi).
 * Writes the value to *out_rssi (dBm; NimBLE yields 127 if unavailable).
 * Returns a NimBLE status (0 == success). Called from housekeeping at ~1 Hz.
 */
int ble_hal_get_rssi(uint16_t conn_handle, int8_t *out_rssi);

/* --------------------------- bond management ---------------------------- */

/* True if at least one bond is stored in NVS (F18/F24). */
bool ble_hal_has_bond(void);

/* Number of stored peer bonds. */
int ble_hal_bond_count(void);

/*
 * ble_hal_delete_all_bonds — erase all stored bonds (factory-reset of pairings).
 * Returns a NimBLE status (0 == success).
 */
int ble_hal_delete_all_bonds(void);

/*
 * ble_hal_delete_bond_of_conn — delete the stored bond for the peer on this
 * connection, resolved by its identity address. Used when a peer bonds (Just
 * Works forms the bond before app-level checks run) but then FAILS the Identity
 * token check (F15): the impostor's bond must not persist, or the harness would
 * keep reconnecting to it. Must be called while the connection is still up so
 * the peer identity is resolvable. Returns a NimBLE status (0 == success).
 */
int ble_hal_delete_bond_of_conn(uint16_t conn_handle);

#endif /* QUICKLOCK_BLE_HAL_H */
