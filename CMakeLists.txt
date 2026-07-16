# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# BLE subsystem components are grouped under ble/ (kept out of the auto-scanned
# components/ dir so future subsystems — Security, Detection, Alarm, LED, … — can
# be dropped into components/ or their own group folders without clutter). Each
# subdir of ble/ is a normal IDF component; they are still referenced by name in
# REQUIRES, so a future components/<subsystem> can depend on ble_hal, ql_log, etc.
# Add more groups here as they appear, e.g. set(EXTRA_COMPONENT_DIRS ble security).
set(EXTRA_COMPONENT_DIRS ble)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(QuickLock-Harness-FW)
