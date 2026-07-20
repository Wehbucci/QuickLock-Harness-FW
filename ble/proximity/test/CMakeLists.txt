# Unity tests for the pure proximity module. Registered as a test component so a
# unity-test runner can pick it up, e.g. from an IDF unity-test-app:
#     idf.py -T proximity build flash monitor
# Because proximity.[ch] is dependency-free, the tests can also be compiled and
# run natively on a host (cc proximity.c test_proximity.c) with a Unity shim.
idf_component_register(SRCS "test_proximity.c"
                       INCLUDE_DIRS "."
                       REQUIRES unity proximity)
