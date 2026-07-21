# main: composition root. Depends on every module it starts directly; their
# transitive requires pull in the rest of the tree (ble_hal -> bt, led_task ->
# common, etc.). MINIMAL_BUILD is ON, so anything missing from this list simply
# does not get built.
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES freertos
                                common
                                ql_log
                                imu
                                led_task
                                alarm_task
                                battery_status_task
                                security_core_task
                                belt_detection_task
                                ble_events
                                ble_security_bridge
                                ble_hal
                                ble_task
                                ql_console
                                esp_driver_gpio)
