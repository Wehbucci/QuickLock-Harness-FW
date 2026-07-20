idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES freertos common esp_driver_gpio led_task alarm_task battery_status_task security_core_task belt_detection_task)