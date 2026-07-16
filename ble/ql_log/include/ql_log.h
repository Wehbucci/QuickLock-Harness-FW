/*
 * ql_log.h — thin per-module logging convention over esp_log.h.
 *
 * Purpose: give every QuickLock module a single, consistent way to declare its
 * log TAG and emit levelled logs, so all serial output (required for verifying
 * the BLE task without the fob — HARNESS_BLE_TASK.md section 10) looks uniform
 * and no module reaches for a stray printf().
 *
 * Usage in a .c file:
 *     #include "ql_log.h"
 *     QL_LOG_TAG("ble_task");     // defines the file-local TAG once
 *     ...
 *     QL_LOGI("state -> %s", name);
 *
 * These are intentionally trivial wrappers; they exist for the naming
 * discipline, not to hide esp_log. Anything esp_log can do is still available.
 */

#ifndef QUICKLOCK_QL_LOG_H
#define QUICKLOCK_QL_LOG_H

#include "esp_log.h"

/* Declare the file-local TAG. One call per translation unit, near the top. */
#define QL_LOG_TAG(name) static const char *TAG = (name)

#define QL_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define QL_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define QL_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define QL_LOGD(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#define QL_LOGV(fmt, ...) ESP_LOGV(TAG, fmt, ##__VA_ARGS__)

#endif /* QUICKLOCK_QL_LOG_H */
