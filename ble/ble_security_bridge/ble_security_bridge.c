/*
 * ble_security_bridge.c — BLE event queue -> Security Core command + notify.
 *
 * See ble_security_bridge.h. No NimBLE dependency: this is queue plumbing plus
 * the event->command mapping table. HARNESS_BLE_TASK.md sections 4, 11.
 */

#include "ble_security_bridge.h"

#include "ble_events.h"
#include "config.h"
#include "globals.h"
#include "ql_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

QL_LOG_TAG("ble_sec_bridge");

/*
 * Map a BLE event to the command the Security Core task understands.
 *
 * Returns true and writes *out_cmd if this event should drive Security; false if
 * the event is BLE-internal (link/pairing bookkeeping) and Security has no say
 * in it. Only three commands exist in `enum BLE_COMMANDS`, so most of the
 * eleven BLE events are deliberately informational.
 */
static bool map_event(ble_event_kind_t kind, enum BLE_COMMANDS *out_cmd)
{
    switch (kind) {
    case BLE_EVT_ARM_REQUESTED:
        /* F13: explicit ARM from the fob. */
        *out_cmd = BLE_ARM;
        return true;

    case BLE_EVT_DISARM_REQUESTED:
        /* F13: explicit DISARM from the fob. The only path back to S0 —
         * every armed state accepts it (state machine, S0 transition). */
        *out_cmd = BLE_DISARM;
        return true;

    case BLE_EVT_FOB_OUT_OF_RANGE:
        /* F14, Mechanism B: filtered RSSI crossed the far threshold while the
         * link is still up. Soft "user walked away" signal. */
        *out_cmd = BLE_OOR;
        return true;

    case BLE_EVT_LINK_LOST_SUPERVISION:
        /* F22, Mechanism A: supervision timeout, and the fob did not re-appear
         * within the re-acquire grace. Authoritative "user left"; fail secure.
         * Same command as Mechanism B because Security's response is identical
         * (auto-arm from S0); the two mechanisms differ only in confidence,
         * which is already distinguished in the log above. */
        *out_cmd = BLE_OOR;
        return true;

    case BLE_EVT_SILENCE_REQUESTED:
        /* TODO(security-core): `enum BLE_COMMANDS` has no SILENCE member, so
         * the fob's silence opcode (contract 0x03) currently reaches Security as
         * nothing at all. Silencing must NOT be folded into BLE_DISARM: disarm
         * leaves the state machine in S0, whereas silence is meant to quiet the
         * sounder while the harness stays armed. Adding BLE_SILENCE to globals.h
         * plus a case in security_core_task.c is the fix. */
        QL_LOGW("SILENCE has no Security command yet; dropped (see TODO)");
        return false;

    case BLE_EVT_FOB_IN_RANGE:
        /* Coming back into range does NOT disarm: per the state machine, S0 is
         * reachable only via an explicit disarm command from the fob. Purely
         * informational here. */
    case BLE_EVT_FOB_CONNECTED:
    case BLE_EVT_FOB_DISCONNECTED:
        /* A bare disconnect is not yet "the user left" — ble_task starts the
         * re-acquire grace and raises LINK_LOST_SUPERVISION if it expires.
         * Acting here too would arm twice for one departure. */
    case BLE_EVT_PAIRING_SUCCEEDED:
    case BLE_EVT_PAIRING_FAILED:
    case BLE_EVT_IDENTITY_REJECTED:
        return false;

    default:
        QL_LOGW("unknown event kind=%d", (int)kind);
        return false;
    }
}

/*
 * Drain the BLE event queue forever, republishing actionable events to Security.
 *
 * Concurrency note — why this task's priority and core are not arbitrary:
 * `ble_command` is a single shared global with no lock, so a second event must
 * not overwrite it before Security has read the first. This task runs on
 * BLE_SECURITY_BRIDGE_CORE at BLE_SECURITY_BRIDGE_PRIO, which is the same core
 * as the Security Core task and a LOWER priority than it. Under FreeRTOS SMP
 * that makes ble_wake_up_security_task() preempt us immediately: Security runs
 * to its next block and consumes ble_command before we get the CPU back to
 * handle the next event. Raising this priority to or above Security's, or moving
 * it to core 0, reintroduces the lost-command race. See config.h.
 */
static void bridge_task(void *arg)
{
    (void)arg;
    QueueHandle_t q = ble_events_queue();

    QL_LOGI("BLE->Security bridge started (core %d)", xPortGetCoreID());

    for (;;) {
        ble_event_t evt;
        if (xQueueReceive(q, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        enum BLE_COMMANDS cmd;
        if (!map_event(evt.kind, &cmd)) {
            QL_LOGI("%s (detail=%ld): no Security command",
                    ble_event_name(evt.kind), (long)evt.detail);
            continue;
        }

        /* The contract from globals.h: publish the command, then notify. */
        ble_command = cmd;
        ble_wake_up_security_task();

        QL_LOGI("%s (detail=%ld) -> ble_command=%d, Security notified",
                ble_event_name(evt.kind), (long)evt.detail, (int)cmd);
    }
}

esp_err_t ble_security_bridge_start(void)
{
    if (ble_events_queue() == NULL) {
        QL_LOGE("ble_events_init() must be called first");
        return ESP_ERR_INVALID_STATE;
    }
    if (security_core_task_handle == NULL) {
        /* Notifying a NULL handle is undefined; fail loudly at boot rather than
         * silently dropping every command later. */
        QL_LOGE("security_core_task must be created before the bridge starts");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(bridge_task,
                                            "ble_sec_bridge",
                                            BLE_SECURITY_BRIDGE_STACK,
                                            NULL,
                                            BLE_SECURITY_BRIDGE_PRIO,
                                            NULL,
                                            BLE_SECURITY_BRIDGE_CORE);
    if (ok != pdPASS) {
        QL_LOGE("failed to create bridge task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
