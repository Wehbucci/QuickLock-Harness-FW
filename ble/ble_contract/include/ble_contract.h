/*
 * ble_contract.h — QuickLock wire-level BLE contract (single source of truth).
 *
 * Purpose: the exact UUIDs, GATT value encodings, and the Identity token shared
 * by BOTH sides of the QuickLock link. This header is deliberately
 * STACK-AGNOSTIC: it pulls in no NimBLE, ESP-IDF, Zephyr, or Arduino headers, so
 * the very same file can be compiled unchanged by
 *   - the ESP32 harness (NimBLE central, this project),
 *   - a mock peripheral, and
 *   - the future nRF52840 fob firmware (which may run Zephyr/NCS or Arduino).
 *
 * See BLE_CONTRACT.md. Serves: F13, F15, F17, F21, F23, F36.
 *
 * TRAP (documented once, here): 128-bit UUIDs go on the wire LITTLE-ENDIAN,
 * i.e. the reverse of how the UUID string reads. NimBLE's BLE_UUID128_DECLARE /
 * BLE_UUID128_INIT take the bytes in that little-endian order. To keep this
 * header free of any stack dependency, we expose each UUID as a comma-separated
 * list of its 16 little-endian bytes (a "..._BYTES_LE" macro). The one
 * translation unit that talks to NimBLE (ble_hal.c) feeds these straight into
 * BLE_UUID128_INIT(). Get the byte order right here once and every side inherits
 * it; a slip shows up as "the service is never found".
 */

#ifndef QUICKLOCK_BLE_CONTRACT_H
#define QUICKLOCK_BLE_CONTRACT_H

#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* 128-bit service and characteristic UUIDs (BLE_CONTRACT.md section 2)        */
/*                                                                            */
/* String form (how a human reads it) and the little-endian byte list (how it */
/* goes on the wire) are given together so they can be checked against each    */
/* other. Reverse the 16 bytes of the string to get the _BYTES_LE list.       */
/* -------------------------------------------------------------------------- */

/* QuickLock Service: 6b2f0001-9a3c-4b7e-8f21-0a4d2c9e1f30 */
#define QL_SVC_UUID_STR "6b2f0001-9a3c-4b7e-8f21-0a4d2c9e1f30"
#define QL_SVC_UUID_BYTES_LE \
    0x30, 0x1f, 0x9e, 0x2c, 0x4d, 0x0a, 0x21, 0x8f, \
    0x7e, 0x4b, 0x3c, 0x9a, 0x01, 0x00, 0x2f, 0x6b

/* Command characteristic: 6b2f0002-9a3c-4b7e-8f21-0a4d2c9e1f30
 * Fob->harness Notify (button press) + harness->fob Write-with-response. */
#define QL_CHR_COMMAND_UUID_STR "6b2f0002-9a3c-4b7e-8f21-0a4d2c9e1f30"
#define QL_CHR_COMMAND_UUID_BYTES_LE \
    0x30, 0x1f, 0x9e, 0x2c, 0x4d, 0x0a, 0x21, 0x8f, \
    0x7e, 0x4b, 0x3c, 0x9a, 0x02, 0x00, 0x2f, 0x6b

/* Identity characteristic: 6b2f0003-9a3c-4b7e-8f21-0a4d2c9e1f30
 * Read, encryption required (server sets BLE_GATT_CHR_F_READ_ENC). */
#define QL_CHR_IDENTITY_UUID_STR "6b2f0003-9a3c-4b7e-8f21-0a4d2c9e1f30"
#define QL_CHR_IDENTITY_UUID_BYTES_LE \
    0x30, 0x1f, 0x9e, 0x2c, 0x4d, 0x0a, 0x21, 0x8f, \
    0x7e, 0x4b, 0x3c, 0x9a, 0x03, 0x00, 0x2f, 0x6b

/* TODO(F29): Fob Battery characteristic 6b2f0004-... (Read + Notify, uint8
 * percent). Deferred per BLE_CONTRACT.md section 2; do NOT implement yet. Its
 * little-endian byte list would end in 0x04, 0x00, 0x2f, 0x6b. */

/* Client Characteristic Configuration Descriptor (standard 16-bit 0x2902).
 * Writing 0x0001 to it subscribes to Command notifications. */
#define QL_CCCD_UUID16            0x2902u
#define QL_CCCD_NOTIFY_ENABLE     0x0001u

/* -------------------------------------------------------------------------- */
/* Command characteristic value encodings (BLE_CONTRACT.md section 3.1)        */
/* Single-byte payloads; replay/eavesdrop protection is at the link layer.     */
/* -------------------------------------------------------------------------- */

/* Fob -> harness (Notify): request opcodes. */
typedef enum {
    QL_CMD_ARM     = 0x01, /* ARM request */
    QL_CMD_DISARM  = 0x02, /* DISARM request */
    QL_CMD_SILENCE = 0x03, /* SILENCE an active alarm */
} ql_command_t;
/* Any other Command value is ignored and logged as a protocol error. */

/* Harness -> fob (Write-with-response): confirmed system state (F17). */
typedef enum {
    QL_STATE_DISARMED = 0x00, /* System is now DISARMED */
    QL_STATE_ARMED    = 0x01, /* System is now ARMED */
} ql_state_t;

/* -------------------------------------------------------------------------- */
/* Identity token (BLE_CONTRACT.md section 3.2)                                */
/*                                                                            */
/* Fixed 16-byte project token, readable only over an encrypted (bonded) link. */
/* Hex 51 55 49 43 4B 4C 4F 43 4B 5F 46 4F 42 5F 76 31 == ASCII               */
/* "QUICKLOCK_FOB_v1". The harness reads it once after encryption and compares */
/* to this constant; mismatch (or a read that fails because the link is not    */
/* encrypted) means the peer is not a genuine bonded fob and is dropped (F15). */
/* -------------------------------------------------------------------------- */

#define QL_IDENTITY_TOKEN_LEN 16

#define QL_IDENTITY_TOKEN_BYTES \
    0x51, 0x55, 0x49, 0x43, 0x4B, 0x4C, 0x4F, 0x43, \
    0x4B, 0x5F, 0x46, 0x4F, 0x42, 0x5F, 0x76, 0x31

/* Single definition of the token as an array, usable from any translation unit.
 * 'static const' in a header is intentional: the token is tiny and read-only,
 * and this keeps the contract self-contained with no matching .c file. */
static const uint8_t QL_IDENTITY_TOKEN[QL_IDENTITY_TOKEN_LEN] = {
    QL_IDENTITY_TOKEN_BYTES
};

#endif /* QUICKLOCK_BLE_CONTRACT_H */
