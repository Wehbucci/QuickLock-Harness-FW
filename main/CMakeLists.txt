idf_component_register(SRCS "hello_world_main.c" "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES freertos common led_task alarm_task battery_status_task security_core_task)
