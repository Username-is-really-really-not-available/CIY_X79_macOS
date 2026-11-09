# Spezielle Datenformate & Matrix-Layout

Referenzdokumentation für den planaren Per-Key RGB-Report, LED-Index-Offsets und Display P3 Farbkorrektur.

---

## 1. Report-Formate

### Report 0x0A (42 Bytes) – Globale Beleuchtungssteuerung
Wird per `IOHIDDeviceSetReport` / `IOHIDDeviceGetReport` (Feature Report) übertragen:
* `Byte 0`: `0x0A` (Report ID)
* `Byte 1`: Modus (`0` = Aus, `1..14` = Animationen, `15` = Statische Einzelfarbe, `16..18` = Effekte, `19` = Customize Per-Key RGB, `20` = Ripple)
* `Byte 2`: Farbpalette (`0..10` für Preset-Farben)
* `Byte 3`: Geschwindigkeit invertiert: `4 - speed` (Wertebereich `0..4`, `0` = langsam, `4` = schnell)
* `Byte 4`: Helligkeit (`0` = dunkel/aus bis `5` = maximal)
* `Bytes 5..41`: Reserviert (`0x00`)

### Report 0x0B (379 Bytes) – Planare Per-Key RGB-Matrix
Wird ausschließlich per Feature Report übertragen und steuert bis zu 126 Per-Key LEDs in planarer Farbanordnung:
* `Byte 0`: `0x0B` (Report ID)
* `Bytes 1..126`: **Ebene 0: ROT** (Helligkeitswerte `0x00..0xFF` für LED-Index `0..125`)
* `Bytes 127..252`: **Ebene 1: GRÜN** (Helligkeitswerte `0x00..0xFF` für LED-Index `0..125`)
* `Bytes 253..378`: **Ebene 2: BLAU** (Helligkeitswerte `0x00..0xFF` für LED-Index `0..125`)

---

## 2. Hardware +1 Index-Offset

Die Sinowealth MCU spiegelt Hardware-Slot 1 (`Esc`) intern auf Slot 0 (`XRAM[0x0A60] = XRAM[0x0A61]`).
Dadurch sind alle physischen LED-Indizes im planaren Puffer um exakt `+1` gegenüber der Tastenmatrix verschoben:

$$\text{LED\_Index} = \text{Matrix\_Slot} + 1$$

### Wichtige Referenz-Mappings (ISO-DE, 69 Tasten)
* `Esc`: Slot 1 -> LED-Index **2** (Slot 0 ist intern gespiegelt)
* `W`: Slot 14 -> LED-Index **15**
* `A`: Slot 9 -> LED-Index **10**
* `S`: Slot 15 -> LED-Index **16**
* `D`: Slot 21 -> LED-Index **22**
* Pfeiltaste ▲: Slot 88 -> LED-Index **89**
* Pfeiltaste ◄: Slot 83 -> LED-Index **84**
* Pfeiltaste ▼: Slot 89 -> LED-Index **90**
* Pfeiltaste ►: Slot 95 -> LED-Index **96**

---

## 3. Display P3 / Wide-Gamut Farbraum-Konvertierung

Auf Apple Liquid Retina Displays liefert `NSColorPanel` Gleitkommawerte im erweiterten Farbraum (Display P3). Diese können Werte `< 0.0` oder `> 1.0` annehmen.

Vor der Überführung in `uint8_t` müssen alle Farbkanäle zwingend mathematisch geklemmt werden:
```objc
static inline uint8_t ClampColorChannel(CGFloat val) {
    if (val <= 0.0) return 0;
    if (val >= 1.0) return 255;
    return (uint8_t)round(val * 255.0);
}
```
Ohne diese Klemmung führt eine Konvertierung zu Integer-Wrapping (z. B. `(uint8_t)-5 == 251`), was unkontrollierte Fehlansteuerungen der LEDs auslöst.
