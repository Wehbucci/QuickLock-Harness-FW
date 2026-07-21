# ble_events: outbound event queue to the Security task. Pure FreeRTOS plumbing;
# needs config for the queue depth and ql_log for logging. Deliberately does NOT
# require `common` — the globals.h coupling lives in ble_security_bridge, which
# is what keeps this module reusable and the BLE task independent of Security.
idf_component_register(SRCS "ble_events.c"
                       INCLUDE_DIRS "include"
                       REQUIRES config ql_log)
