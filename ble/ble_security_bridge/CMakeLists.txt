# ble_security_bridge: adapter from the BLE event queue to the Security Core
# task's global + notification protocol. Requires `common` for globals.h — this
# is the ONLY component under ble/ that does, which is what keeps the rest of the
# BLE subsystem independent of the project-wide shared state.
idf_component_register(SRCS "ble_security_bridge.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ble_events common
                       PRIV_REQUIRES config ql_log)
