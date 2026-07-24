/*
 * ql_console_bench_hooks.c — desk bring-up commands for testing with only the
 * ESP32 + IMU on the bench: no fob, no belt sensor. `arm`/`disarm` fake the
 * BLE command a real fob would produce (same globals.h calls the BLE->Security
 * bridge makes), and `secstatus` snapshots the state that's otherwise only
 * visible by reading code. Nothing here is product behavior — it's a second,
 * removable translation unit so it's easy to leave out of a commit; ql_console.c
 * only forward-declares and calls ql_console_register_bench_hooks().
 *
 * Gated by the same QL_TEST_HOOKS_ENABLED flag as the `rssi` hook in
 * ql_console.c (config.h) so a product build can compile it out entirely.
 */

#include "config.h"

#if QL_TEST_HOOKS_ENABLED

#include <stddef.h>
#include "esp_console.h"
#include "globals.h"
#include "ql_log.h"

QL_LOG_TAG("ql_bench");

static int cmd_arm(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ble_command = BLE_ARM;
    ble_wake_up_security_task();
    QL_LOGW("bench hook: fake BLE_ARM sent (no fob involved)");
    return 0;
}

static int cmd_disarm(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ble_command = BLE_DISARM;
    ble_wake_up_security_task();
    QL_LOGW("bench hook: fake BLE_DISARM sent (no fob involved)");
    return 0;
}

static const char *security_state_name(enum SECURITY_STATE s)
{
    switch (s) {
    case SECURITY_DISARMED:    return "DISARMED";
    case SECURITY_ARMED_QUIET: return "ARMED_QUIET";
    case SECURITY_ARMED_TIER2: return "ARMED_TIER2";
    case SECURITY_ARMED_TIER3: return "ARMED_TIER3";
    default:                   return "?";
    }
}

static const char *imu_command_name(enum IMU_COMMANDS c)
{
    switch (c) {
    case IMU_NO_COMMAND:               return "NO_COMMAND";
    case IMU_QUIET_TO_TIER2:           return "QUIET_TO_TIER2";
    case IMU_QUIET_TO_TIER3:           return "QUIET_TO_TIER3";
    case IMU_TIER2_MOVEMENT_SUSTAINED: return "TIER2_MOVEMENT_SUSTAINED";
    case IMU_TIER2_TO_TIER3:           return "TIER2_TO_TIER3";
    case IMU_TIER3_MOVEMENT_DETECTED:  return "TIER3_MOVEMENT_DETECTED";
    default:                           return "?";
    }
}

static const char *belt_state_name(enum BELT_STATE b)
{
    switch (b) {
    case BELT_OPEN:    return "OPEN";
    case BELT_CLOSED:  return "CLOSED";
    case BELT_UNKNOWN: return "UNKNOWN";
    default:           return "?";
    }
}

static int cmd_secstatus(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    QL_LOGI("security_state=%s | imu_command=%s | belt_state=%s",
            security_state_name(security_state),
            imu_command_name(imu_command),
            belt_state_name(belt_state));
    return 0;
}

static const esp_console_cmd_t s_bench_cmds[] = {
    {
        .command = "arm",
        .help = "Bench hook: fake a BLE_ARM command with no fob present",
        .hint = NULL,
        .func = cmd_arm,
    },
    {
        .command = "disarm",
        .help = "Bench hook: fake a BLE_DISARM command with no fob present",
        .hint = NULL,
        .func = cmd_disarm,
    },
    {
        .command = "secstatus",
        .help = "Bench hook: print security_state / imu_command / belt_state",
        .hint = NULL,
        .func = cmd_secstatus,
    },
};

esp_err_t ql_console_register_bench_hooks(void)
{
    for (size_t i = 0; i < sizeof(s_bench_cmds) / sizeof(s_bench_cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&s_bench_cmds[i]);
        if (err != ESP_OK) {
            QL_LOGE("register '%s' failed; %s", s_bench_cmds[i].command, esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

#else /* !QL_TEST_HOOKS_ENABLED */

#include "esp_err.h"

esp_err_t ql_console_register_bench_hooks(void)
{
    return ESP_OK;
}

#endif
