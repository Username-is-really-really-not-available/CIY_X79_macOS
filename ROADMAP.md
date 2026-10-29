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
  - `ciy_protocol_decode_0a(const uint8_t *in_buf, size_t len, CIYLightingState *out_st)`: Prüfung auf `len >= 5` und Report-ID `0x0A`, defensives Clamping von Tempo und Helligkeit.
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
  - Exportiert alle 69 logischen Tasten und 70 grafischen Tastenflächen (`row`, `col`, `width_u`) inklusive der vorberechneten Byte-Offsets im 379-Byte-Report `0x0B`:
    - `report_0b_r_offset = 1 + led_index`
    - `report_0b_g_offset = 1 + 126 + led_index`
    - `report_0b_b_offset = 1 + 252 + led_index`

### 3. Headless Verifikation & Zero-Dependency-Guards (`tests/test_ciy_protocol.c`)
- [x] **Isolierte Protokoll- & C++-Tests in `scripts/run_tests.sh`:**
  - Kompilierung von `tests/test_ciy_protocol.c` strikt **ohne** `-framework IOKit` und **ohne** `-framework CoreFoundation` (mit ASan/UBSan).
  - Zusätzlicher `clang++ -x c++`-Check für `ciy_protocol.h` und `ciy_layout.h` zum Nachweis der C++-Kompatibilität.

---

## Version 1.5 – OpenRGB Upstream Contribution Bundle
*Fokus: Direkte Upstream-Integration in das OpenRGB-Projekt.*

- [ ] **OpenRGB Controller Template (`RGBController_CIYX79.cpp / .h`):**
  - Bereitstellung einer fertigen C++-Controller-Vorlage für OpenRGB (`RGBController`-Klasse, `SetupZones()`, Zonen- und Matrix-Definitionen).
  - Anbindung des planaren 378-Byte-RGB-Puffers (R/G/B-Ebenen) an OpenRGBs Farbverwaltung.
  - Disambiguierungs-Logik für Sino Wealth `VID 0x258A / PID 0x0049` (Prüfung von `Interface 1` und Report-Deskriptor, um Konflikte mit Fremdtastaturen wie Hyrax HCK61 zu vermeiden).
