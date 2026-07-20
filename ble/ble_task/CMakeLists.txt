# ble_task: application state machine. Notably does NOT require 'bt' — it reaches
# the radio only through ble_hal, which keeps NimBLE out of this translation unit
# (the grep -r "nimble/" invariant).
idf_component_register(SRCS "ble_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ble_hal ble_events
                       PRIV_REQUIRES config ble_contract proximity ql_log esp_timer)
