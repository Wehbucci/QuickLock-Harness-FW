# ql_log: header-only per-module logging convention over esp_log.
# Publicly requires 'log' so any consumer inherits esp_log.h.
idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES log)
