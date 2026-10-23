# CIY X79 RGB macOS Controller & Protocol Documentation

A native, lightweight macOS solution (Apple Silicon & Intel) for controlling the RGB lighting, built-in dynamic effects, and individual per-key color profiles (e.g. WASD gaming layouts) for the **CIY X79** mechanical keyboard (ISO-DE 65%, 68 keys).

**Languages:** [🇩🇪 Deutsch](README.md) | [🇬🇧 English](README_EN.md) | [🇨🇳 简体中文](README_ZH.md) | [🇷🇺 Русский](README_RU.md)

---

## Table of Contents
1. [Overview & Quickstart](#1-overview--quickstart)
2. [Installation & macOS Gatekeeper (First Launch)](#2-installation--macos-gatekeeper-first-launch)
3. [Key Hardware Insights & Protocol Architecture](#3-key-hardware-insights--protocol-architecture)
4. [USB-HID Protocol Reference](#4-usb-hid-protocol-reference)
5. [Key Matrix & LED Index Map (68-Key ISO-DE)](#5-key-matrix--led-index-map-68-key-iso-de)
6. [Lighting Modes & Color Palettes](#6-lighting-modes--color-palettes)
7. [The macOS GUI App (CIYX79Control.app)](#7-the-macos-gui-app-ciyx79controlapp)
8. [The Command Line Tool (ciy_ctl)](#8-the-command-line-tool-ciy_ctl)
9. [Architecture & Layer Model](#9-architecture--layer-model)
10. [Automated Test Suite](#10-automated-test-suite)
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

### Layout Note: German ISO-DE (68 keys) vs. ANSI-US (67 keys)
This project was tailored for the **German ISO-DE version (68 keys)**:
* **ISO Specifics:** Full support for the tall two-row ISO Enter key (Matrix Slot 81) and the `< > |` key (Matrix Slot 76) next to left Shift.
* **ANSI-US Compatibility:** The ANSI version has 67 keys (wide flat Enter, no `< > |` key). The underlying USB-HID protocol and slot mapping are completely identical; only the visual vector preview in the GUI reflects the ISO-DE layout.

### Quick Start:

* **Graphical App (GUI):**
  Launch `CIYX79Control.app`. You can select presets (WASD Gaming, Cyberpunk, All Red, Clean White), edit individual key colors on an interactive canvas, adjust brightness (1–5), and import/export JSON profiles.
* **Terminal CLI (`ciy_ctl`):**
  ```bash
  # Check connection and current state
  ./ciy_ctl status
  ./ciy_ctl status --json

  # Set full-board static color (Hex)
  ./ciy_ctl fill "#FF5500" 5

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

## 5. Key Matrix & LED Index Map (68-Key ISO-DE)

Determined through analysis of the keyboard's HID matrix descriptors and packet offsets:

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

# Fill all keys with a hex color
./ciy_ctl fill "#00FF88" 5

# Load an exported JSON profile (syntax: ./ciy_ctl load <profile.json> [profile_name] [brightness 0..5])
./ciy_ctl load gaming.json
./ciy_ctl load gaming.json "WASD" 4 --json

# Gaming shortcuts
./ciy_ctl wasd
./ciy_ctl wasd-arrows
./ciy_ctl all-red

# Set raw parameters: ./ciy_ctl set <Mode> [Color] [Speed] [Brightness]
./ciy_ctl set 15 0 1 5
```

---

## 9. Architecture & Layer Model

```
┌────────────────────────────────────────────────────────┐
│                        User Layer                      │
│      CIYX79Control.app (GUI)    /    ciy_ctl (CLI)     │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                    Interactive Canvas                  │
│                     CIYKeyboardView.m                  │
│         (68-Key ISO-DE Vector Rendering & Mouse)       │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                   Cocoa Driver Backend                 │
│                     KeyboardBackend.m                  │
│      (Thread safety, serial _hwQueue, IOKit hotplug)   │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                  Pure C USB-HID Engine                 │
│                  ciy_hid.c / ciy_hid.h                 │
│      (Report 0x0A, 0x0B Planar RGB, IOKit transfers)   │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                 CIY X79 Hardware (USB)                 │
│         VID: 0x258A | PID: 0x0049 | Interface 1        │
└────────────────────────────────────────────────────────┘
```

---

## 10. Automated Test Suite

Run the hardware-free automated test suite:
```bash
./scripts/run_tests.sh
```
Covers planar RGB buffer calculations, hex conversions, edge cases, JSON profile serialization, status menu item formatting, app binding deduplication, and fallback profile recovery.

---

## 11. Compilation & Packaging

Compile native Universal Binaries (`arm64` & `x86_64`) using Clang:
```bash
# Build universal App and CLI (embeds AppIcon & docs, signs both binaries)
./scripts/build_app.sh

# Build redistributable DMG installer image
./scripts/create_dmg.sh
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
