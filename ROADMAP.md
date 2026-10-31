# Roadmap & Release-Planung

Dieses Dokument erfasst geplante Meilensteine und technische Spezifikationen für zukünftige Releases des **CIY X79 RGB macOS Controller**-Projekts.

---

## Version 1.2 – Decoupled Protocol Engine & Developer CLI Tooling
*Fokus: Zero-OS-Lock-in, C/C++-Portabilität und Entwickler-Werkzeuge für Drittprojekte (OpenRGB, Linux, Embedded).*

### 1. Single-Header Pure-C/C++ Protokoll-Kern (`src/core/ciy_protocol.h`)
- [x] **Plattformunabhängiges Header-Only-Design (`static inline`):**
  - Reine C11-Standard-Header (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<string.h>`), keinerlei Abhängigkeit von `IOKit` oder `CoreFoundation`.
  - Volle C++-Kompatibilität durch `#ifdef __cplusplus extern "C"`-Guards in `ciy_protocol.h` und `src/layouts/ciy_layout.h`.
  - Inklusion in `src/core/ciy_hid.h` zur Wahrung voller Abwärtskompatibilität bestehender Build-Aufrufe.
- [x] **Protokoll-Encoder, Decoder & Transfer-Plan:**
  - `ciy_protocol_encode_0a(CIYLightingState st, uint8_t out_buf[42])`: Strikte Bereichsvalidierung (`mode <= 20`, `color <= 10`, `speed <= 4`, `brightness <= 5`, Ablehnung mit `false` bei Überschreitung), Tempo-Invertierung (`4 - speed`), Null-Initialisierung der Padding-Bytes `5..41`.
  - `ciy_protocol_decode_0a(const uint8_t *in_buf, size_t len, CIYLightingState *out_st)`: Prüfung auf `len >= 5` und Report-ID `0x0A`, defensives Clamping von Modus, Farbe, Tempo und Helligkeit.
  - `ciy_protocol_encode_0b(const uint8_t *planar_rgb, size_t rgb_len, uint8_t out_buf[379])`: Aufbau des 379-Byte-Pakets (`Byte 0 = 0x0B` + 378 Bytes Planar-RGB).
  - Bereitstellung von `CIYLightingState`, `CIYPlanStep`, `CIYTransferPlan` und `ciy_build_transfer_plan()` (Sequenz: Report `0x0B` $\rightarrow$ 50 ms Pause $\rightarrow$ Report `0x0A` Mode 19) direkt in `ciy_protocol.h`.
- [x] **Refactoring von `src/core/ciy_hid.c`:**
  - `ciy_get_state()`, `ciy_set_state()`, `ciy_send_planar_rgb()` und `ciy_build_transfer_plan()` nutzen intern ausschließlich die Funktionen aus `ciy_protocol.h`.

### 2. CLI-Erweiterungen für Entwickler (`src/cli/ciy_ctl.c`)
- [x] **Universeller `--dry-run` für alle Schreibbefehle:**
  - Freischaltung von `--dry-run` neben `load` auch für `set`, `fill`, `wasd`, `wasd-arrows` und `all-red` (garantiert ohne USB-/IOKit-Zugriff).
- [x] **Hex-Paket-Inspektion (`--dump-bytes`):**
  - Kombinierbar mit `--dry-run` (sowie `--json`), um die exakten generierten Wire-Bytes von Report `0x0B` (379 Bytes) und Report `0x0A` (42 Bytes) als Hex-String auszugeben.
- [x] **Layout- & Offset-Export (`export-layout`):**
  - Neuer Befehl `ciy_ctl export-layout [--json]` (Einbindung von `src/layouts/ciy_x79_iso_de.c` in den CLI-Build).
  - Exportiert alle 69 logischen Tasten und 70 grafischen Tastenflächen (`row`, `width_u`) inklusive der vorberechneten Byte-Offsets im 379-Byte-Report `0x0B`:
    - `report_0b_r_offset = 1 + led_index`
    - `report_0b_g_offset = 1 + 126 + led_index`
    - `report_0b_b_offset = 1 + 252 + led_index`

### 3. Headless Verifikation & Zero-Dependency-Guards (`tests/test_ciy_protocol.c`)
- [x] **Isolierte Protokoll- & C++-Tests in `scripts/run_tests.sh`:**
  - Kompilierung von `tests/test_ciy_protocol.c` strikt **ohne** `-framework IOKit` und **ohne** `-framework CoreFoundation` (mit ASan/UBSan).
  - Zusätzlicher `clang++ -x c++`-Check für `ciy_protocol.h` und `ciy_layout.h` zum Nachweis der C++-Kompatibilität.

---

## Version 1.3 – OpenRGB Contribution Bundle & Linux Bridge (ISO-DE verifiziert)
*Fokus: Vorbereitung der Upstream-Integration in OpenRGB auf Basis des verifizierten ISO-DE-Layouts, Cross-Platform-Treiber für Linux-Maintainer und Werkzeuge zur geführten Hardware-Verifikation weiterer Layouts.*

### 1. OpenRGB C++ Referenz-Controller (`contrib/openrgb/`)
- [x] **Hardware-verifizierter ISO-DE Controller (`RGBController_CIYX79.cpp / .h`):**
  - Implementierung der `RGBController`-Klasse für OpenRGB, basierend auf dem physisch verifizierten **ISO-DE Layout (69 Tasten)** mit 5×16 Matrix-Zone.
  - Direkte Einbindung des C11/C++-Header-Only-Kerns `src/core/ciy_protocol.h` zur Erzeugung der Wire-Bytes (Report `0x0B` und `0x0A`).
  - Zuordnung der planaren RGB-Ebenen (378 Bytes) zu OpenRGBs Farbraum (`RGBColor`).
- [x] **Sino-Wealth Disambiguierungs-Logik (`CIYX79Detector.cpp`):**
  - Strikte Erkennungslogik zur Verhinderung von Fehlzugriffen auf fremde Sino-Wealth-Tastaturen (`VID 0x258A / PID 0x0049`, z. B. Hyrax HCK61).
  - Validierung von `Interface 1` (Vendor-Defined HID Class) und Prüfung der 379-Byte Feature-Report-Fähigkeit vor Initialisierung des Controllers.
- [x] **Headless Mock-Test-Harness:**
  - C++ Test-Suite mit OpenRGB-Mock-Klassen in `scripts/run_tests.sh` zur Validierung der Puffergrenzen und Farbübertragung via ASan/UBSan (ohne Qt-Abhängigkeit).

### 2. Cross-Platform Linux-Treiber (`src/core/ciy_hid_hidapi.c`)
- [x] **Modulares HID-Backend:**
  - Saubere Trennung der IOKit-spezifischen Logik (`src/core/ciy_hid_iokit.c`) für macOS und Bereitstellung eines POSIX/Linux-Backends (`src/core/ciy_hid_hidapi.c`).
  - Wahrung der Zero-Bloat-Regel: Der native macOS-Build bleibt zu 100 % frei von externen Abhängigkeiten und nutzt weiterhin natives unprivilegiertes IOKit.
- [x] **Linux-Kompilierung für `ciy_ctl`:**
  - Build-Unterstützung für Linux (Ubuntu/Debian, Arch, Fedora) via `clang/gcc` und `hidapi-hidraw`.
  - Ermöglicht es OpenRGB-Maintainern und Linux-Entwicklern, `ciy_ctl` nativ unter Linux auszuführen.
  - Linux udev-Regel in `resources/60-ciy-x79.rules` für unprivilegierten Zugriff auf Interface 1.

### 3. Community Matrix-Probing & Diagnostics Tooling
- [x] **Gezielte LED-Ansteuerung (`ciy_ctl probe-led`):**
  - Befehl `ciy_ctl probe-led <led_index> [r g b | #HEX] [-b 0..5]` (oder `--slot <0..124>`) zur isolierten visuellen Prüfung einzelner LED-Slots auf physischer Hardware.
- [x] **Interaktiver Sweep-Modus:**
  - Diagnosemodus `ciy_ctl sweep` zum schrittweisen Durchschalten aller 125 Matrix-Slots, standardmäßig interaktiv mit Fail-Safe Terminal- und Hardware-Restoration via POSIX `sigaction` (`SIGINT`), optional `--auto [--delay-ms <ms>]`.
- [x] **Community Mapping-Guide (`docs/community_layout_mapping.md`):**
  - Schritt-für-Schritt-Anleitung für externe Tester zur Dokumentation und Einreichung abweichender Tastenpositionen gemäß der Projekt-Taxonomie.

### 4. Upstream Submission Package (`docs/openrgb_submission.md`)
- [x] **Vollständige Vorlage für OpenRGB GitLab Issue & Merge Request:**
  - Dokumentierte USB-Hierarchie, Wire-Paket-Dumps, Hardware-Timings, nicht-destruktiver Lese-Handshake zur Behebung von Issue #2679 und Dokumentation beider Integrationspfade (Patch für `SinowealthControllerDetect.cpp` oder eigenständiger `CIYX79Controller`).

---

## Version 1.4 – WebHID Community Mapper, GUI Zonen & Multi-Source Akku-Indikator
*Fokus: Zero-Install Layout-Vermessung im Browser, native GUI Zonen- & Gradient-Tools sowie universeller Multi-Source & Cross-Platform Akku-Indikator mit striktem Profil-Schutz.*

### 1. Zero-Install WebHID Community Mapper (`docs/mapper/index.html`)
- [x] **Autarke Single-Page WebHID Applikation:**
  - Reine HTML5-, CSS3- und Vanilla-ES6-JavaScript-Lösung ohne Node.js, npm oder Build-Dependencies.
  - Direkter USB-HID Zugriff über `navigator.hid` auf Vendor-Defined Interface 1 (VID `0x258A`, PID `0x0049`).
  - Interaktive 65 % Vektordarstellung mit Umschaltung zwischen **ISO-DE** und **ANSI-US**.
  - Schrittweiser LED-Sweep (Vor/Zurück/Auto-Play), LED-Probe und visuelles Mapping per Klick auf die Taste.
  - 1-Klick JSON-Export zur Einreichung verifizierter Community-Layouts in GitHub-Issues.
  - Automatische Secure-Context-Prüfung mit klaren Hinweisen bei Aufruf über `file:///`.

### 2. Multi-Source & Cross-Platform Akku-Engine (`src/core/ciy_battery.h`)
- [x] **Header-Only Pure-C11 Akku-Architektur:**
  - Nativer macOS-Support via IOKit `IOPowerSources` (`IOPSCopyPowerSourcesInfo`, `IOPSCopyPowerSourcesList`).
  - Linux-Unterstützung über POSIX `/sys/class/power_supply/BAT*/`.
  - Multi-Source Support: Interner Laptop-Akku, Bluetooth-Peripherie (Magic Trackpad, Maus) und USV-Stromversorgungen.
  - Deterministische Test-Mocks über `CIY_BATTERY_MOCK_PERCENT` und `CIY_BATTERY_MOCK_CHARGING`.
  - Frei wählbare Tasten-Ziele: Zahlenreihe `1-0` (10 Stufen), Leertaste (Slot 35, analoge Farbumschaltung), Nav-Spalte (Slots 91, 94, 87, 86) und benutzerdefinierte Tastenfolgen.
  - Akku-Ampelfarben: Grün (`#00E676` > 50%), Bernstein/Gelb (`#FFD600` 21..50%), Warn-Rot (`#FF1744` <= 20%) und Charging Electric Cyan (`#00E5FF`).
  - Planar-Overlay-Berechnung mit Erhalt unberührter Matrix-Slots (`ciy_battery_calculate_overlay`).

### 3. CLI Subcommand `ciy_ctl battery` (`src/cli/ciy_ctl.c`)
- [x] **Terminal-Steuerung & Scripting:**
  - `ciy_ctl battery`: Ausgabe von Ladestand, Status, berechneter Farbe und Ziel-Slots (Text & JSON).
  - `ciy_ctl battery --apply [--target ...] [-b ...]`: Berechnet Overlay und sendet Report 0x0B und 0x0A Mode 19 an die Hardware.
  - `ciy_ctl battery --list-sources`: Listet alle erkannten Stromquellen strukturiert auf.
  - `ciy_ctl battery --watch`: Ereignisgesteuerte Dauerüberwachung mit unterbrechungsfreiem Signal-Handling (`SIGINT`).
  - Volle Integration in `--dry-run`, `--dump-bytes` und `--json`.

### 4. GUI Zonen, Farbverläufe & Ephemeres Overlay (`src/app/`)
- [x] **Strikter Profil-Schutz & Overlay-Schicht (`CIYKeyboardView`):**
  - Strikte Trennung zwischen gespeicherten Basis-Profilfarben (`_keyColors`) und flüchtigen Overlays (`_overlayColors`).
  - Mischung (Merge) erfolgt ausschließlich im temporären Übertragungs-Snapshot in `sendCurrentProfileToHardware`, ohne `NSUserDefaults` zu verändern.
- [x] **Ereignisgesteuerter Akku-Monitor (`CIYBatteryMonitor`):**
  - Nutzt `IOPSNotificationCreateRunLoopSource` anstelle von Polling-Schleifen.
  - 500 ms Debounce-Timer zur Bündelung schneller Netzteil-Zustandswechsel.
  - Delta-Filter: 0 % CPU-Last im Leerlauf bei unverändertem Ladestand.
- [x] **Zonen-Schnellauswahl & Linear-Gradient Generator:**
  - 1-Klick-Zonenauswahl für WASD, Pfeiltasten, Zahlen 1–0, Modifikatoren und Buchstabenblock (Alpha).
  - 2-Farben-Linear-Farbverlauf (`ciy_gradient.h`) mit stufenloser sRGB-Interpolation über U-Koordinaten und Zeilen.
  - Dialog zur Übernahme auf die Tastatur und zum Kopieren als `ciy_ctl`-Befehl.

### 5. Qualitätssicherung & Test-Suiten
- [x] C11 Unit-Tests unter ASan/UBSan für `test_ciy_battery.c` und `test_ciy_gradient.c`.
- [x] Erweiterte CLI-Testabdeckung in `tests/test_ciy_ctl.sh`.
- [x] GUI- und Overlay-Layer-Tests in `tests/test_profile_logic.m`.
- [x] Universal Binary Build (`arm64` & `x86_64`) für `CIYX79Control.app` v1.4.0 und `dist/ciy_ctl`.

---

## Version 1.5 – Upstream OpenRGB Tracking & Community Layouts
*Fokus: Fortlaufende Begleitung des OpenRGB Upstream-Merges und Einbindung verifizierter Layouts (z. B. ANSI-US).*

- [ ] Begleitung des OpenRGB Upstream-Merges auf GitLab.
- [ ] Integration verifizierter Layout-Dateien aus der Community (z. B. `src/layouts/ciy_x79_ansi_us.c`) nach Eingang von WebHID-Mapper-Ergebnissen.
