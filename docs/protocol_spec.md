# CIY X79 (ISO-DE) - USB-HID Protokoll- & Hardware-Spezifikation

Entwickler-Referenz für Low-Level-Treiber (`ciy_hid.c`), CLI (`ciy_ctl.c`) und Cocoa-Backend (`KeyboardBackend.m`).

---

## 1. USB-Identifikatoren & Endpoint

* **Vendor ID (VID):** `0x258A` (Sino Wealth)
* **Product ID (PID):** `0x0049`
* **Target Interface:** Interface `1` (Vendor-Defined HID Class; Interface `0` ist Standard-Tastatur-Boot-Interface)
* **Transfer-Methode:** USB Control Transfers an Endpoint 0
  * **Set Feature:** `bmRequestType = 0x21` (`kUSBOut | kUSBClass | kUSBInterface`), `bRequest = 0x09` (`kHIDRqSetReport`), `wValue = (kHIDReportTypeFeature << 8) | ReportID`, `wIndex = 1`
  * **Get Feature:** `bmRequestType = 0xA1` (`kUSBIn | kUSBClass | kUSBInterface`), `bRequest = 0x01` (`kHIDRqGetReport`), `wValue = (kHIDReportTypeFeature << 8) | ReportID`, `wIndex = 1`

---

## 2. Report-Definitionen

### Report 0x0A (42 Bytes) – Globale Beleuchtungssteuerung
Wird per Get/Set Feature übertragen.
* `Byte 0`: `0x0A` (Report ID)
* `Byte 1`: Modus (`0` = Aus, `1..18` = Effekte, `15` = Solid/Einzelfarbe, `19` = Customize Per-Key)
* `Byte 2`: Farbpalette (`0..10` für Preset-Farben)
* `Byte 3`: Geschwindigkeit invertiert: `4 - speed` (Wertebereich `0..4`, `0` = langsam, `4` = schnell)
* `Byte 4`: Helligkeit (`0` = dunkel/aus bis `5` = maximal)
* `Bytes 5..41`: Reserviert (`0x00`)

### Report 0x0B (379 Bytes) – Planare Per-Key RGB-Matrix
Wird ausschließlich per Set Feature übertragen (benötigt aktiven Modus 19).
* `Byte 0`: `0x0B` (Report ID)
* `Bytes 1..126`: **Ebene 0: ROT** (Helligkeit `0x00..0xFF` für LED-Index `0..125`)
* `Bytes 127..252`: **Ebene 1: GRÜN** (Helligkeit `0x00..0xFF` für LED-Index `0..125`)
* `Bytes 253..378`: **Ebene 2: BLAU** (Helligkeit `0x00..0xFF` für LED-Index `0..125`)

---

## 3. Hardware-Besonderheiten & Kritische Regeln

1. **Sende-Reihenfolge (Modus 19 Customize):**
   * Schritt 1: `Report 0x0B` (379 Bytes Planar RGB) an Interface 1 senden.
   * Schritt 2: **Zwingend 50 ms Pause** (`usleep(50000)`).
   * Schritt 3: `Report 0x0A` mit `mode = 19 (0x13)` senden.
   * *Fehlerfolge:* Ohne Pause oder bei falscher Reihenfolge verwirft die MCU den XRAM-Puffer und schaltet in Standard-Animationen.

2. **Hardware +1 Index-Offset:**
   * MCU spiegelt Hardware-Slot 1 (`Esc`) intern auf Slot 0 (`XRAM[0x0A60] = XRAM[0x0A61]`).
   * Formel: `LED_Index = Matrix_Slot + 1`
   * Wichtigste Indizes:
     * `W`: Slot 14 -> Index **15**
     * `A`: Slot 9 -> Index **10**
     * `S`: Slot 15 -> Index **16**
     * `D`: Slot 21 -> Index **22**
     * Pfeil ▲: Slot 88 -> Index **89**
     * Pfeil ◄: Slot 83 -> Index **84**
     * Pfeil ▼: Slot 89 -> Index **90**
     * Pfeil ►: Slot 95 -> Index **96**

3. **Volatiler XRAM:**
   * Per-Key-Matrizen liegen im flüchtigen XRAM (kein EEPROM/Flash).
   * Hardware-Tastenkombinationen wie `Fn + ;` überschreiben den Speicher sofort mit integrierten Firmware-Mustern.

4. **Multi-User Lockfile:**
   * Pfad: `/tmp/ciy_x79_bus_<UID>.lock`
   * Berechtigung: `0600`, Timeout: max. 2,0 s via `flock()`.
   * Verhindert Kollisionen zwischen GUI-Background-Queue und CLI-Tool.

5. **Wide-Gamut Farbklemmung:**
   * Werte aus macOS `NSColorPanel` (Display P3) strikt auf `[0, 255]` klemmen (`ClampColorChannel`), um Integer-Wrap zu verhindern.

6. **Testisolation:**
   * Bei `CIY_TESTING=1` wird ein Mock-HID-Device im Speicher verwendet; kein USB-Transfer an Hardware.

---

## 4. Beleuchtungsmodi (0..20)

| Modus (Dez / Hex) | Name | Typ |
|---|---|---|
| `0` (`0x00`) | Aus / Off | Inaktiv |
| `1` (`0x01`) .. `14` (`0x0E`) | Animationseffekte (Welle, Atmung, Neon, Schlange etc.) | Firmware-Presets |
| `15` (`0x0F`) | Statisch / Solid | Globale Einzelfarbe aus Palette |
| `16` (`0x10`) .. `18` (`0x12`) | Wellenbrecher, Glühen, Funkenflug | Firmware-Presets |
| `19` (`0x13`) | **Customize (Per-Key RGB)** | Liest `Report 0x0B` aus XRAM |
| `20` (`0x14`) | Ripple / Regenbogen-Welle | Firmware-Animation |

---

## 5. Tastenmatrix & LED-Indizes (ISO-DE, 69 Tasten)

| Reihe | Tasten | Slots | USB-LED-Indizes (`Slot + 1`) |
|---|---|---|---|
| **Reihe 0** | Esc, 1..0, ß, ´, ⌫ Backspace, Entf | 1, 7, 13, 19, 25, 31, 37, 43, 49, 55, 61, 67, 73, 79, 86 | 2, 8, 14, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74, 80, 87 |
| **Reihe 1** | Tab, Q, W, E, R, T, Z, U, I, O, P, Ü, + \*, Enter (oben), Bild ⇡ | 2, 8, 14, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74, 81, 87 | 3, 9, 15, 21, 27, 33, 39, 45, 51, 57, 63, 69, 75, 82, 88 |
| **Reihe 2** | Caps, A, S, D, F, G, H, J, K, L, Ö, Ä, # ', Enter (unten), Bild ⇣ | 3, 9, 15, 21, 27, 33, 39, 45, 51, 57, 63, 69, 75, 81, 94 | 4, 10, 16, 22, 28, 34, 40, 46, 52, 58, 64, 70, 76, 82, 95 |
| **Reihe 3** | Shift L, < > \|, Y, X, C, V, B, N, M, , ;, . :, - _, Shift R, ▲, Ende | 4, 76, 10, 16, 22, 28, 34, 40, 46, 52, 58, 64, 82, 88, 91 | 5, 77, 11, 17, 23, 29, 35, 41, 47, 53, 59, 65, 83, 89, 92 |
| **Reihe 4** | Strg L, Win/Cmd, Alt L, Leertaste, AltGr, Fn, Strg R, ◄, ▼, ► | 5, 11, 17, 35, 53, 77, 80, 83, 89, 95 | 6, 12, 18, 36, 54, 78, 81, 84, 90, 96 |
