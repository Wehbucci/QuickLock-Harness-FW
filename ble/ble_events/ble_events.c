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
#include "esp_timer.h"

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

/*
 * Stub Security consumer. Drains the queue and logs each event with a boot-
 * relative timestamp, standing in for the real Security Core task so the BLE
 * half can be verified end-to-end without the fob (TESTING_WITHOUT_FOB.md).
 */
static void security_stub_task(void *arg)
{
    (void)arg;
    QL_LOGI("stub Security consumer started (TODO: replace with Security Core)");
    for (;;) {
        ble_event_t evt;
        if (xQueueReceive(s_event_q, &evt, portMAX_DELAY) == pdTRUE) {
            int64_t t_ms = esp_timer_get_time() / 1000;
            /* TODO(security-core): drive the security_state machine here instead
             * of just logging. */
            QL_LOGI("[t=%lld ms] consumed %s (detail=%ld)",
                    (long long)t_ms, ble_event_name(evt.kind), (long)evt.detail);
        }
    }
}

void ble_events_start_stub_consumer(void)
{
    /* Core 1 on purpose: the safety-critical chain lives on core 1 (F35) and the
     * real Security task will too, so exercise that placement now. Low priority;
     * it only logs. */
    xTaskCreatePinnedToCore(security_stub_task, "sec_stub", 3072, NULL, 2, NULL, 1);
}
