/*
 * ble_events.c — outbound event queue + stub Security consumer.
 *
 * See ble_events.h. This translation unit has no NimBLE dependency; it is pure
 * FreeRTOS plumbing plus logging. HARNESS_BLE_TASK.md sections 4, 10, 11.
 */

#include "ble_events.h"
#include "config.h"
#include "ql_log.h"
#include "freertos/task.h"

QL_LOG_TAG("ble_events");

static QueueHandle_t s_event_q = NULL;

esp_err_t ble_events_init(void)
{
    if (s_event_q != NULL) {
        return ESP_OK; /* idempotent */
    }
    s_event_q = xQueueCreate(BLE_EVENT_QUEUE_LEN, sizeof(ble_event_t));
    if (s_event_q == NULL) {
        QL_LOGE("failed to allocate event queue");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

QueueHandle_t ble_events_queue(void)
{
    return s_event_q;
}

bool ble_events_post(ble_event_kind_t kind, int32_t detail)
{
    if (s_event_q == NULL) {
        QL_LOGE("post before init: %s", ble_event_name(kind));
        return false;
    }
    ble_event_t evt = { .kind = kind, .detail = detail };
    /* Zero timeout: the BLE task must never block on a full Security queue. A
     * full queue means Security is wedged; log and drop rather than stall the
     * radio task. */
    if (xQueueSend(s_event_q, &evt, 0) != pdTRUE) {
        QL_LOGW("event queue full, dropped %s (detail=%ld)",
                ble_event_name(kind), (long)detail);
        return false;
    }
    /* Required log: every event posted to the Security queue (section 10). */
    QL_LOGI("-> Security: %s (detail=%ld)", ble_event_name(kind), (long)detail);
    return true;
}

const char *ble_event_name(ble_event_kind_t kind)
{
    switch (kind) {
    case BLE_EVT_FOB_CONNECTED:         return "FOB_CONNECTED";
    case BLE_EVT_FOB_DISCONNECTED:      return "FOB_DISCONNECTED";
    case BLE_EVT_ARM_REQUESTED:         return "ARM_REQUESTED";
    case BLE_EVT_DISARM_REQUESTED:      return "DISARM_REQUESTED";
    case BLE_EVT_SILENCE_REQUESTED:     return "SILENCE_REQUESTED";
    case BLE_EVT_FOB_OUT_OF_RANGE:      return "FOB_OUT_OF_RANGE";
    case BLE_EVT_FOB_IN_RANGE:          return "FOB_IN_RANGE";
    case BLE_EVT_LINK_LOST_SUPERVISION: return "LINK_LOST_SUPERVISION";
    case BLE_EVT_PAIRING_SUCCEEDED:     return "PAIRING_SUCCEEDED";
    case BLE_EVT_PAIRING_FAILED:        return "PAIRING_FAILED";
    case BLE_EVT_IDENTITY_REJECTED:     return "IDENTITY_REJECTED";
    default:                            return "UNKNOWN";
    }
}
