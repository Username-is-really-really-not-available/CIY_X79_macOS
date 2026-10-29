# CIY X79 RGB macOS Controller & Protocol Documentation

A native, lightweight macOS solution (Apple Silicon & Intel) for controlling the RGB lighting, built-in dynamic effects, and individual per-key color profiles (e.g. WASD gaming layouts) for the **CIY X79** mechanical keyboard (ISO-DE 65%, 69 keys).

**Languages:** [🇩🇪 Deutsch](README.md) | [🇬🇧 English](README_EN.md) | [🇨🇳 简体中文](README_ZH.md) | [🇷🇺 Русский](README_RU.md)

---

## Table of Contents
1. [Overview & Quickstart](#1-overview--quickstart)
2. [Installation & macOS Gatekeeper (First Launch)](#2-installation--macos-gatekeeper-first-launch)
3. [Key Hardware Insights & Protocol Architecture](#3-key-hardware-insights--protocol-architecture)
4. [USB-HID Protocol Reference](#4-usb-hid-protocol-reference)
5. [Key Matrix & LED Index Map (69-Key ISO-DE)](#5-key-matrix--led-index-map-69-key-iso-de)
6. [Lighting Modes & Color Palettes](#6-lighting-modes--color-palettes)
7. [The macOS GUI App (CIYX79Control.app)](#7-the-macos-gui-app-ciyx79controlapp)
8. [The Command Line Tool (ciy_ctl)](#8-the-command-line-tool-ciy_ctl)
9. [Architecture & Layer Model](#9-architecture--layer-model)
10. [Automated Test Suite & Hardware Diagnostics](#10-automated-test-suite--hardware-diagnostics)
11. [Compilation & Packaging](#11-compilation--packaging)
12. [Security, Privacy & Performance](#12-security-privacy--performance)
13. [License, Trademark Notice & Disclaimer](#13-license-trademark-notice--disclaimer)

---

## 1. Overview & Quickstart

* **Keyboard Model:** CIY X79 Mechanical Keyboard (65% form factor, hot-swappable)
* **Microcontroller:** Sinowealth MCU (8051-based USB controller)
* **USB Hardware Identifiers:**
  * **Vendor ID (VID):** `0x258A` (Sino Wealth)
  * **Product ID (PID):** `0x0049`
  * **USB Interface:** Interface `1` (Vendor-Defined HID Class)

### Layout Note: German ISO-DE (69 physical keys) vs. ANSI-US (67/68 keys)
This project was tailored specifically for the **German ISO-DE version (69 physical keys)**:
* **Exact Key Count (69 keys):** While ANSI-US 65% keyboards typically have 67 or 68 keys, the German ISO-DE layout features **69 distinct physical switches and LEDs**:
  * 61 keys in the main alphanumeric typing block (including the 2-row tall ISO Enter key, `< > |` next to left Shift, and `# '` in row 2).
  * 8 keys in the navigation and arrow cluster (`Del`, `PgUp`, `PgDn`, `End` + 4 arrow keys).
* **ISO Specifics:** Full support for German umlauts (`Ä`, `Ö`, `Ü`, `ß`), accent keys, and symbols.
* **ANSI-US Compatibility:** The ANSI version has 67 or 68 keys (single-height Enter, wide Shift). The underlying USB-HID protocol is identical; only the interactive vector canvas reflects the German ISO-DE layout.

### Compatibility Matrix & Taxonomy

To establish unambiguous expectations and avoid unverified hardware claims, this project adheres to a strict 4-tier taxonomy:

* **Maintainer-verified:** Model, revision, and all core features physically verified by the maintainer on genuine hardware under macOS.
* **Community-verified:** A reproducible hardware report from an owner exists (including macOS version, CPU architecture, and protocol traces); physical maintainer verification pending.
* **Experimental / unverified:** Unconfirmed support (e.g., based on firmware/MCU similarity or theoretical mappings); no operational guarantee.
* **Unsupported:** Explicitly reserved for proven incompatible devices or revisions (e.g., incompatible MCU, diverging USB packet formats, or failed hardware tests) – not merely untested models.

| Model | Layout | VID:PID | Interface | Status | Tested Scope | Evidence / Reference |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **CIY X79** | ISO-DE (69 keys) | `0x258A:0x0049` | 1 | **Maintainer-verified** | Status, Modes 0..20, Custom Planar RGB, Hotplug Re-Sync, macOS 12..15 (arm64 & x86_64) | Baseline v1.0 / v1.1 |
| **CIY X79** | ANSI-US (68 keys) | `0x258A:0x0049` | 1 | **Experimental / unverified** | Protocol assumed identical; physical key matrix & LED mapping pending | [Issue Template](.github/ISSUE_TEMPLATE/device_layout_request.md) |
| **Generic Sinowealth Keyboards** | Various | Various | * | **Unsupported** | Firmware protocols diverge; fail-closed protection prevents malformed writes | Protocol Spec |

### Quick Start:

* **Graphical App (GUI):**
  Launch `CIYX79Control.app`. You can select presets (WASD Gaming, Cyberpunk, All Red, Clean White), edit individual key colors on an interactive canvas, adjust brightness (1–5), and import/export JSON profiles.
* **Terminal CLI (`ciy_ctl`):**
  ```bash
  # Check connection and current state
  ./ciy_ctl status
  ./ciy_ctl status --json

  # Set full-board static color with brightness flag (-b 0..5)
  ./ciy_ctl fill "#FF5500" -b 4

  # Apply gaming profiles
  ./ciy_ctl wasd
  ./ciy_ctl wasd-arrows
  ./ciy_ctl all-red

  # Load exported JSON profile
  ./ciy_ctl load profile.json
  ```

---

## 2. Installation & macOS Gatekeeper (First Launch)

Because this software is provided as a free community open-source project without a paid Apple Developer Certificate ($99/year), macOS **Gatekeeper** will display a security prompt on initial launch (*"Developer cannot be verified"* or *"App is damaged"*).

### One-Time Verification:

* **Method 1 (Finder – Recommended):**
  1. Move `CIYX79Control.app` into your `/Applications` folder.
  2. In Finder, **Right-click** (or `Control` + Click) on `CIYX79Control.app` and choose **"Open"**.
  3. Click **"Open"** (or *"Open Anyway"*) in the system dialog. After this confirmation, macOS will launch the app directly via double-click.

* **Method 2 (Terminal):**
  Remove the download quarantine attribute via terminal:
  ```bash
  xattr -cr /Applications/CIYX79Control.app
  ```

---

## 3. Key Hardware Insights & Protocol Architecture

To establish interoperability under macOS (in accordance with § 69e German Copyright Act UrhG and § 3 German Trade Secrets Act GeschGehG), the USB-HID protocol of the CIY X79 was analyzed. Four critical hardware characteristics were identified:

### A. Custom Mode vs. Rainbow Animation
* **Mode 19 (`0x13`):** Official `Customize` mode. In this mode alone, the microcontroller reads and renders per-key RGB colors stored in its volatile XRAM buffer!
* **Mode 20 (`0x14`):** Dynamic rainbow ripple animation. If Mode 20 is addressed, the keyboard deliberately plays this animated pattern.
* **Mode 15 (`0x0F`):** `Solid` color mode (entire board single static color from the hardware palette).

### B. Mandatory Transmission Sequence
If Mode 19 is activated before the color buffer is populated and latched, the firmware falls back to stock animations. The required atomic sequence is:
1. Send **`Report 0x0B` (379 bytes)**: Transfers planar RGB buffers into XRAM.
2. Wait **50 ms** (`usleep(50000)`): Gives the 8051 MCU time to latch the buffer.
3. Send **`Report 0x0A` (42 bytes)** with Mode 19 (`0x13`): Instantly activates custom lighting without flickering.

### C. The +1 Memory Index Offset
In the Sinowealth firmware, hardware Matrix Slot 1 (`Esc`) is mirrored internally to Slot 0 (`XRAM[0x0A60] = XRAM[0x0A61]`). Consequently, all physical LED buffer addresses are shifted by **+1**:
$$\text{LED\_Index} = \text{Matrix\_Slot} + 1$$

* **W:** Slot `14` $\rightarrow$ **Index `15`**
* **A:** Slot `9` $\rightarrow$ **Index `10`**
* **S:** Slot `15` $\rightarrow$ **Index `16`**
* **D:** Slot `21` $\rightarrow$ **Index `22`**
* **Arrow Up:** Slot `88` $\rightarrow$ **Index `89`**
* **Arrow Left:** Slot `83` $\rightarrow$ **Index `84`**
* **Arrow Down:** Slot `89` $\rightarrow$ **Index `90`**
* **Arrow Right:** Slot `95` $\rightarrow$ **Index `96`**

### D. Volatile XRAM vs. On-Board Shortcuts (`Fn + ;`)
Custom Per-Key RGB is held in volatile **XRAM** rather than persistent flash ROM. If you cycle through animations using hardware shortcuts (`Fn + ;`), the MCU overrides the custom buffer. The macOS app and CLI allow you to restore any profile in less than 50 ms.

### E. Multi-User Lockfile Security (`/tmp/ciy_x79_bus_<UID>.lock`)
To serialize USB transfers between the Cocoa background queue (`_hwQueue`) and CLI commands (`ciy_ctl`), the driver uses user-specific lockfiles (`/tmp/ciy_x79_bus_<UID>.lock`) with permissions `0600` (readable and writable strictly by the current user) and a non-blocking 2.0-second timeout, avoiding world-readable file security issues and lock freezes.

### F. RFC 8259 Unicode Escape Decoding (`\uXXXX`)
Standard JSON encoders (such as Python's `json.dump` or browser tools) escape non-ASCII characters to `\uXXXX` sequences (`\u00df` for `ß`, `\u00e4` for `ä`, etc.). The built-in C JSON parser in [`ciy_hid.c`](src/core/ciy_hid.c) decodes these sequences directly into valid UTF-8, ensuring profiles containing German special characters load seamlessly.

### G. Wide-Gamut (Display P3) Color Clamping
On macOS systems with wide-gamut displays (P3 ColorSpace), native color pickers may yield coordinates outside `[0.0, 1.0]` in standard sRGB. The Cocoa backend strictly clamps color channels mathematically into `[0, 255]` before transmitting RGB planes, preventing integer overflow and undefined LED colors.

### H. Hardware Test Isolation (`CIY_TESTING=1`)
Continuous Integration and automated test suites run completely isolated from real hardware: exporting `CIY_TESTING=1` routes all transfers through a virtual mock HID device, verifying packets and buffers in memory without touching endpoint 0 of any connected keyboard.

---

## 4. USB-HID Protocol Reference

Communication occurs via **USB Control Transfers** on Endpoint 0 (Interface 1):
* **Set Feature:** `bmRequestType = 0x21` (`kUSBOut | kUSBClass | kUSBInterface`), `bRequest = 0x09` (`kHIDRqSetReport`)
* **Get Feature:** `bmRequestType = 0xA1` (`kUSBIn | kUSBClass | kUSBInterface`), `bRequest = 0x01` (`kHIDRqGetReport`)
* **wValue:** `(kHIDReportTypeFeature << 8) | ReportID`
* **wIndex:** `1` (Interface number)

| Report ID | Buffer Size | Direction | Purpose |
|---|---|---|---|
| **`0x0A`** | 42 Bytes | Get / Set Feature | Global state (Mode, Palette, Speed, Brightness) |
| **`0x0B`** | 379 Bytes | Set Feature | Planar RGB matrix (3 planes $\times$ 126 bytes) |
| **`0x09`** | 505 Bytes | Get / Set Feature | Key remapping configuration (126 slots) |

### Structure of `Report 0x0A` (42 Bytes):
* `Byte 0`: `0x0A` (Report ID)
* `Byte 1`: Mode (`0` = Off, `1..18` = Effects, `15` = Solid, `19` = Customize / Per-Key)
* `Byte 2`: Color palette (`0..10`)
* `Byte 3`: Inverted speed (`4 - speed`, range `0` = slow .. `4` = fast)
* `Byte 4`: Brightness (`0` = dark/off .. `5` = maximum)
* `Bytes 5..41`: Reserved (`0x00`)

### Structure of `Report 0x0B` (379 Bytes Planar RGB):
* `Byte 0`: `0x0B` (Report ID)
* `Bytes 1..126`: **Plane 0: RED** (brightness `0x00`..`0xFF` for Index `0..125`)
* `Bytes 127..252`: **Plane 1: GREEN** (`0x00`..`0xFF` for Index `0..125`)
* `Bytes 253..378`: **Plane 2: BLUE** (`0x00`..`0xFF` for Index `0..125`)

---

## 5. Key Matrix & LED Index Map (69-Key ISO-DE)

Determined through analysis of the HID matrix structure, hardware latency benchmarks, and USB packet offsets. The German ISO-DE layout features **69 physical keys** (the 2-row tall ISO Enter key occupies matrix slot 81 and mechanically spans rows 1 and 2):

| Row | Key (ISO-DE) | Matrix Slot | USB LED Index (`Slot + 1`) | Width (U) |
|---|---|---|---|---|
| **Row 0** | **Esc** | 1 | 2 | 1.0 |
| | **1 .. 0** | 7, 13, 19, 25, 31, 37, 43, 49, 55, 61 | 8, 14, 20, 26, 32, 38, 44, 50, 56, 62 | 1.0 |
| | **ß / ?** | 67 | 68 | 1.0 |
| | **´ \` (Accent)** | 73 | 74 | 1.0 |
| | **⌫ Backspace** | 79 | 80 | 2.0 |
| | **Delete (Entf)** | 86 | 87 | 1.0 |
| **Row 1** | **⇥ Tab** | 2 | 3 | 1.5 |
| | **Q** | 8 | 9 | 1.0 |
| | **W** | **14** | **15** | 1.0 |
| | **E, R, T, Z, U, I, O, P** | 20, 26, 32, 38, 44, 50, 56, 62 | 21, 27, 33, 39, 45, 51, 57, 63 | 1.0 |
| | **Ü** | 68 | 69 | 1.0 |
| | **+ \*** | 74 | 75 | 1.0 |
| | **↵ Enter (upper)** | 81 | 82 | 1.5 |
| | **Page Up (Bild ⇡)** | 87 | 88 | 1.0 |
| **Row 2** | **⇪ Caps Lock** | 3 | 4 | 1.75 |
| | **A** | **9** | **10** | 1.0 |
| | **S** | **15** | **16** | 1.0 |
| | **D** | **21** | **22** | 1.0 |
| | **F, G, H, J, K, L** | 27, 33, 39, 45, 51, 57 | 28, 34, 40, 46, 52, 58 | 1.0 |
| | **Ö** | 63 | 64 | 1.0 |
| | **Ä** | 69 | 70 | 1.0 |
| | **# '** | 75 | 76 | 1.0 |
| | **↵ Enter (lower)** | 81 | 82 | 1.25 |
| | **Page Down (Bild ⇣)** | 94 | 95 | 1.0 |
| **Row 3** | **⇧ Left Shift** | 4 | 5 | 1.25 |
| | **< > \|** | 76 | 77 | 1.0 |
| | **Y, X, C, V, B, N, M** | 10, 16, 22, 28, 34, 40, 46 | 11, 17, 23, 29, 35, 41, 47 | 1.0 |
| | **, ;** | 52 | 53 | 1.0 |
| | **. :** | 58 | 59 | 1.0 |
| | **- _** | 64 | 65 | 1.0 |
| | **⇧ Right Shift** | 82 | 83 | 1.75 |
| | **Arrow Up (▲)** | **88** | **89** | 1.0 |
| | **End (Ende)** | 91 | 92 | 1.0 |
| **Row 4** | **Left Ctrl** | 5 | 6 | 1.25 |
| | **Win / Cmd** | 11 | 12 | 1.25 |
| | **Left Alt** | 17 | 18 | 1.25 |
| | **Spacebar** | 35 | 36 | 6.25 |
| | **AltGr** | 53 | 54 | 1.25 |
| | **Fn** | 77 | 78 | 1.0 |
| | **Right Ctrl** | 80 | 81 | 1.0 |
| | **Arrow Left (◄)** | **83** | **84** | 1.0 |
| | **Arrow Down (▼)** | **89** | **90** | 1.0 |
| | **Arrow Right (►)** | **95** | **96** | 1.0 |

---

## 6. Lighting Modes & Color Palettes

### Modes (0 .. 20):
| Mode | Internal Name | Description |
|---|---|---|
| **0** | `Off` | LEDs completely powered down |
| **1** | `Wave / Rainbow` | Horizontal chromatic wave |
| **2** | `Breathing` | Pulsing fade in / fade out |
| **3** | `Ripple` | Expanding ring from pressed keys |
| **4** | `Raindrops` | Randomized individual keys |
| **5** | `Neon Stream` | Flowing neon stripes |
| **6** | `Stars` | Gentle twinkle in background |
| **7** | `Snake` | Zig-zag path along rows |
| **8** | `Spectrum Cycle` | Synchronous color shift across board |
| **9** | `Vortex` | Rotating chromatic swirl |
| **10** | `Meteor` | Horizontal light trails |
| **11** | `Aurora` | Soft diffuse waves |
| **12** | `Windmill` | Rotating star blades |
| **13** | `Ripple Bloom` | Circular expansion |
| **14** | `Laser` | Lateral beam upon keystroke |
| **15** | **`Solid`** | **Single static color across full board** |
| **16** | `Wave Break` | Converging light waves |
| **17** | `Glow` | Steady glow with subtle highlights |
| **18** | `Sparks` | Rapid light particles |
| **19** | **`Customize (Matrix)`** | **True Per-Key Custom RGB from XRAM (Report 0x0B)** |
| **20** | `Rainbow Ripple` | High-speed dynamic wave |

### Palettes (0 .. 10):
`0: Red` | `1: Orange` | `2: Yellow` | `3: Green` | `4: Cyan` | `5: Blue` | `6: Purple` | `7: White` | `8: Rainbow` | `9: Dynamic 1` | `10: Dynamic 2`

---

## 7. The macOS GUI App (`CIYX79Control.app`)

Written in native Objective-C/Cocoa without external dependencies or heavy runtimes:
* **Decoupled Editing:** Clicking and designing keys operates entirely in UI memory with zero USB overhead until you choose to transmit.
* **Instant Presets:** WASD Gaming, WASD Stealth, Arrow Highlights, Cyberpunk, Clean White, All Red.
* **macOS Color Well:** Native color picker for full 24-bit RGB fine-tuning.
* **JSON Profile Import & Export (`⌘E` / `⌘O`):** Save your setups to disk or share them with others.
* **IOKit Hotplug Auto-Sync:** Listens to USB connect/disconnect events and re-synchronizes your active profile automatically upon reconnection.
* **Auto-Profile Switching by App Focus & Menu Bar Item (Tray):**
  * **Menu Bar Status Item (`NSStatusItem`):** The app integrates an adaptive keyboard icon into the macOS menu bar. Closing the main window (`⌘W` or red close button) hides the window while the app stays active in the background (0.0% CPU usage).
  * **Quick Menu Bar Controls:**
    * Live connection status (*CIY X79: Verbunden / Getrennt*)
    * *"Hauptfenster öffnen..."* to immediately restore the GUI (also deminiaturizes the window if minimized in the Dock)
    * One-click switching between all custom profiles (with active profile checkmark and tags such as `[Steam]`)
    * Instant toggle for auto-trigger (`⚡ Auto-Trigger`)
    * Cleanly quit the application (`⌘Q`)
  * **Conflict-Free App Binding:** Link any profile to specific apps or games (e.g. Steam, CS:GO, VS Code). Assigning an app to a profile automatically clears it from other profiles to guarantee 1:1 uniqueness.
  * **100% Unprivileged & Secure:** Uses standard Cocoa `NSWorkspaceDidActivateApplicationNotification` — **no** Accessibility permissions, **no** Input Monitoring, and **no** keyloggers required.
  * **Intelligent Debouncing (250 ms):** Eliminates LED flickering and excessive USB traffic during rapid `⌘ + Tab` window switching.
  * **Resilient Default Fallback:** Automatically reverts to your designated default profile (e.g. Clean White) when returning to desktop or unassigned apps. Stale or invalid fallback IDs are automatically repaired on launch.
  * **Clean Teardown (`applicationWillTerminate:`):** Fully deregisters and destroys IOKit notification ports and USB handles upon exit.

---

## 8. The Command Line Tool (`ciy_ctl`)

Ideal for shell scripts, menubar tools (SketchyBar, SwiftBar), Alfred, or Raycast:

```bash
# Query status (text or JSON)
./ciy_ctl status
./ciy_ctl status --json

# Fill all keys with a hex color (supports explicit -b / --brightness flag)
./ciy_ctl fill "#FF5500" -b 4
./ciy_ctl fill 00FF88 3           # Positional fallback supported

# Load an exported JSON profile:
# Syntax: ./ciy_ctl load <profile.json> [profile_name_or_index] [-b 0..5]
./ciy_ctl load gaming.json
./ciy_ctl load gaming.json "WASD" -b 4 --json
./ciy_ctl load gaming.json 2 -b 3  # Selects 2nd profile by index

# Validate profile offline without hardware (zero USB I/O):
# Syntax: ./ciy_ctl validate-profile <profile.json> [--json]
./ciy_ctl validate-profile gaming.json            # Verify syntax, slots (0..124), and colors (#RGB)
./ciy_ctl validate-profile gaming.json --json     # Machine-readable output for CI/CD

# Simulate profile transfer (--dry-run with zero USB I/O):
# Syntax: ./ciy_ctl --dry-run load <profile.json> [profile_name_or_index] [-b 0..5] [--json]
./ciy_ctl --dry-run load gaming.json              # Shows planned sequence (0x0B -> 50ms -> 0x0A)
./ciy_ctl --dry-run load gaming.json --json       # Detailed transfer plan as JSON

# Gaming shortcuts
./ciy_ctl wasd
./ciy_ctl wasd-arrows
./ciy_ctl all-red

# Set raw parameters: ./ciy_ctl set <Mode> [Color] [Speed] [Brightness]
./ciy_ctl set 15 0 1 5
./ciy_ctl set 0 0 0 0   # Completely power down LEDs

# List all modes and colors (text or JSON)
./ciy_ctl list-modes
./ciy_ctl list-modes --json

# Enable Zsh autocompletion in ~/.zshrc:
# fpath=(/path/to/CIY_X79_macOS/scripts/completions $fpath)
# autoload -Uz compinit && compinit

# Help & options
./ciy_ctl --help
```

> [!TIP]
> **Unambiguous Argument Resolution in `load`:**
> If a single argument follows the JSON file path (e.g. `./ciy_ctl load gaming.json 3`), it is strictly evaluated as a **profile selector** (here: the 3rd profile in the file). To adjust brightness, always use the explicit `-b` or `--brightness` flag (e.g. `-b 3`).

---

## 9. Architecture & Layer Model

### Directory Structure
```
CIY_X79_macOS/
├── src/
│   ├── core/                    # Pure C USB driver layer (IOKit & HID)
│   │   ├── ciy_hid.h            # USB HID headers, planar RGB & JSON parser
│   │   └── ciy_hid.c            # USB control transfers, low-level protocol, mock device
│   ├── layouts/                 # Decoupled layout definitions & key models
│   │   ├── ciy_layout.h         # Model for CIYLogicalKey, CIYKeySurface, CIYKeyboardLayout
│   │   └── ciy_x79_iso_de.c     # 69 logical keys & 70 key surfaces of CIY X79 ISO-DE
│   ├── app/                     # Native Cocoa application (Objective-C)
│   │   ├── main.m
│   │   ├── AppDelegate.h / .m   # UI lifecycle, menus, export/import, color selection
│   │   ├── CIYKeyboardView.h / .m # Canvas view (69-key ISO-DE vector rendering)
│   │   └── KeyboardBackend.h / .m # Thread-safe driver, P3 clamp & hotplug
│   └── cli/                     # Command line utility
│       └── ciy_ctl.c            # CLI tool ciy_ctl (status, JSON, fill, load, -b flag)
├── resources/                   # App icons & graphic assets
│   └── AppIcon.icns
├── scripts/                     # Build, test, and packaging scripts
│   ├── build_app.sh             # Universal binary compiler & codesigning (-O2)
│   ├── create_dmg.sh            # Compressed disk image packager (UDZO)
│   ├── run_tests.sh             # Isolated test suite runner
│   ├── hardware_diagnostics.py  # Hardware diagnostics, latency benchmark & sweep
│   └── completions/             # Shell autocompletion
│       └── _ciy_ctl             # Zsh completion definition
├── tests/                       # Automated test suites
│   ├── fixtures/                # Valid & invalid profile JSON test fixtures
│   ├── test_ciy_hid.c           # C protocol, hex, Unicode & lockfile tests
│   ├── test_profile_logic.m     # Canvas, P3 clamp & profile memory logic tests
│   └── test_ciy_ctl.sh          # End-to-End CLI tests (syntax, -b, error handling)
├── dist/                        # Compiled universal binaries & DMG installer
│   ├── CIYX79Control.app
│   ├── ciy_ctl
│   └── CIYX79Control-1.1.0.dmg
├── README.md                    # German documentation & primary guide
├── README_EN.md                 # English documentation
├── README_ZH.md                 # Chinese documentation
├── README_RU.md                 # Russian documentation
├── LICENSE                      # MIT license
└── .gitignore                   # Git ignore patterns
```

### Layer Model

```
┌────────────────────────────────────────────────────────┐
│                        User Layer                      │
│      CIYX79Control.app (GUI)    /    ciy_ctl (CLI)     │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                    Interactive Canvas                  │
│                     CIYKeyboardView.m                  │
│         (69-Key ISO-DE Vector Rendering & Mouse)       │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                   Cocoa Driver Backend                 │
│                     KeyboardBackend.m                  │
│      (Thread safety, serial _hwQueue, P3 clamping)     │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                  Pure C USB-HID Engine                 │
│                  ciy_hid.c / ciy_hid.h                 │
│      (Report 0x0A, 0x0B Planar RGB, CIY_TESTING mock)  │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                 CIY X79 Hardware (USB)                 │
│         VID: 0x258A | PID: 0x0049 | Interface 1        │
└────────────────────────────────────────────────────────┘
```

1. **`src/core/` (`ciy_hid.c` / `ciy_hid.h`):**
   * Pure C layer with zero UI dependencies.
   * Dispatches USB Control Requests to Interface 1 via IOKit.
   * Per-user lockfile (`/tmp/ciy_x79_bus_<UID>.lock`, `0600`) with timeout serialization.
   * Built-in virtual hardware mock when `CIY_TESTING` environment variable is active.
   * Planar RGB builders and JSON parser with RFC 8259 Unicode escape decoding (`\uXXXX`).
2. **`src/app/` (`KeyboardBackend.m` / `KeyboardBackend.h`):**
   * Cocoa singleton (`[KeyboardBackend shared]`).
   * Thread safety backed by a serial background queue (`_hwQueue`) and asynchronous init.
   * Mathematical color channel clamping for Wide-Gamut/Display P3 displays.
   * IOKit hotplug monitoring.
3. **`src/app/` (`CIYKeyboardView.m` / `CIYKeyboardView.h`):**
   * `NSView` subclass providing precise 65% ISO-DE vector rendering (69 physical keys).
   * Crash-safe sRGB color extraction (`[color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]]`).
   * Mouse hover and key selection tracking.
4. **`src/app/` (`AppDelegate.m` / `AppDelegate.h`):**
   * Application lifecycle, layout composition, color palette, and profile management with sanitization.
5. **`src/cli/` (`ciy_ctl.c`):**
   * Lightweight C command-line tool for scripting and terminal operation with clean option extraction (`-b / --brightness`).

---

## 10. Automated Test Suite & Hardware Diagnostics

### A. Automated Test Suite (`scripts/run_tests.sh`)
Run the hardware-free, fully isolated automated test suite:
```bash
./scripts/run_tests.sh
```
Covers:
* `tests/test_ciy_hid.c`: Planar RGB buffer calculations, hex conversions, Unicode `\uXXXX` escape decoding, out-of-bounds guards, and secure per-user lockfile permissions (`0600`).
* `tests/test_profile_logic.m`: Display P3 color clamping, profile lifecycle, app binding deduplication, and fallback recovery.
* `tests/test_ciy_ctl.sh`: End-to-end CLI option extraction (`-b` flag vs profile selector), JSON validation, and error reporting.

### B. Hardware Diagnostics & Benchmark (`scripts/hardware_diagnostics.py`)
When a real CIY X79 is connected, use the diagnostic script to measure response times and verify key mappings:
```bash
# 1. Run complete diagnostic battery (Isolation -> Benchmark -> Clusters -> Sweep -> Restore)
python3 scripts/hardware_diagnostics.py all

# 2. Run latency benchmark across all 69 keys (measures USB ack times in ms)
python3 scripts/hardware_diagnostics.py benchmark

# 3. Visible LED sweep across all 69 keys (rows 0 to 4)
python3 scripts/hardware_diagnostics.py sweep

# 4. Functional cluster dynamics (WASD, arrows, navigation, modifiers)
python3 scripts/hardware_diagnostics.py clusters

# 5. Test specific single key (e.g. Esc, Enter, Space, ArrowUp, <>)
python3 scripts/hardware_diagnostics.py key Space

# 6. Restore standard WASD gaming profile
python3 scripts/hardware_diagnostics.py restore
```

---

## 11. Compilation & Packaging

### 1. Compile Universal Binary App & CLI:
```bash
./scripts/build_app.sh
```
Builds both `CIYX79Control.app` and `ciy_ctl` with compiler optimization (`-O2`) as **Universal Binaries** (`-arch arm64 -arch x86_64`) for Apple Silicon (M1–M4) and Intel Macs, embeds high-resolution icons (`AppIcon.icns`) and multilingual documentation (`README*.md`), and applies ad-hoc codesigning.

### 2. Create Disk Image (DMG):
```bash
./scripts/create_dmg.sh
```
Generates the redistributable `CIYX79Control-1.1.0.dmg` installer with drag-and-drop `/Applications` symlink, embedded `ciy_ctl` binary, license, and documentation.

### 3. Compile CLI Tool Manually:
```bash
clang -O2 -arch arm64 -arch x86_64 -Wall -Wextra -framework IOKit -framework CoreFoundation src/core/ciy_hid.c src/cli/ciy_ctl.c -o dist/ciy_ctl
```

---

## 12. Security, Privacy & Performance

* **Zero Input Monitoring:** Requires **no** Accessibility or Input Monitoring permissions. Does not intercept or log keystrokes.
* **Zero Kernel Extensions:** Operates purely in unprivileged user space via official Apple IOKit APIs.
* **100% Offline:** No network sockets, zero telemetry, no third-party analytic trackers.
* **0.0% Idle CPU Usage:** Event-driven architecture with zero background polling loops.

---

## 13. License, Trademark Notice & Disclaimer

### License
This project is licensed under the **[MIT License](LICENSE)**. You are free to use, modify, and distribute this software with copyright attribution.

### Trademark Notice & Disclaimer
"CIY", "Sinowealth", "Apple", "macOS", and other product names mentioned are trademarks or registered trademarks of their respective holders. Their use is purely descriptive to identify product compatibility under **§ 23 (1) No. 3 German Trademark Act (MarkenG)** and fair use. This project is an independent community open-source initiative and is **not affiliated with, endorsed by, or sponsored by** the hardware manufacturers or Apple Inc.

### Disclaimer of Warranty & Liability (German Law)
Use this software at your own risk. The software communicates directly with hardware registers and USB microcontrollers. To the maximum extent permitted by law (governed by **§ 521 German Civil Code BGB - law of donations**), liability of the author is restricted to intent and gross negligence. No warranty is provided for hardware defects or data integrity.
