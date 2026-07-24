/*
 * ql_console.c — serial test console implementation. See ql_console.h.
 *
 * Every command here is a thin call into API the firmware already exposed; the
 * console adds no policy and holds no state of its own. It includes no NimBLE
 * headers (it reaches the radio only through ble_hal / ble_task), so it does
 * not weaken the `grep -rn "nimble/" ` invariant.
 *
 * Lives in components/ rather than ble/ on purpose: it is an operator UI, not
 * part of the BLE subsystem. It is also the first thing to prove that a
 * components/<subsystem> can depend on the ble/ group by NAME, which is the
 * layout the README promises for Security, Detection, Alarm, and LED.
 */

#include "ql_console.h"

#include "ble_hal.h"
#include "ble_task.h"
#include "config.h"
#include "ql_log.h"

#include <stdlib.h>
#include <string.h>
#include "esp_console.h"

QL_LOG_TAG("ql_console");

/* Bench-only fake arm/disarm + status commands, defined in
 * ql_console_bench_hooks.c so that file can be left out of a commit without
 * touching this one beyond this declaration and the registration call below. */
extern esp_err_t ql_console_register_bench_hooks(void);

/* -------------------------------------------------------------------------- */
/* Commands                                                                    */
/* -------------------------------------------------------------------------- */

static int cmd_pair(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ble_task_enter_pairing_mode();
    /* The request is a flag the BLE task picks up on its next housekeeping
     * tick, so the window opens a few ms from now rather than inside this
     * call — say so, or the log ordering looks like a bug. */
    QL_LOGI("pairing requested; window opens within %d ms and lasts %d ms",
            HOUSEKEEP_MS, PAIRING_WINDOW_MS);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ble_task_log_status();
    return 0;
}

static int cmd_bonds(int argc, char **argv)
{
    if (argc == 1) {
        QL_LOGI("%d bond(s) stored (max %d)", ble_hal_bond_count(), MAX_BONDS);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        ble_hal_delete_all_bonds();
        QL_LOGW("all bonds cleared; the harness will now refuse every peer "
                "until you open a pairing window (`pair`)");
        return 0;
    }
    QL_LOGE("usage: bonds [clear]");
    return 1;
}

static int cmd_rssi(int argc, char **argv)
{
#if QL_TEST_HOOKS_ENABLED
    if (argc != 2) {
        QL_LOGE("usage: rssi <dbm> | rssi clear   (e.g. `rssi -80`)");
        return 1;
    }
    if (strcmp(argv[1], "clear") == 0) {
        ble_task_clear_rssi_override();
        return 0;
    }

    char *end = NULL;
    long v = strtol(argv[1], &end, 10);
    /* Reject anything that is not a plausible link RSSI up front. Passing a
     * positive value through would just be dropped by proximity_rssi_is_valid()
     * deep in housekeeping, leaving the user staring at a console that accepted
     * the command and a filter that never moved. */
    if (end == argv[1] || *end != '\0' || v >= 0 || v < -127) {
        QL_LOGE("bad RSSI '%s': want a negative dBm value in -127..-1", argv[1]);
        return 1;
    }
    ble_task_inject_rssi((int8_t)v);
    return 0;
#else
    (void)argc;
    (void)argv;
    QL_LOGE("rssi override not compiled in (config.h QL_TEST_HOOKS_ENABLED == 0)");
    return 1;
#endif
}

/* -------------------------------------------------------------------------- */
/* Registration                                                                */
/* -------------------------------------------------------------------------- */

static const esp_console_cmd_t s_cmds[] = {
    {
        .command = "pair",
        .help = "Open the pairing window so a NEW fob may bond (F15)",
        .hint = NULL,
        .func = cmd_pair,
    },
    {
        .command = "status",
        .help = "Show state machine, connection, bonds, pairing window, RSSI",
        .hint = NULL,
        .func = cmd_status,
    },
    {
        .command = "bonds",
        .help = "Show stored bond count; `bonds clear` deletes them all",
        .hint = " [clear]",
        .func = cmd_bonds,
    },
    {
        .command = "rssi",
        .help = "Force the RSSI sample for range tests; `rssi clear` to stop",
        .hint = " <dbm>|clear",
        .func = cmd_rssi,
    },
};

esp_err_t ql_console_start(void)
{
    esp_console_repl_t *repl = NULL;

    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "quicklock>";
    repl_cfg.max_cmdline_length = 64;
    repl_cfg.task_priority = QL_CONSOLE_TASK_PRIO;
    repl_cfg.task_core_id = QL_CONSOLE_TASK_CORE;   /* core 1: keep off the radio (F35) */
    repl_cfg.task_stack_size = QL_CONSOLE_TASK_STACK;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        QL_LOGE("esp_console_new_repl_uart failed; %s", esp_err_to_name(err));
        return err;
    }

    err = esp_console_register_help_command();
    if (err != ESP_OK) {
        QL_LOGE("register help command failed; %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < sizeof(s_cmds) / sizeof(s_cmds[0]); i++) {
        err = esp_console_cmd_register(&s_cmds[i]);
        if (err != ESP_OK) {
            QL_LOGE("register '%s' failed; %s", s_cmds[i].command,
                    esp_err_to_name(err));
            return err;
        }
    }

    err = ql_console_register_bench_hooks();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        QL_LOGE("esp_console_start_repl failed; %s", esp_err_to_name(err));
        return err;
    }

    QL_LOGI("test console ready on the monitor UART -- type `help`");
    return ESP_OK;
}
