idf_component_register(SRCS "imu_hal.c"
                             "imu_detection.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2c esp_driver_gpio common)
