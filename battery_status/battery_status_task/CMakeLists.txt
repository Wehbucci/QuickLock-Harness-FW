idf_component_register(SRCS "battery_status_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common freertos
                       PRIV_REQUIRES ql_log)
