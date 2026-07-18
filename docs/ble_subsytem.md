# QuickLock Harness — BLE Communication Subsystem

Central-side BLE firmware for the QuickLock harness (the ESP32-WROOM-32E main
unit). The harness is the **BLE central / GATT client**; the keychain fob is the
peripheral/GATT server. This project implements **only the BLE communication
task** and its decoupled event interface to the (not-yet-built) Security task.

- **Environment:** pure ESP-IDF, C, the **NimBLE host bundled with ESP-IDF**
  (native C API — not NimBLE-Arduino, not Bluedroid).
- **Targeted IDF version:** **v6.0.0** (`idf.py --version` → `ESP-IDF v6.0`). All
  NimBLE signatures and Kconfig symbol names were verified against this version.
- **Target chip:** ESP32 (dual-core, `esp32`).

The wire-level contract, the task design, and the verification plan live in
`extras/BLE_CONTRACT.md`, `extras/HARNESS_BLE_TASK.md`, and
`extras/TESTING_WITHOUT_FOB.md`. This README covers building, configuring, and
running.

> **Scope note.** Detection, alarm, LED, battery, and scheduling subsystems are
> **not** implemented. The mock peripheral (`mock_fob/`) is intentionally **not**
> built yet — the focus here is the real harness. See *Verifying without the fob*
> for how to exercise this with nRF Connect in the meantime.

---

## Module layout

Layered, one responsibility per module. Application logic never calls NimBLE or
touches GPIO directly — it goes through the HAL. The rule that enforces this:

```
grep -rn "nimble/" ble/     # matches ONLY ble/ble_hal/ble_hal.c
```

All BLE components are grouped under a top-level **`ble/`** folder (registered via
`EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`), keeping the auto-scanned
`components/` dir and `main/` clean for the subsystems added later (Security,
Detection, Alarm, LED, …). Components are referenced by **name**, not path, so a
future `components/<subsystem>` can still `REQUIRES ble_hal` / `ql_log`.

```
QuickLock-Harness-FW/
  CMakeLists.txt                 project: set(EXTRA_COMPONENT_DIRS ble) + project()
  sdkconfig.defaults             NimBLE on, central+observer, SC+bonding, NVS bonds, 2 bonds
  main/
    main.c                       app_main: events -> HAL -> task, in order
  ble/                           <-- BLE subsystem group (EXTRA_COMPONENT_DIRS)
    ble_contract/include/ble_contract.h   UUIDs (LE bytes), opcodes, Identity token — STACK-AGNOSTIC
    config/include/config.h                all tuning constants (section 7), no magic numbers
    ql_log/include/ql_log.h                per-module TAG + ESP_LOG wrappers
    proximity/  proximity.[ch] + test/     PURE math: path-loss, EMA, hysteresis (no BLE/IDF)
    ble_events/ ble_events.[ch]            outbound queue + ble_event_t + stub Security consumer
    ble_hal/    ble_hal.[ch]               the ONLY NimBLE translation unit
    ble_task/   ble_task.[ch]              connection state machine + block-on-queue loop
  components/                    (future subsystems land here — auto-scanned)
    ql_console/ ql_console.[ch]            BRING-UP ONLY: operator commands on the monitor UART
```

`ql_console` is the first tenant of `components/`, and it is a live check that
the layout claim above holds: it depends on the `ble/` group **by name**
(`PRIV_REQUIRES ble_hal ble_task`), exactly as Security/Detection/Alarm/LED will.

Add future groups the same way, e.g. `set(EXTRA_COMPONENT_DIRS ble security)`.

**Layering:** `ble_task` (decide) -> `ble_hal` (act, NimBLE) and `ble_events`
(wake Security). `proximity` is pure and shared. `ble_contract`/`config`/`ql_log`
are header-only. `ble_task`'s `CMakeLists.txt` deliberately does **not** require
`bt`, so NimBLE cannot leak into the state machine.

### Event-driven, not polling

The BLE task blocks on its inbound FreeRTOS queue for up to `HOUSEKEEP_MS`
(20 ms). NimBLE GAP/GATT callbacks run on the **NimBLE host task**; they do the
minimum — translate the event into a small `ble_inbound_msg_t` and post it,
which unblocks our task immediately. On the timeout tick the task runs
housekeeping (sample+filter RSSI, tick the pairing window and re-acquire grace).
Outbound, the task posts `ble_event_t`s to the Security queue; it never calls
Security directly.

---

## Connection state machine

```
IDLE -> SCANNING -> CONNECTING -> BONDING -> VERIFYING_IDENTITY -> CONNECTED
                                                                       |
                                        (disconnect / supervision timeout)
                                                                       v
                                                                   SCANNING
```

- **BONDING** runs `ble_gap_security_initiate()` — re-encrypts with stored keys
  for a known bond, or forms a new LE Secure Connections bond (only inside the
  pairing window).
- **VERIFYING_IDENTITY** discovers the service/characteristics/CCCD and reads the
  Identity token *over the encrypted link*; a mismatch drops the peer (F15/F36).
- **CONNECTED** subscribes to Command notifications and samples RSSI (~1 Hz).

Target: SCANNING -> CONNECTED within 3 s on a known bond (F24) — the actual time
is logged on every connect.

---

## Task / core placement (design doc Table 4, F35)

| Task | Priority | Core | Stack | Created by |
|---|---|---|---|---|
| NimBLE host (`nimble_host`) | `configMAX_PRIORITIES-4` (21 by default) | 0 | 4096 B | `nimble_port_freertos_init()` |
| **BLE Communication** (`ble_task`) | **3** | **0** | 4096 words | `ble_task_start()` |
| Stub Security consumer (`sec_stub`) | 2 | 1 | 3072 B | `ble_events_start_stub_consumer()` |
| Console REPL (`console_repl`) | 2 | 1 | 4096 B | `ql_console_start()` (bring-up only) |

The controller, NimBLE host, and our BLE task all live on **core 0** so the
safety-critical detection/alarm chain (core 1) is never delayed by the radio
(F35). The stub consumer sits on core 1 to mimic where the real Security task
will run. Host-task priority/stack are the IDF defaults, noted here because the
F35 argument depends on knowing what runs on core 0.

---

## Required menuconfig / sdkconfig options

All are set in `sdkconfig.defaults` (applied on a fresh `sdkconfig`). Symbol
names are the IDF **v6.0** names — they have shifted across releases, so verify
if you move IDF versions.

| Option | Value | Why |
|---|---|---|
| `CONFIG_BT_ENABLED` | `y` | Bluetooth on |
| `CONFIG_BT_NIMBLE_ENABLED` | `y` | **NimBLE host, not Bluedroid** (Bluedroid is the IDF default — must override) |
| `CONFIG_BT_NIMBLE_ROLE_CENTRAL` | `y` | harness is central |
| `CONFIG_BT_NIMBLE_ROLE_OBSERVER` | `y` | scanning |
| `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL` | `n` | harness never advertises |
| `CONFIG_BT_NIMBLE_ROLE_BROADCASTER` | `n` | — |
| `CONFIG_BT_NIMBLE_GATT_CLIENT` | `y` | GATT client |
| `CONFIG_BT_NIMBLE_SM_SC` | `y` | LE Secure Connections (ECDH+AES), F36 |
| `CONFIG_BT_NIMBLE_SECURITY_ENABLE` | `y` | Security Manager |
| `CONFIG_BT_NIMBLE_NVS_PERSIST` | `y` | **bonds survive power loss** (F18) |
| `CONFIG_BT_NIMBLE_MAX_BONDS` | `2` | 2 stored bonds (F16); matches `MAX_BONDS` in `config.h` |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` | `1` | one fob |
| `CONFIG_BT_NIMBLE_PINNED_TO_CORE_0` | `y` | host task on core 0 (F35) |
| `CONFIG_ESPTOOLPY_FLASHSIZE_4MB` + `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE` | — | NimBLE+app exceeds the default 2 MB single-app table |

Just Works (no MITM) is selected in **code**, not Kconfig, in `ble_hal.c`:
`ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT`, `sm_mitm = 0`,
`sm_bonding = 1`, `sm_sc = 1`.

---

## Build / flash / monitor

```bash
# one-time per shell: load the IDF v6.0 environment
. $HOME/.espressif/v6.0/esp-idf/export.sh      # or your IDF_PATH/export.sh

cd QuickLock-Harness-FW
idf.py set-target esp32          # only needed once / after a full clean
idf.py build
idf.py -p /dev/tty.usbserial-XXXX flash monitor    # your serial port
```

Exit the monitor with `Ctrl-]`. A fresh checkout regenerates `sdkconfig` from
`sdkconfig.defaults`; run `idf.py reconfigure` after changing the defaults.

To **force re-pairing** (wipe stored bonds): type `bonds clear` then `pair` at
the console below. (`idf.py -p PORT erase-flash` still works and wipes more.)
The harness also auto-opens the pairing window on first boot when no bond is
stored.

---

## The bring-up console

The harness has no board HAL and no buttons yet, so `components/ql_console`
puts the operator inputs on the UART `idf.py monitor` is already showing. Type
a command and press Enter at the `quicklock>` prompt:

| Command | Does |
|---|---|
| `pair` | Open the pairing window (`PAIRING_WINDOW_MS`) so a **new** fob may bond (F15) |
| `status` | State machine, connection handle, bond count, pairing window, filtered RSSI |
| `bonds` | How many bonds are stored |
| `bonds clear` | Delete all stored bonds — forces a re-pair (F15/F18) |
| `rssi <dbm>` | Force the RSSI sample, e.g. `rssi -80` — test hook, see below |
| `rssi clear` | Go back to the real radio RSSI |
| `help` | List the commands |

Until this existed, three capabilities the firmware **already implemented** had
no caller and could not be reached from outside: `ble_task_enter_pairing_mode()`
(which carried a `TODO(ui)` for exactly this), `ble_hal_delete_all_bonds()`, and
the proximity decision path. `pair` is a placeholder for the real UI, not a
replacement for it — when the pairing button lands, it becomes one more caller
of the same `ble_task` API.

**On `rssi <dbm>`.** Mechanism B (F14) is an EMA plus a hysteresis band feeding
two events. Testing it by walking away also tests `RSSI_C_DBM` / `RSSI_N`, which
are still **uncalibrated defaults** — so a failure is ambiguous, and you cannot
tell a broken threshold from an untuned constant. Injecting the sample makes the
decision logic deterministic at a desk; the injected value still flows through
the real filter and the real thresholds. It does **not** replace the walk test,
which is the only thing that validates the constants. The override logs at
**warning** level and shows up in `status`, because a forgotten override would
make every later range test lie. Set `QL_TEST_HOOKS_ENABLED` to `0` in
`config.h` for a production build and the override cannot exist at runtime.

> The REPL prompt and the log share one UART, so log lines will scroll over the
> prompt. That is expected — press Enter to redraw it.

**A full step-by-step bring-up plan — both firmwares, every command, every
expected log line, with tick-boxes — is `extras/TEST_PLAN.md`.**

### Proximity unit tests

The `proximity` module is dependency-free, so its Unity tests
(`components/proximity/test/`) run under an IDF unity-test-app
(`idf.py -T proximity build flash monitor`) **or** compile natively on a host
(`cc proximity.c test_proximity.c` with a Unity shim). The pure math was
validated on-host during development.

---

## Verifying without the fob (nRF Connect path)

Everything you need is in `idf.py monitor`. Full procedure and acceptance
criteria: `extras/TESTING_WITHOUT_FOB.md`. Quick version:

1. In **nRF Connect for Mobile**, configure the local GATT server:
   - Service `6b2f0001-9a3c-4b7e-8f21-0a4d2c9e1f30`
   - Char `6b2f0002-...` -> Notify + Write
   - Char `6b2f0003-...` -> Read, value =
     `51 55 49 43 4B 4C 4F 43 4B 5F 46 4F 42 5F 76 31` ("QUICKLOCK_FOB_v1")
2. **Advertise** with the QuickLock **service UUID in the advertisement data**
   (not only the scan response — the harness filters its scan on that UUID).
3. `idf.py -p PORT flash monitor`. First boot auto-opens the pairing window;
   accept the phone's pairing prompt. Watch the log go
   `SCANNING -> CONNECTING -> BONDING -> VERIFYING_IDENTITY -> CONNECTED`.
4. From the app, send a **notification** on `6b2f0002` with `0x01`/`0x02`/`0x03`.
   The log shows the opcode, the event posted to the Security queue, and the
   harness writing back the state byte (`0x01`/`0x00`).
5. Confirm the **Identity** read logs a token match.
6. **Out-of-range (B):** walk the phone away; filtered RSSI crosses
   `OUT_THRESHOLD_DBM` -> `FOB_OUT_OF_RANGE`; walk back -> `FOB_IN_RANGE`.
7. **Supervision timeout (A):** turn off the phone's Bluetooth; the disconnect
   logs its **raw reason**, and `LINK_LOST_SUPERVISION` is raised after the grace
   window.

**Honest caveats (see the contract):** Just Works gives encryption + ECDH but
**not** MITM authentication — the pairing-mode window, not MITM, is what stops a
stranger bonding their own fob. A phone often will **not** honour the requested
connection interval / supervision timeout, so Mechanism-A *timing* and enforced
encryption on the Identity read are only fully trustworthy against the ESP-IDF
mock (`mock_fob/`, deferred) or the real fob — not nRF Connect.

---

## Serial logging

Per-module `TAG` via `esp_log.h`. You will see: every state transition, scan
match (addr+RSSI), connect status, encryption result (encrypted/authenticated/
bonded), each discovered characteristic + handle, the CCCD handle, every received
Command opcode, every state write-back ACK, **raw AND filtered RSSI per sample**
(with an estimated distance), every disconnect **with its raw reason code**, and
every event posted to the Security queue.

---

## Integration hooks / TODOs for other subsystems

- **Security Core task** — replace the stub consumer in `ble_events.c`
  (`ble_events_start_stub_consumer`) with the real task receiving on
  `ble_events_queue()`. The queue + `ble_event_t` enum are the integration
  contract; the BLE task does not change. (`// TODO(security-core)` markers in
  `ble_events.c`, `main.c`, `ble_task.c`.)
- **State write-back source** — the BLE task currently writes back the state
  *implied* by the command (ARM->ARMED, DISARM->DISARMED). In the full system the
  **confirmed** state should come from the Security task. (`// TODO(security-core)`
  in `ble_task.c: handle_command`.)
- **Pairing button** — `ble_task_enter_pairing_mode()` is wired to the console's
  `pair` command; a physical button via a board HAL is still to come, and will
  call the same function. (`// TODO(ui)`.)
- **Fob Battery characteristic (F29)** — deferred; `// TODO(F29)` marker in
  `ble_contract.h`.
- **LED + tone arm/disarm confirmation (F17)** — belongs to the LED/Alarm tasks;
  the BLE task only writes the confirmation byte to the fob.

---

## Assumptions

- **Bond ↔ address matching:** advertisers may use resolvable private addresses,
  and IRK-based RPA resolution is out of scope for v1. The harness connects when
  a bond exists **or** the pairing window is open, then relies on link encryption
  + the Identity read to reject impostors (F15). Documented in `ble_task.c`.
- **Pairing-window gate:** enforced at the scan/connect decision — the harness
  never connects to an *unbonded* peer outside the window, so it never forms a
  bond outside the window (BLE_CONTRACT.md section 5).
- **Hard-trigger policy:** on a supervision-timeout disconnect the BLE task
  always starts the re-acquire grace and raises `LINK_LOST_SUPERVISION` if the
  fob does not return; the "only auto-arm if disarmed" policy belongs to the
  Security task that consumes the event.
- **Connection parameters** are *requested* after connect via a param-update
  (`ble_gap_update_params`); the peripheral may negotiate or ignore them.
- ESP32-WROOM-32E has ≥4 MB flash (assumed by the flash-size/partition defaults).
