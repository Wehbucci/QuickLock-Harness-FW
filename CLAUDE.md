# QuickLock Harness — firmware (ESP32-WROOM-32E, ESP-IDF v6.0)

BLE **central / GATT client**. The other half of the system is the nRF52840 **fob**
(BLE peripheral) at `../quicklock-fob-fw/` — a separate repo, usually an additional
working directory in this session. **The two units share no source, only the wire
contract.** A change to one side's protocol behaviour is a change to both.

University of Waterloo capstone. Specs are cited as `F13`, `F22`, `F35` … throughout the
code; those come from `extras/Capstone Design Document.docx.pdf`.

**Scope:** the **BLE communication subsystem**, integrated with the **Security Core**
task that owns the arm/disarm state machine. LED, alarm, and battery tasks exist as
skeletons; IMU/belt detection and scheduling are **not** implemented. `mock_fob/` is
referenced by the docs but deliberately **not built** — the real fob firmware now exists.

## Source of truth, in precedence order

1. **`extras/BLE_CONTRACT.md`** — UUIDs, opcodes, Identity token, security, connection
   parameters. **Wins over everything, including the design docs.** If code and this file
   disagree, the code is the bug. Duplicated byte-for-byte in the fob repo.
2. `extras/HARNESS_BLE_TASK.md` — this unit's design: task placement, state machine,
   proximity, module layout.
3. `docs/ble_subsytem.md` — the real README (build, config, console, logging, TODOs).
   Note the filename typo; `README.md` at the root is the generic ESP-IDF setup guide.
4. `extras/TESTING_WITHOUT_FOB.md` — nRF Connect path, mostly superseded now the fob exists.
5. `extras/TEST_PLAN.md` — bring-up checklist. Shared copy; edit both repos together.

## Read these before answering anything about the link

`ble/ble_contract/include/ble_contract.h` + `ble/config/include/config.h` +
`ble/ble_hal/ble_hal.c`. That's the whole protocol surface. Don't sweep the tree for it.

## Module layout

BLE components are grouped under **`ble/`** (registered via `EXTRA_COMPONENT_DIRS` in the
root `CMakeLists.txt`), keeping the auto-scanned `components/` clean for future subsystems.
Components are referenced **by name, not path**, so `components/<subsystem>` can
`REQUIRES ble_hal`.

```
main/main.c                  app_main: events -> tasks -> HAL -> bridge -> ble_task -> console
ble/ble_contract/            UUIDs as little-endian BYTE LISTS. Stack-agnostic: no NimBLE include.
ble/config/                  EVERY tuning constant. Stack-agnostic.
ble/ql_log/                  per-module TAG over esp_log
ble/proximity/               PURE math: path loss, EMA, hysteresis. No BLE/IDF includes. Has Unity tests.
ble/ble_events/              outbound queue + ble_event_t. No globals.h dependency.
ble/ble_security_bridge/     ble_event_t -> ble_command + notify. The ONLY ble/ user of globals.h.
ble/ble_hal/                 the ONLY NimBLE translation unit
ble/ble_task/                connection state machine + block-on-queue loop
components/ql_console/       BRING-UP ONLY: operator commands on the monitor UART
common/                      globals.h: shared state, task handles, notify helpers
security_core/               owns security_state; consumes ble_command
led/ alarm/ battery_status/  one task each; skeletons
```

**Invariant — verify after touching the HAL:**

```bash
grep -rn "nimble/" ble/ main/ components/   # real #includes only in ble/ble_hal/ble_hal.c
```

`ble_task`'s `CMakeLists.txt` **deliberately does not require `bt`**, and `ble_hal` keeps
`bt` in `PRIV_REQUIRES` — that's what mechanically prevents NimBLE leaking into the state
machine. Don't "fix" either.

## Tasks and cores (design doc Table 4, F35)

| Task | Prio | Core | Created by |
|---|---|---|---|
| NimBLE host (`nimble_host`) | 21 | 0 | `nimble_port_freertos_init()` — not ours |
| **BLE Communication** (`ble_task`) | **3** | **0** | `ble_task_start()` |
| **Security Core** (`security_core_task`) | **5** | **1** | `app_main` |
| `alarm_task` | 4 | 1 | `app_main` |
| `led_task` | 2 | 0 | `app_main` |
| `battery_status_task` | 1 | 0 | `app_main` |
| BLE→Security bridge (`ble_sec_bridge`) | 2 | 1 | `ble_security_bridge_start()` |
| Console REPL | 2 | 1 | `ql_console_start()` — bring-up only |

Controller + host + our BLE task all on **core 0**, so the safety-critical detection/alarm
chain (core 1) is never delayed by the radio (**F35**). Keep new radio-adjacent work off
core 1.

**The bridge's 2/core-1 placement is a correctness constraint, not a preference** — it must
stay on Security's core at a priority *below* Security's. See the next section.

NimBLE GAP/GATT callbacks run on the **host task**, not ours: translate to a
`ble_inbound_msg_t`, post, return. No blocking, no state machine in a callback.

## The two traps (commented where they occur)

1. **UUIDs are little-endian** byte lists (`ble_contract.h`) — the reverse of the string.
   A slip presents as *"the scan never matches"*.
2. **Unit conversions** happen in exactly one commented place, `ble_hal_request_conn_params()`:
   connection interval = 1.25 ms units (1000 ms → 800), supervision timeout = 10 ms units
   (4000 → 400). Constants stay in ms in `config.h`. The BLE spec constraint
   `timeout > (1 + latency) * interval_max * 2` is validated in code.

## How BLE reaches Security (two conventions, one adapter)

The two subsystems were built against different integration styles, and both are kept:

- **BLE side:** `ble_task` posts typed `ble_event_t`s to `ble_events_queue()` and never
  names its consumer. `ble_task` is unchanged by the integration and still runs with no
  Security task present.
- **Security side (`common/globals.h`):** a producer sets the shared `ble_command` global,
  then calls `ble_wake_up_security_task()`. `security_core_task` wakes, reads the global,
  and drives `security_state`.

`ble/ble_security_bridge/` is the single seam between them. It drains the queue and
republishes actionable events as commands:

| BLE event | → `ble_command` |
|---|---|
| `ARM_REQUESTED` | `BLE_ARM` |
| `DISARM_REQUESTED` | `BLE_DISARM` |
| `FOB_OUT_OF_RANGE` (Mechanism B, F14) | `BLE_OOR` |
| `LINK_LOST_SUPERVISION` (Mechanism A, F22) | `BLE_OOR` |
| everything else | *(none — logged only)* |

`FOB_DISCONNECTED` is deliberately **not** mapped: `ble_task` already starts the re-acquire
grace and raises `LINK_LOST_SUPERVISION` if it expires, so acting on both would arm twice
for one departure. `FOB_IN_RANGE` is not mapped either — per the state machine, S0 is
reachable only by an explicit disarm from the fob.

**The `ble_command` race, and why it is closed.** `ble_command` is one unlocked global, so
a second event must not overwrite it before Security reads the first. The bridge runs on
**core 1** (Security's core) at **priority 2**, below Security's **5**, so
`ble_wake_up_security_task()` preempts the bridge immediately and Security consumes the
command before the bridge runs again. Moving the bridge to core 0 or raising its priority
to ≥ Security's reintroduces a lost-command bug that will look like *"the harness
occasionally ignores the fob."* Constants live in `config.h`.

Known deferrals (`// TODO(security-core)`):
- `enum BLE_COMMANDS` has **no SILENCE member**, so the fob's silence opcode (0x03)
  reaches Security as nothing. Do **not** fold it into `BLE_DISARM` — silence must quiet
  the sounder while staying armed.
- `ble_task` writes back the state *implied* by the command (ARM→ARMED) rather than the
  **confirmed** `security_state`, so the fob's LED can lie if Security rejects a command.

## Bench console (bring-up scaffold, not a product feature)

The harness has **no board HAL and no buttons yet**, so `components/ql_console/` puts the
operator inputs on the monitor UART: `pair`, `status`, `bonds`, `bonds clear`,
`rssi <dbm>`, `rssi clear`, `help`.

Before it existed, `ble_task_enter_pairing_mode()` (marked `TODO(ui)`) and
`ble_hal_delete_all_bonds()` had **no caller at all**. `pair` is a placeholder for the real
pairing button — the button will call the same function, so the console doesn't need
removing first.

`rssi <dbm>` overrides the sample at the source so Mechanism B (EMA → hysteresis → events)
is testable at a desk; the real path runs unless explicitly overridden. It logs at
**warning** level and appears in `status` because a forgotten override makes every later
range test lie. `QL_TEST_HOOKS_ENABLED = 0` compiles it out.

## Build / flash / monitor

```bash
. $HOME/.espressif/v6.0/esp-idf/export.sh   # once per shell — idf.py is NOT on PATH
idf.py build
idf.py -p PORT flash monitor                # exit: Ctrl-]
idf.py reconfigure                          # after editing sdkconfig.defaults
```

Builds clean, **zero warnings — keep it that way.** `MINIMAL_BUILD` is ON, so a component
only builds if something `REQUIRES` it (a new component needs adding to `main`'s REQUIRES).

## Hard rules

- **NimBLE, not Bluedroid, not NimBLE-Arduino.** Bluedroid is the IDF default and
  `sdkconfig.defaults` must override it. Pure ESP-IDF C — not Arduino.
- **Targeted IDF v6.0.** Kconfig symbol names and NimBLE signatures shift across releases;
  **verify against the installed headers** (`~/.espressif/v6.0/esp-idf/components/`) rather
  than assuming.
- **No magic numbers.** Anything tunable lives in `ble/config/include/config.h`.
- `sdkconfig` is generated — edit `sdkconfig.defaults`. `CONFIG_BT_NIMBLE_MAX_BONDS` must
  match `MAX_BONDS`.
- **`proximity` must stay pure** (no BLE/IDF includes) — that's what makes it host-testable.
- Just Works gives encryption + ECDH but **not** MITM. The **pairing-mode window**, not
  MITM, is what stops a stranger bonding. Say so honestly; don't overclaim.
- **Log the raw disconnect reason always.** `BLE_HS_HCI_ERR(BLE_ERR_CONN_SPVN_TMO)` =
  `0x0208` is the authoritative "user left" signal (F22).

## Documentation conventions

File header on every file: purpose + the F/N specs it serves. Doc comment on every
non-trivial function. Inline comments explain the **why**, not the what, and cite the spec
(`// F22: fail-secure auto-arm on supervision timeout`). Report risks honestly rather than
working around them quietly.
