idf_component_register(SRCS "belt_detection_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common freertos esp_driver_gpio esp_rom esp_timer
                       PRIV_REQUIRES ql_log)