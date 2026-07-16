# ble_events: outbound event queue to the (future) Security task + stub consumer.
# Pure FreeRTOS plumbing; needs config for queue depth, ql_log for logging,
# esp_timer for timestamps. FreeRTOS + esp_timer come from the common requires.
idf_component_register(SRCS "ble_events.c"
                       INCLUDE_DIRS "include"
                       REQUIRES config ql_log esp_timer)
