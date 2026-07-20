# proximity: pure RSSI-to-range math. No BLE/IDF deps by design, so no REQUIRES.
# (Links libm via the toolchain for log10f/powf.)
idf_component_register(SRCS "proximity.c"
                       INCLUDE_DIRS "include")
