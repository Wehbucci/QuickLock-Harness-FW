/*
 * ql_console.h — serial test console for harness bring-up.
 *
 * Purpose: give the harness the operator inputs it needs on a bench, over the
 * one channel that exists today — the UART that `idf.py monitor` is already
 * showing. The harness has no board HAL and no buttons yet, so before this
 * module three capabilities the firmware already implemented were unreachable
 * from outside:
 *
 *   - ble_task_enter_pairing_mode()  — marked TODO(ui), no caller
 *   - ble_hal_delete_all_bonds()     — no caller (re-pair meant erase-flash)
 *   - the proximity decision path    — only reachable by walking away
 *
 * Commands:
 *   pair            open the pairing window (F15) for PAIRING_WINDOW_MS
 *   status          state machine, conn handle, bonds, pairing window, RSSI
 *   bonds           how many bonds are stored
 *   bonds clear     delete all stored bonds (forces a re-pair; F15/F18)
 *   rssi <dbm>      force the RSSI sample, e.g. `rssi -80` (test hook)
 *   rssi clear      go back to the real radio RSSI
 *   help            list commands
 *
 * This is a BRING-UP AID, not a product feature. It is also the placeholder
 * for the real UI: when the pairing button and board HAL exist, `pair` becomes
 * one more caller of the same ble_task API, not a rewrite.
 *
 * Placement: the REPL task runs on core 1 (config.h QL_CONSOLE_TASK_CORE) so a
 * human at a keyboard never contends with the radio on core 0 (F35).
 *
 * Serves (as a test surface): F13, F14, F15, F17, F18, F22.
 */

#ifndef QUICKLOCK_QL_CONSOLE_H
#define QUICKLOCK_QL_CONSOLE_H

#include "esp_err.h"

/*
 * ql_console_start — register the commands and start the UART REPL. Call once
 * from app_main AFTER ble_task_start(), so every command has something to talk
 * to. Returns ESP_OK, or an esp_err_t if the REPL could not be created.
 *
 * Takes over stdin. Log output continues to go to the same UART, so lines will
 * interleave with the prompt — that is expected and harmless; press Enter to
 * redraw the prompt.
 */
esp_err_t ql_console_start(void);

#endif /* QUICKLOCK_QL_CONSOLE_H */
