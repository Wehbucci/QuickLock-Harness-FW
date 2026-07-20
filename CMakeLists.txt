# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Component groups. Each entry is a folder holding its components one level down
# (e.g. led/led_task/), which is where ESP-IDF looks for the nested
# CMakeLists.txt. Grouping keeps the auto-scanned components/ dir clean.
#
#   ble             — the BLE communication subsystem (see CLAUDE.md)
#   common          — globals.h: cross-task shared state, handles, notify helpers
#   led/alarm/...   — one task per folder
#
# Components are referenced BY NAME in REQUIRES, not by path, so a component in
# one group can depend on one in another (ble_security_bridge REQUIRES common).
set(EXTRA_COMPONENT_DIRS
    ble
    common
    led
    alarm
    battery_status
    security_core
    belt_detection)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(QuickLock-Harness-FW)
