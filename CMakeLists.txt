# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Task components live in their own folders adjacent to main/. Each of these
# directories contains one component (e.g. led/led_task/), so list the parent
# folders here; ESP-IDF searches one level down for the CMakeLists.txt.
set(EXTRA_COMPONENT_DIRS
    common
    led
    alarm
    battery_status
    security_core)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(QuickLock-Harness-FW)
