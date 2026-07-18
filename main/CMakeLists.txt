# main: composition root. Depends on the three top-level modules it starts; their
# transitive requires pull in the rest of the tree (ble_hal -> bt, etc.).
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES ble_events ble_hal ble_task ql_console)
