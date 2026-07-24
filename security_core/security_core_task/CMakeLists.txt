idf_component_register(SRCS "security_core_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common freertos
                       PRIV_REQUIRES ql_log imu)
