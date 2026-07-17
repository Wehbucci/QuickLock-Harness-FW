idf_component_register(SRCS "led_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common freertos esp_driver_gpio)
