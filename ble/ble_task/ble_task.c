/*
 * ble_task.c — BLE Communication task: connection state machine + proximity.
 *
 * Event-driven block-on-queue loop (HARNESS_BLE_TASK.md section 3): the task
 * sleeps on its inbound queue for up to HOUSEKEEP_MS, then either handles a HAL
 * event (woken early) or runs periodic housekeeping (woken by the timeout). It
 * decides, then acts through ble_hal, and wakes the Security task by posting to
 * ble_events. It includes NO NimBLE headers — the grep -r "nimble/" invariant.
 *
 * Serves: F13, F14, F15, F17, F22, F24.
 */

#include "ble_task.h"
#include "ble_hal.h"
#include "ble_events.h"
#include "ble_contract.h"
#include "config.h"
#include "proximity.h"
#include "ql_log.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

QL_LOG_TAG("ble_task");

/* 0xFFFF is NimBLE's "no connection" sentinel; mirror it locally to avoid a
 * NimBLE include here (kept in sync with BLE_HS_CONN_HANDLE_NONE). */
#define BLE_HS_INVALID_HANDLE 0xFFFF

/* Connection state machine (HARNESS_BLE_TASK.md section 5). */
typedef enum {
    ST_IDLE = 0,          /* pre-sync, or nothing to do */
    ST_SCANNING,          /* discovering the QuickLock service */
    ST_CONNECTING,        /* connection request outstanding */
    ST_BONDING,           /* encrypting / pairing */
    ST_VERIFYING_IDENTITY,/* discovery + Identity read over encrypted link */
    ST_CONNECTED,         /* subscribed; servicing commands + RSSI */
} ble_state_t;

static const char *state_name(ble_state_t s)
{
    switch (s) {
    case ST_IDLE:               return "IDLE";
    case ST_SCANNING:           return "SCANNING";
    case ST_CONNECTING:         return "CONNECTING";
    case ST_BONDING:            return "BONDING";
    case ST_VERIFYING_IDENTITY: return "VERIFYING_IDENTITY";
    case ST_CONNECTED:          return "CONNECTED";
    default:                    return "?";
    }
}

/* -------------------------------------------------------------------------- */
/* Task-local state                                                            */
/* -------------------------------------------------------------------------- */
static QueueHandle_t s_inbound_q;
static ble_state_t   s_state = ST_IDLE;
static uint16_t      s_conn_handle;
static proximity_t   s_prox;

static bool          s_started;             /* first SCANNING kickoff done */

/* Pairing-mode window (F15). */
static volatile bool s_pairing_request;     /* set by ble_task_enter_pairing_mode */
static bool          s_pairing_mode;
static int64_t       s_pairing_deadline_us;
static int           s_bonds_at_connect;    /* to detect a freshly formed bond */

/* Timers, kept in microseconds via esp_timer so cadence is independent of what
 * woke the task (a real clock, not "20 ms ticks since something happened"). */
static int64_t s_last_rssi_us;
static int64_t s_connect_started_us;        /* for the F24 3 s budget log */

/* Mechanism A re-acquire grace after a supervision-timeout disconnect (F22). */
static bool    s_reacquire_pending;
static int64_t s_reacquire_deadline_us;

#if QL_TEST_HOOKS_ENABLED
/* Bench RSSI override (see ble_task.h for the rationale). Written by the
 * console task, read by housekeeping on the BLE task; single aligned words, no
 * read-modify-write on either side, so volatile is sufficient. */
static volatile bool   s_rssi_override_on;
static volatile int8_t s_rssi_override_dbm;
#endif

static int64_t now_us(void) { return esp_timer_get_time(); }
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* -------------------------------------------------------------------------- */
/* State transition helper (with required logging, section 10)                 */
/* -------------------------------------------------------------------------- */
static void set_state(ble_state_t next)
{
    if (next != s_state) {
        QL_LOGI("state %s -> %s", state_name(s_state), state_name(next));
        s_state = next;
    }
}

/* Drop the current link (if any) and return to scanning for a known bond. */
static void drop_and_rescan(const char *why)
{
    QL_LOGW("dropping link: %s", why);
    if (s_conn_handle != BLE_HS_INVALID_HANDLE) {
        ble_hal_disconnect(s_conn_handle);
    }
    /* The DISCONNECT event will restart the scan; if we were never connected,
     * kick the scan here. */
    if (s_state == ST_SCANNING || s_state == ST_IDLE) {
        ble_hal_start_scan();
    }
}

/* -------------------------------------------------------------------------- */
/* Pairing window helpers                                                      */
/* -------------------------------------------------------------------------- */
static void open_pairing_window(const char *why)
{
    s_pairing_mode = true;
    s_pairing_deadline_us = now_us() + (int64_t)PAIRING_WINDOW_MS * 1000;
    QL_LOGI("pairing mode OPEN for %d ms (%s)", PAIRING_WINDOW_MS, why);
}

static void close_pairing_window(const char *why)
{
    if (s_pairing_mode) {
        s_pairing_mode = false;
        QL_LOGI("pairing mode CLOSED (%s)", why);
    }
}

/* -------------------------------------------------------------------------- */
/* One-time startup once the host has synced                                   */
/* -------------------------------------------------------------------------- */
static void start_scanning_once_ready(void)
{
    if (s_started || !ble_hal_is_ready()) {
        return;
    }
    s_started = true;

    if (ble_hal_has_bond()) {
        /* Known fob exists: reconnect only to it (no pairing). (F24) */
        QL_LOGI("boot: %d bond(s) present -> scanning to reconnect",
                ble_hal_bond_count());
    } else {
        /* First boot / no bond: open the pairing window automatically so the
         * harness can bond with the fob (or mock) without a physical button.
         * A real deployment would gate this behind the pairing button. */
        open_pairing_window("no bond stored at boot");
    }
    set_state(ST_SCANNING);
    ble_hal_start_scan();
}

/* -------------------------------------------------------------------------- */
/* Command handling (F13, F17)                                                 */
/* -------------------------------------------------------------------------- */

/*
 * Map a validated Command opcode to a Security event and the state byte to write
 * back to the fob. NOTE: in the full system the CONFIRMED state comes back from
 * the Security task after it acts; here (Security not built yet) we optimistically
 * write the state implied by the command so the fob's confirmation LED (F17) can
 * be exercised end-to-end. TODO(security-core): drive the write-back from
 * Security's confirmed state instead.
 */
static void handle_command(uint8_t opcode)
{
    switch (opcode) {
    case QL_CMD_ARM:
        QL_LOGI("Command RX: ARM (0x%02x)", opcode);
        ble_events_post(BLE_EVT_ARM_REQUESTED, 0);
        ble_hal_write_state(s_conn_handle, QL_STATE_ARMED);
        break;
    case QL_CMD_DISARM:
        QL_LOGI("Command RX: DISARM (0x%02x)", opcode);
        ble_events_post(BLE_EVT_DISARM_REQUESTED, 0);
        ble_hal_write_state(s_conn_handle, QL_STATE_DISARMED);
        break;
    case QL_CMD_SILENCE:
        QL_LOGI("Command RX: SILENCE (0x%02x)", opcode);
        ble_events_post(BLE_EVT_SILENCE_REQUESTED, 0);
        /* Silence does not change armed state, so no state write-back. */
        break;
    default:
        /* Any other value is ignored and logged as a protocol error (contract
         * section 3.1). */
        QL_LOGW("Command RX: unknown opcode 0x%02x (ignored)", opcode);
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* Inbound HAL event handling                                                  */
/* -------------------------------------------------------------------------- */
static void handle_event(const ble_inbound_msg_t *msg)
{
    switch (msg->kind) {

    case BLE_IN_SCAN_MATCH: {
        if (s_state != ST_SCANNING) {
            return; /* already connecting/connected; ignore stray reports */
        }
        /* Connect if we have a bond (reconnect) OR the pairing window is open
         * (new fob). Address-level bond matching would need IRK/RPA resolution;
         * instead we gate here and rely on encryption + the Identity read to
         * reject an impostor (F15). */
        bool may_connect = ble_hal_has_bond() || s_pairing_mode;
        QL_LOGI("scan match: addr %02x:%02x:%02x:%02x:%02x:%02x rssi=%d -> %s",
                msg->addr.val[5], msg->addr.val[4], msg->addr.val[3],
                msg->addr.val[2], msg->addr.val[1], msg->addr.val[0],
                msg->rssi, may_connect ? "connecting" : "ignored (locked)");
        if (!may_connect) {
            return;
        }
        s_bonds_at_connect = ble_hal_bond_count();
        s_connect_started_us = now_us();
        set_state(ST_CONNECTING);
        if (ble_hal_connect(&msg->addr) != 0) {
            set_state(ST_SCANNING);
            ble_hal_start_scan();
        }
        break;
    }

    case BLE_IN_CONNECTED:
        if (msg->status != 0) {
            QL_LOGW("connect failed; status=%d -> rescan", msg->status);
            set_state(ST_SCANNING);
            ble_hal_start_scan();
            break;
        }
        s_conn_handle = msg->conn_handle;
        s_reacquire_pending = false; /* reconnected before/at grace: cancel A */
        QL_LOGI("connected; conn=%u", s_conn_handle);
        /* Ask the peer to adopt our target connection parameters (F26 support).
         * The peripheral may negotiate or ignore this. */
        ble_hal_request_conn_params(s_conn_handle);
        /* Encrypt (or pair, if new). Bonding is only meaningful in the pairing
         * window; if there is no bond and pairing is closed, drop now. */
        if (!ble_hal_has_bond() && !s_pairing_mode) {
            drop_and_rescan("unbonded peer outside pairing window");
            break;
        }
        set_state(ST_BONDING);
        if (ble_hal_start_security(s_conn_handle) != 0) {
            drop_and_rescan("security_initiate failed");
        }
        break;

    case BLE_IN_ENC_CHANGED:
        if (msg->status != 0) {
            QL_LOGW("encryption failed; status=%d", msg->status);
            ble_events_post(BLE_EVT_PAIRING_FAILED, msg->status);
            drop_and_rescan("encryption failed");
            break;
        }
        QL_LOGI("link encrypted");
        /* A brand-new bond appears as an increase in the stored-bond count. */
        if (ble_hal_bond_count() > s_bonds_at_connect) {
            QL_LOGI("new bond formed (now %d)", ble_hal_bond_count());
            ble_events_post(BLE_EVT_PAIRING_SUCCEEDED, 0);
            close_pairing_window("bond formed");
        }
        set_state(ST_VERIFYING_IDENTITY);
        /* Only now (encrypted) discover and read Identity (F15/F36). */
        if (ble_hal_discover(s_conn_handle) != 0) {
            drop_and_rescan("discovery kickoff failed");
        }
        break;

    case BLE_IN_DISC_DONE:
        QL_LOGI("discovery ok -> reading Identity");
        if (ble_hal_read_identity(s_conn_handle) != 0) {
            drop_and_rescan("identity read kickoff failed");
        }
        break;

    case BLE_IN_DISC_FAILED:
        drop_and_rescan("service/characteristic discovery failed");
        break;

    case BLE_IN_IDENTITY_READ: {
        bool ok = (msg->status == 0) &&
                  (msg->payload_len == QL_IDENTITY_TOKEN_LEN) &&
                  (memcmp(msg->payload, QL_IDENTITY_TOKEN,
                          QL_IDENTITY_TOKEN_LEN) == 0);
        if (!ok) {
            /* Wrong token, wrong length, or a read that failed because the link
             * was not truly encrypted -> not a genuine fob (F15). */
            QL_LOGW("Identity REJECTED (status=%d len=%u)",
                    msg->status, msg->payload_len);
            ble_events_post(BLE_EVT_IDENTITY_REJECTED, msg->status);
            /* Just Works forms the bond before this app-level check, so an
             * impostor that paired in the window now has a stored bond. Delete
             * it (while the link is still up so the peer is resolvable) before
             * dropping, or we would reconnect to it forever (F15). */
            ble_hal_delete_bond_of_conn(s_conn_handle);
            drop_and_rescan("identity mismatch");
            break;
        }
        QL_LOGI("Identity verified (token match) -> subscribing to Command");
        if (ble_hal_subscribe_command(s_conn_handle) != 0) {
            drop_and_rescan("subscribe kickoff failed");
        }
        break;
    }

    case BLE_IN_SUBSCRIBED:
        if (msg->status != 0) {
            QL_LOGW("CCCD subscribe failed; status=%d", msg->status);
            drop_and_rescan("subscribe failed");
            break;
        }
        /* Fully up: proximity fresh, RSSI cadence reset, notify Security. */
        {
            proximity_config_t pcfg = {
                .c_dbm = RSSI_C_DBM, .n = RSSI_N, .alpha = RSSI_ALPHA,
                .out_threshold_dbm = OUT_THRESHOLD_DBM,
                .in_threshold_dbm = IN_THRESHOLD_DBM,
            };
            proximity_init(&s_prox, &pcfg);
        }
        s_last_rssi_us = now_us();
        set_state(ST_CONNECTED);
        int64_t elapsed_ms = (now_us() - s_connect_started_us) / 1000;
        QL_LOGI("CONNECTED and subscribed in %lld ms (F24 budget %d ms)",
                (long long)elapsed_ms, CONNECT_BUDGET_MS);
        if (elapsed_ms > CONNECT_BUDGET_MS) {
            QL_LOGW("exceeded F24 3 s connect budget (%lld ms)",
                    (long long)elapsed_ms);
        }
        ble_events_post(BLE_EVT_FOB_CONNECTED, 0);
        break;

    case BLE_IN_WRITE_DONE:
        /* ATT-acknowledged state write-back completed (F13/F17 loop closed). */
        QL_LOGI("state write-back ACK (status=%d)", msg->status);
        break;

    case BLE_IN_NOTIFY_RX:
        if (s_state != ST_CONNECTED) {
            QL_LOGW("notify before CONNECTED (ignored)");
            break;
        }
        if (msg->payload_len < 1) {
            QL_LOGW("empty Command notification (ignored)");
            break;
        }
        handle_command(msg->payload[0]);
        break;

    case BLE_IN_CONN_UPDATED:
        /* Negotiated params already logged by the HAL; nothing to decide. */
        break;

    case BLE_IN_DISCONNECTED: {
        bool was_connected = (s_state == ST_CONNECTED);
        bool spvn = ble_hal_reason_is_supervision_timeout(msg->status);
        QL_LOGW("disconnected (reason=0x%04x, was %s%s)",
                msg->status, state_name(s_state),
                spvn ? ", SUPERVISION TIMEOUT" : "");
        if (was_connected) {
            ble_events_post(BLE_EVT_FOB_DISCONNECTED, msg->status);
        }
        s_conn_handle = BLE_HS_INVALID_HANDLE;

        /* Mechanism A (F22): a supervision timeout is the authoritative
         * "link truly lost" signal. Give the fob a short grace to re-appear
         * (walked past a dead spot, brief interference); if it does not, raise
         * the hard trigger for the auto-arm decision. The disarmed/armed policy
         * belongs to Security, which consumes this event. */
        if (spvn) {
            s_reacquire_pending = true;
            s_reacquire_deadline_us = now_us() + (int64_t)REACQUIRE_GRACE_MS * 1000;
            QL_LOGW("supervision timeout: re-acquire grace %d ms started",
                    REACQUIRE_GRACE_MS);
        }
        set_state(ST_SCANNING);
        ble_hal_start_scan();
        break;
    }

    default:
        QL_LOGD("unhandled inbound kind=%d", (int)msg->kind);
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* Periodic housekeeping (runs on the queue-timeout tick)                      */
/* -------------------------------------------------------------------------- */
static void housekeeping_tick(void)
{
    /* Wait for the host to sync, then kick off scanning exactly once. */
    start_scanning_once_ready();

    const int64_t t = now_us();

    /* Honour a pairing-mode request raised from another context (button/console). */
    if (s_pairing_request) {
        s_pairing_request = false;
        open_pairing_window("requested");
        /* If we are idle-scanning with no bond, we are already scanning; if we
         * are stuck not scanning, make sure we are. */
        if (s_state == ST_IDLE && ble_hal_is_ready()) {
            set_state(ST_SCANNING);
            ble_hal_start_scan();
        }
    }

    /* Tick the pairing-window countdown. */
    if (s_pairing_mode && t > s_pairing_deadline_us) {
        close_pairing_window("window elapsed");
    }

    /* Tick the Mechanism-A re-acquire grace. */
    if (s_reacquire_pending && t > s_reacquire_deadline_us) {
        s_reacquire_pending = false;
        QL_LOGW("re-acquire grace elapsed without reconnect -> LINK_LOST");
        ble_events_post(BLE_EVT_LINK_LOST_SUPERVISION, 0);
    }

    /* Mechanism B: sample + filter RSSI at ~1 Hz while connected (F14). */
    if (s_state == ST_CONNECTED &&
        (t - s_last_rssi_us) >= (int64_t)RSSI_SAMPLE_MS * 1000) {
        s_last_rssi_us = t;
        int8_t raw = 0;
        int rc;
#if QL_TEST_HOOKS_ENABLED
        if (s_rssi_override_on) {
            /* Test hook: substitute the sample at the source, so everything
             * downstream (EMA, hysteresis, events) is the real code path. */
            raw = s_rssi_override_dbm;
            rc = 0;
        } else
#endif
        {
            rc = ble_hal_get_rssi(s_conn_handle, &raw);
        }
        if (rc != 0 || !proximity_rssi_is_valid(raw)) {
            QL_LOGD("RSSI sample unavailable (rc=%d raw=%d)", rc, raw);
            return;
        }
        float filt = proximity_update(&s_prox, raw);
        /* Required log: raw AND filtered RSSI per sample (section 10). */
        QL_LOGI("RSSI raw=%d dBm filt=%.1f dBm (~%.1f m)",
                raw, filt, proximity_distance_m(&s_prox, filt));

        switch (proximity_evaluate(&s_prox)) {
        case PROX_WENT_OUT:
            QL_LOGW("filtered RSSI crossed OUT threshold (%.0f dBm)",
                    (double)OUT_THRESHOLD_DBM);
            ble_events_post(BLE_EVT_FOB_OUT_OF_RANGE, (int32_t)filt);
            break;
        case PROX_CAME_IN:
            QL_LOGI("filtered RSSI crossed IN threshold (%.0f dBm)",
                    (double)IN_THRESHOLD_DBM);
            ble_events_post(BLE_EVT_FOB_IN_RANGE, (int32_t)filt);
            break;
        case PROX_NO_CHANGE:
        default:
            break;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* The task: block-on-queue loop (HARNESS_BLE_TASK.md section 3)                */
/* -------------------------------------------------------------------------- */
static void ble_task_fn(void *arg)
{
    (void)arg;
    QL_LOGI("BLE Communication task started (prio %d, core %d)",
            BLE_TASK_PRIO, xPortGetCoreID());
    for (;;) {
        ble_inbound_msg_t msg;
        /* Block here, sleeping, until a HAL callback posts OR HOUSEKEEP_MS
         * elapses. This is a wake-on-event loop, not a poll. */
        if (xQueueReceive(s_inbound_q, &msg, pdMS_TO_TICKS(HOUSEKEEP_MS)) == pdTRUE) {
            handle_event(&msg);      /* woken by an event */
        } else {
            housekeeping_tick();     /* woken by the timeout */
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */
esp_err_t ble_task_start(void)
{
    s_conn_handle = BLE_HS_INVALID_HANDLE;
    s_inbound_q = xQueueCreate(BLE_INBOUND_QUEUE_LEN, sizeof(ble_inbound_msg_t));
    if (s_inbound_q == NULL) {
        QL_LOGE("failed to create inbound queue");
        return ESP_ERR_NO_MEM;
    }
    /* Register the queue as the HAL's event sink BEFORE any scan, so no early
     * event is lost. */
    ble_hal_set_event_sink(s_inbound_q);

    BaseType_t ok = xTaskCreatePinnedToCore(ble_task_fn, "ble_task",
                                            BLE_TASK_STACK, NULL, BLE_TASK_PRIO,
                                            NULL, BLE_TASK_CORE);
    if (ok != pdPASS) {
        QL_LOGE("failed to create BLE task");
        return ESP_ERR_NO_MEM;
    }
    QL_LOGI("BLE task created; waiting for NimBLE host sync at t=%lld ms",
            (long long)now_ms());
    return ESP_OK;
}

void ble_task_enter_pairing_mode(void)
{
    /* Single-writer flag consumed by housekeeping; safe across tasks on ESP32
     * (aligned 32-bit access is atomic). Keeps this callable from a button ISR
     * deferral or a console command without touching the state machine directly. */
    s_pairing_request = true;
}

void ble_task_log_status(void)
{
    QL_LOGI("--- status ---");
    QL_LOGI("state    : %s", state_name(s_state));
    if (s_conn_handle != BLE_HS_INVALID_HANDLE) {
        QL_LOGI("conn     : handle=%u", s_conn_handle);
    } else {
        QL_LOGI("conn     : none");
    }
    QL_LOGI("bonds    : %d stored (max %d)", ble_hal_bond_count(), MAX_BONDS);

    if (s_pairing_mode) {
        int64_t left_ms = (s_pairing_deadline_us - now_us()) / 1000;
        QL_LOGI("pairing  : OPEN (%lld ms left)", (long long)(left_ms > 0 ? left_ms : 0));
    } else {
        QL_LOGI("pairing  : CLOSED (new bonds refused; `pair` opens it)");
    }

    if (s_state == ST_CONNECTED) {
        float filt = proximity_filtered_rssi(&s_prox);
        QL_LOGI("proximity: filt=%.1f dBm (~%.1f m) out_of_range=%s",
                filt, proximity_distance_m(&s_prox, filt),
                proximity_is_out_of_range(&s_prox) ? "YES" : "no");
    } else {
        QL_LOGI("proximity: n/a (not connected)");
    }

#if QL_TEST_HOOKS_ENABLED
    if (s_rssi_override_on) {
        QL_LOGW("rssi ovr : ON at %d dBm -- NOT reading the real radio",
                s_rssi_override_dbm);
    } else {
        QL_LOGI("rssi ovr : off (real radio RSSI)");
    }
#endif
    QL_LOGI("--------------");
}

#if QL_TEST_HOOKS_ENABLED
void ble_task_inject_rssi(int8_t dbm)
{
    s_rssi_override_dbm = dbm;
    s_rssi_override_on = true;
    /* Warn, not info: a forgotten override would make every later range test
     * lie, so it should be loud in the log. */
    QL_LOGW("TEST HOOK: RSSI override ON at %d dBm (real radio RSSI ignored)", dbm);
}

void ble_task_clear_rssi_override(void)
{
    s_rssi_override_on = false;
    QL_LOGI("TEST HOOK: RSSI override OFF (back to real radio RSSI)");
}
#endif
