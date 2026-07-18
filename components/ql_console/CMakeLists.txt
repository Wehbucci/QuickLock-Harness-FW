# ql_console: bring-up operator UI over the monitor UART.
#
# The first tenant of components/ — and a live check that the README's layout
# claim holds: a components/<subsystem> can depend on the ble/ group by NAME
# (not path), exactly as Security/Detection/Alarm/LED will need to.
#
#   REQUIRES      — console: esp_console_* appears in this component's headers'
#                   sibling .c only, but the REPL types come from it; keeping it
#                   public costs nothing and keeps the dependency obvious.
#   PRIV_REQUIRES — impl-only: the two modules the commands drive, plus the
#                   header-only helpers. NimBLE is NOT here and must not be:
#                   the console reaches the radio only through ble_hal.
idf_component_register(SRCS "ql_console.c"
                       INCLUDE_DIRS "include"
                       REQUIRES console
                       PRIV_REQUIRES ble_hal ble_task config ql_log)
