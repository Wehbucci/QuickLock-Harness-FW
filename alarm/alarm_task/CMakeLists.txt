idf_component_register(SRCS "alarm_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common freertos esp_driver_gpio esp_driver_ledc
                       PRIV_REQUIRES ql_log)