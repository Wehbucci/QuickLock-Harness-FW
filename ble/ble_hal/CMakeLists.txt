# ble_hal: the ONLY component that links NimBLE.
#   REQUIRES      — freertos: ble_hal.h exposes a QueueHandle_t in its API.
#   PRIV_REQUIRES — impl-only deps: bt (NimBLE host), nvs_flash (bond storage),
#                   and the header-only helpers. Keeping bt private means
#                   ble_task inherits ble_hal.h WITHOUT inheriting NimBLE.
idf_component_register(SRCS "ble_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos
                       PRIV_REQUIRES bt nvs_flash config ble_contract ql_log)
