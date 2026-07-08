# QuickLock Harness Firmware

Firmware test harness for the QuickLock capstone project, targeting the
**Espressif ESP32** (dual-core Xtensa LX6) and built with **ESP-IDF v6.0**.

The current `main` firmware is a dual-core FreeRTOS "hello world" that pins one
task to each CPU core and prints which core it is running on — a minimal
starting point for the harness.

This README is a complete, from-scratch setup guide for **macOS** and
**Windows**: it walks you from cloning the repo through building, flashing, and
monitoring the board. If you have never touched ESP-IDF before, follow it
top to bottom.

---

## Table of contents

1. [What you need (prerequisites)](#1-what-you-need-prerequisites)
2. [Install the base tools (Git, VS Code, Python)](#2-install-the-base-tools-git-vs-code-python)
3. [Clone this repository](#3-clone-this-repository)
4. [Install ESP-IDF](#4-install-esp-idf)
   - [Option A — VS Code ESP-IDF extension (recommended, cross-platform)](#option-a--vs-code-esp-idf-extension-recommended-cross-platform)
   - [Option B — Command line (macOS)](#option-b--command-line-macos)
   - [Option C — Command line (Windows)](#option-c--command-line-windows)
5. [Connect the board and find its serial port](#5-connect-the-board-and-find-its-serial-port)
6. [Build](#6-build)
7. [Flash](#7-flash)
8. [Monitor](#8-monitor)
9. [Do it all in one command](#9-do-it-all-in-one-command)
10. [A note on `.vscode/settings.json`](#10-a-note-on-vscodesettingsjson)
11. [Troubleshooting](#11-troubleshooting)
12. [Command cheat sheet](#12-command-cheat-sheet)
13. [Further reading](#13-further-reading)

---

## 1. What you need (prerequisites)

**Hardware**

- An **ESP32 development board** (e.g. ESP32-DevKitC, ESP32-WROVER-KIT, or any
  ESP32-WROOM/WROVER board).
- A **USB cable** that supports data (not charge-only). Most ESP32 devkits use
  USB-A ↔ micro-USB B or USB-A ↔ USB-C.

**Software** (installed in the steps below)

- Git
- Visual Studio Code (recommended editor)
- Python 3.9 or newer (ESP-IDF v6.0 supports up to Python 3.14)
- ESP-IDF v6.0 and its toolchain

**Disk / time budget:** ESP-IDF plus the toolchain is roughly **2–3 GB** and the
first install takes 10–30 minutes depending on your connection.

> **Reference:** Official ESP-IDF v6.0 Getting Started for ESP32 —
> <https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/get-started/index.html>

---

## 2. Install the base tools (Git, VS Code, Python)

### macOS

1. **Xcode Command Line Tools** (provides `git`, `make`, compilers):
   ```sh
   xcode-select --install
   ```
2. **Homebrew** (package manager) — if you don't already have it, see
   <https://brew.sh>. Then:
   ```sh
   brew install git python3 cmake ninja dfu-util
   ```
   > ESP-IDF ships its own copy of CMake/Ninja, but having them system-wide is
   > convenient and harmless.
3. **Visual Studio Code:** <https://code.visualstudio.com/>

### Windows

1. **Git for Windows:** <https://git-scm.com/download/win> (accept the defaults;
   this also installs *Git Bash*).
2. **Visual Studio Code:** <https://code.visualstudio.com/>
3. **Python** is bundled by the ESP-IDF Windows installer in
   [Option C](#option-c--command-line-windows), so you do **not** need to install
   it separately. (If you install it yourself anyway, get it from
   <https://www.python.org/downloads/windows/> and tick *"Add python.exe to PATH"*.)

---

## 3. Clone this repository

Pick a folder without spaces or special characters in the path (ESP-IDF is
happier that way).

**HTTPS** (easiest):
```sh
git clone https://github.com/Wehbucci/QuickLock-Harness-FW.git
cd QuickLock-Harness-FW
```

**SSH** (if you have GitHub SSH keys set up):
```sh
git clone git@github.com:Wehbucci/QuickLock-Harness-FW.git
cd QuickLock-Harness-FW
```

> On **Windows**, run these in *Git Bash*, *PowerShell*, or *CMD*. Cloning into a
> deep path such as `C:\Users\you\Documents\...` is fine, but avoid OneDrive-synced
> folders — the constant syncing can corrupt the `build/` directory.

---

## 4. Install ESP-IDF

You have three routes. **Everyone should start with Option A** — the VS Code
extension is the least error-prone and works identically on macOS and Windows.
Options B and C give you a terminal-only workflow if you prefer the command line
or need CI-style builds.

> All three routes install the same thing: the ESP-IDF v6.0 framework plus its
> toolchain, under `~/.espressif` (macOS/Linux) or `%USERPROFILE%\.espressif`
> (Windows). ESP-IDF v6.0's official installer is the **ESP-IDF Installation
> Manager (EIM)**; the VS Code extension and the standalone installers all use it
> under the hood.

### Option A — VS Code ESP-IDF extension (recommended, cross-platform)

This is the easiest path and gives you clickable **Build / Flash / Monitor**
buttons.

1. Open VS Code.
2. Open the **Extensions** view (`Ctrl+Shift+X` / `Cmd+Shift+X`), search for
   **"ESP-IDF"**, and install the official **Espressif IDF** extension
   (publisher: *Espressif Systems*).
   Marketplace link:
   <https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension>
3. Open the **Command Palette** (`Ctrl+Shift+P` / `Cmd+Shift+P`) and run
   **`ESP-IDF: Open ESP-IDF Installation Manager`** (older versions:
   **`ESP-IDF: Configure ESP-IDF Extension`**).
4. Choose **Express** setup:
   - **ESP-IDF version:** select **v6.0**.
   - Leave the ESP-IDF and tools directories at their defaults
     (`~/esp` / `~/.espressif`).
   - Click **Install** and wait for the download to finish. It sets up the
     toolchain and the Python virtual environment automatically.
5. When it reports success, run **`ESP-IDF: Doctor Command`** from the Command
   Palette to verify the setup.
6. Open this project's folder in VS Code (**File → Open Folder…**) if you haven't
   already.

> **Docs:** ESP-IDF VS Code extension install guide —
> <https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html>

You can now skip to [Section 5](#5-connect-the-board-and-find-its-serial-port).
The extension's blue status-bar icons at the bottom of the window do everything:
🔌 select port, 🔨 build, ⚡ flash, 🖥️ monitor, and 🔥 (build + flash + monitor).

### Option B — Command line (macOS)

Use this if you want to run `idf.py` directly from a terminal.

1. **Clone ESP-IDF v6.0** into `~/esp`:
   ```sh
   mkdir -p ~/esp
   cd ~/esp
   git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git
   ```
   > `--recursive` pulls the ESP-IDF submodules — don't omit it. If you forget,
   > run `git submodule update --init --recursive` inside `~/esp/esp-idf`.

2. **Install the toolchain and Python environment** for the ESP32:
   ```sh
   cd ~/esp/esp-idf
   ./install.sh esp32
   ```
   This creates a Python virtual environment under `~/.espressif/python_env/` and
   downloads the Xtensa toolchain. (Missing this step is the classic cause of the
   `ESP-IDF Python virtual environment ... not found` error.)

3. **Activate the environment** — you must do this **once per new terminal**:
   ```sh
   source ~/esp/esp-idf/export.sh
   ```
   This is what puts `idf.py` on your `PATH`. If you skip it you'll get
   `zsh: command not found: idf.py`. ESP-IDF deliberately does **not** add itself
   to your PATH permanently.

4. **Make activation easy** by adding an alias to your shell profile
   (`~/.zshrc` on modern macOS):
   ```sh
   alias get_idf='source $HOME/esp/esp-idf/export.sh'
   ```
   Reload with `source ~/.zshrc`, then in any new terminal just run:
   ```sh
   get_idf
   ```
   > Prefer the alias over sourcing `export.sh` directly in `~/.zshrc`: the alias
   > keeps the ~1-second IDF setup out of *every* shell startup and only runs it
   > when you actually need it.

> **If you already installed ESP-IDF via the VS Code extension or EIM** (as this
> repo's maintainer did), your export script lives at a versioned path instead,
> for example:
> ```sh
> source ~/.espressif/v6.0/esp-idf/export.sh
> ```
> Use whichever path actually exists on your machine.

> **Docs:** macOS/Linux command-line setup —
> <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-macos-setup.html>

### Option C — Command line (Windows)

The **ESP-IDF Windows Installer** is the simplest terminal route on Windows: it
bundles Git, Python, the toolchain, and creates ready-to-use shell shortcuts.

1. Download the **ESP-IDF Installer** (choose **v6.0**, offline recommended):
   - Installer download page: <https://dl.espressif.com/dl/esp-idf/>
   - (Newer EIM-based downloads: <https://dl.espressif.com/dl/eim/>)
2. Run the installer. Accept the defaults; when asked, select **ESP-IDF v6.0**
   and target the **ESP32**. It installs to `%USERPROFILE%\esp` and
   `%USERPROFILE%\.espressif`.
3. After it finishes, open the Start Menu and launch **"ESP-IDF 6.0 CMD"** (or
   **"ESP-IDF 6.0 PowerShell"**). This opens a terminal where `idf.py` already
   works — the environment is pre-activated for you.
4. In that ESP-IDF terminal, `cd` into your cloned project:
   ```powershell
   cd C:\path\to\QuickLock-Harness-FW
   ```

> To activate ESP-IDF inside a **plain** PowerShell/CMD window instead of the
> shortcut, run `%USERPROFILE%\esp\v6.0\esp-idf\export.ps1` (PowerShell) or
> `export.bat` (CMD). But the Start Menu shortcut is easier.

> **Docs:** Windows setup —
> <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/windows-setup.html>

---

## 5. Connect the board and find its serial port

Plug the ESP32 into your computer with a **data-capable** USB cable.

> With a single board connected, `idf.py` auto-detects the port, so you rarely
> need the exact name below. Knowing it still helps for confirming the board is
> recognized, choosing between multiple boards, or pointing the VS Code extension
> at the right device.

### USB-to-UART driver (often needed)

Most ESP32 devkits talk to your PC through a USB-to-serial chip. If the board
doesn't show up as a serial port, install the driver for its chip:

- **Silicon Labs CP210x** (very common on ESP32-DevKitC):
  <https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers>
- **WCH CH340 / CH341** (common on cheaper clones):
  <https://www.wch-ic.com/downloads/CH341SER_ZIP.html>

macOS may ask you to allow the driver under **System Settings → Privacy &
Security** after installing; reboot if prompted.

### Identify the port name

**macOS** — list serial devices before and after plugging in:
```sh
ls /dev/tty.*
```
Look for something like `/dev/tty.usbserial-0001`, `/dev/tty.SLAB_USBtoUART`, or
`/dev/tty.wchusbserial1410`. That full path is your port.

**Windows** — open **Device Manager → Ports (COM & LPT)** and look for a device
like **"Silicon Labs CP210x USB to UART Bridge (COM5)"**. Your port is the
`COMx` name (e.g. `COM5`).

> In the VS Code extension, click the 🔌 port icon in the status bar and pick the
> port from the dropdown instead of typing it.

---

## 6. Build

Building compiles the firmware and produces `build/QuickLock-Harness-FW.bin`.
The ESP32 target is already set in `sdkconfig.defaults`, so no configuration is
required.

**VS Code (Option A):** click the 🔨 **Build** icon in the status bar, or run
**`ESP-IDF: Build your Project`** from the Command Palette.

**Command line (macOS / Windows):**
```sh
idf.py build
```
> macOS: remember to `get_idf` (or `source .../export.sh`) first.
> Windows: run it inside the *ESP-IDF CMD/PowerShell* window.

A successful first build ends with something like:
```
Project build complete. To flash, run:
 idf.py flash
```

> **Change the target chip?** This project targets `esp32`. To reconfigure after
> editing `sdkconfig.defaults`, run `idf.py set-target esp32` then
> `idf.py reconfigure`.

---

## 7. Flash

Flashing writes the built firmware onto the board over USB.

**VS Code (Option A):** click the ⚡ **Flash** icon. UART flashing is the default
and is already configured.

**Command line:**
```sh
idf.py flash
```
`idf.py` **auto-detects the serial port**, so you usually don't need to specify
it. Only add `-p PORT` if auto-detection fails or you have **more than one** board
connected:

- macOS: `idf.py -p /dev/tty.usbserial-0001 flash`
- Windows: `idf.py -p COM5 flash`

(Get the port name from [Section 5](#5-connect-the-board-and-find-its-serial-port).)

> **Board won't enter download mode?** Some boards need you to hold the **BOOT**
> button while flashing starts (release it once "Connecting…" appears). Most
> devkits auto-enter download mode and need nothing.

> **Flashing too slow or failing?** Lower the baud rate:
> `idf.py -p PORT -b 115200 flash`.

---

## 8. Monitor

The serial monitor shows the board's log output. With this firmware you'll see
both cores printing:
```
Hello world from task A running on core 0 (count 0)
Hello world from task B running on core 1 (count 0)
...
```

**VS Code (Option A):** click the 🖥️ **Monitor** icon.

**Command line:**
```sh
idf.py monitor
```
As with flashing, the port is auto-detected; add `-p PORT` only if needed.

**To exit the monitor:** press **`Ctrl+]`** (that's Control + right square
bracket). Other useful keys inside the monitor: `Ctrl+T` `Ctrl+R` to reset the
board, `Ctrl+T` `Ctrl+H` for help.

---

## 9. Do it all in one command

Build, flash, and open the monitor in a single step:
```sh
idf.py flash monitor
```
(Add `-p PORT` only if the port isn't auto-detected.)

In VS Code, the 🔥 **"Build, Flash and Monitor"** status-bar icon does the same.

---

## 10. A note on `.vscode/settings.json`

This repo commits a `.vscode/settings.json` that contains **absolute paths
specific to the original developer's machine**, for example:

```jsonc
{
  "idf.currentSetup": "/Users/wehbucci/.espressif/v6.0/esp-idf",
  "idf.port": "/dev/tty.usbserial-0001",
  "clangd.path": "/Users/wehbucci/.espressif/tools/esp-clang/.../clangd"
}
```

**These will not match your machine.** When you run the VS Code ESP-IDF
extension's setup ([Option A](#option-a--vs-code-esp-idf-extension-recommended-cross-platform)),
it rewrites the ESP-IDF paths to point at your own install. You should also:

- Set **`idf.port`** to your own serial port (or just pick it with the 🔌 icon).
- Update **`clangd.path`** / `compilerPath` in `.vscode/c_cpp_properties.json` if
  you use clangd/IntelliSense, or simply let the extension regenerate them.

> To avoid churning this file in git, you can tell git to ignore local changes to
> it: `git update-index --skip-worktree .vscode/settings.json`.

---

## 11. Troubleshooting

**`zsh: command not found: idf.py` (or `'idf.py' is not recognized`)**
The ESP-IDF environment isn't active in this terminal. Run
`source ~/esp/esp-idf/export.sh` (macOS, or your `get_idf` alias) / open the
**ESP-IDF CMD** window (Windows). You must do this in every new terminal.

**`ESP-IDF Python virtual environment "...python_env/..." not found`**
The toolchain/Python env was never created (or was deleted). Re-run the install
step: `cd ~/esp/esp-idf && ./install.sh esp32` (macOS) or re-run the Windows
installer / the extension's Installation Manager. This also happens if your
system Python was upgraded and the old venv no longer matches — reinstalling
rebuilds it against the new Python.

**No serial port shows up**
Install the USB-to-UART driver (CP210x or CH340, see
[Section 5](#5-connect-the-board-and-find-its-serial-port)), try a different
**data** cable, and try a different USB port. On macOS, approve the driver under
System Settings → Privacy & Security.

**macOS: `Permission denied: '/dev/tty.usbserial-...'`**
Another program (an open monitor, Arduino IDE, screen session) is holding the
port. Close it. A CP210x driver reinstall + reboot also resolves stubborn cases.

**`A fatal error occurred: Failed to connect to ESP32`**
Hold the **BOOT** button while flashing begins, lower the baud rate
(`-b 115200`), and confirm you selected the right port.

**Build errors after switching branches or ESP-IDF versions**
Delete the build directory and rebuild: `idf.py fullclean` then `idf.py build`
(or just remove the `build/` folder).

**Board misbehaving / want a clean slate**
Erase the entire flash, then reflash: `idf.py -p PORT erase-flash` followed by
`idf.py -p PORT flash`.

---

## 12. Command cheat sheet

| Task | Command |
| --- | --- |
| Activate ESP-IDF (macOS) | `source ~/esp/esp-idf/export.sh` (or `get_idf`) |
| Activate ESP-IDF (Windows) | open **ESP-IDF CMD/PowerShell**, or run `export.bat` / `export.ps1` |
| Set the target chip | `idf.py set-target esp32` |
| Open config menu | `idf.py menuconfig` |
| Build | `idf.py build` |
| Flash | `idf.py flash` |
| Monitor (exit with `Ctrl+]`) | `idf.py monitor` |
| Build + flash + monitor | `idf.py flash monitor` |
| Clean build | `idf.py fullclean` |
| Erase whole flash | `idf.py erase-flash` |
| Show ESP-IDF version | `idf.py --version` |

The port is **auto-detected**. If you need to force one, add `-p PORT` (e.g.
`idf.py -p /dev/tty.usbserial-0001 flash` on macOS, `idf.py -p COM5 flash` on
Windows).

---

## 13. Further reading

- **ESP-IDF v6.0 Getting Started (ESP32):**
  <https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/get-started/index.html>
- **ESP-IDF VS Code extension docs:**
  <https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html>
- **ESP-IDF Installation Manager (EIM):**
  <https://docs.espressif.com/projects/idf-im-ui/en/latest/>
- **ESP-IDF Build System / project structure:**
  <https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-guides/build-system.html>
- **`idf.py` reference:**
  <https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-guides/tools/idf-py.html>
- **Support:** [esp32.com forum](https://esp32.com/) ·
  [ESP-IDF GitHub issues](https://github.com/espressif/esp-idf/issues)

---

## Project layout

```
QuickLock-Harness-FW/
├── CMakeLists.txt            Top-level build script (project name, target)
├── sdkconfig.defaults        Project defaults (ESP32 target, dual-core FreeRTOS)
├── main/
│   ├── CMakeLists.txt        Registers the main component
│   └── hello_world_main.c    Dual-core FreeRTOS "hello world" firmware
├── pytest_hello_world.py     Automated test (pytest-embedded)
├── .devcontainer/            Docker/QEMU dev container definition
├── .vscode/                  VS Code + ESP-IDF extension settings (machine-specific)
└── README.md                 This file
```

Build outputs land in `build/` and are git-ignored.
