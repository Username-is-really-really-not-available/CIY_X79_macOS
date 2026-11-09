# API-Quotas, Hardware-Timings & Parsing-Spezifikation

Dieses Dokument enthält Entwicklerdetails zu Quotas, Hardware-Timings, Rate-Limits und Daten-Parsing für den CIY X79 RGB macOS Controller.

---

## 1. Weiterführende Dokumentation
- Für Low-Level USB-HID Transfers, MCU-Eigenheiten und Lockfile-Details: [`hardware-protocol.md`](hardware-protocol.md)
- Für planare RGB-Pufferstrukturen, Tasten-Matrix und Farbklemmung: [`data-formats.md`](data-formats.md)

---

## 2. API-Quotas & Rate-Limits

### Lokaler Betrieb ohne Cloud/HTTP-Quotas
Der CIY X79 Controller arbeitet vollständig lokal und unprivilegiert im macOS User-Space über das IOKit-Framework (USB-HID Control Transfers, VID `0x258A`, PID `0x0049`, Interface `1`). Es werden keine externen Web-APIs oder Cloud-Endpunkte angesprochen, weshalb keine externen API-Rate-Limits oder Token-Quotas existieren.

### Hardware-Quotas & Übertragungs-Timings
Anstelle von Web-Quotas müssen zwingend strikte hardware- und bus-spezifische Timings eingehalten werden:

1. **Modus 19 Timing-Quota (50 ms Verzögerung):**
   - Beim Senden benutzerdefinierter Per-Key RGB-Beleuchtung muss zwischen dem Planar-RGB-Puffer (`Report 0x0B`, 379 Bytes) und der Modusumschaltung (`Report 0x0A`, Modus 19 `0x13`) eine Zwangspause von mindestens **50 ms** (`usleep(50000)`) eingehalten werden.
   - *Konsequenz bei Verletzung:* Die Sinowealth MCU verwirft den XRAM-Puffer und schaltet unkontrolliert auf integrierte Standardeffekte zurück.

2. **Inter-Process Lockfile Timeout (2,0 Sekunden):**
   - Vor jedem USB-Transfer an das Device muss der per-UID Bus-Lock im Per-User-Temp-Verzeichnis (`confstr(_CS_DARWIN_USER_TEMP_DIR)`, Datei `ciy_x79_bus_<UID>.lock`) mittels `flock(fd, LOCK_EX)` reserviert werden.
   - Der Lock-Timeout ist auf maximal **2,0 Sekunden** begrenzt. Bei Überschreitung bricht `ciy_bus_lock()` mit einem Fehler ab, um Deadlocks zwischen GUI-Queue (`_hwQueue`) und CLI (`ciy_ctl`) zu verhindern.

---

## 3. Daten-Parsing & Sanitizing

### JSON-Profil-Parsing & Unicode-Escapes
- Profile werden im JSON-Format abgelegt und sowohl vom CLI (`ciy_ctl load`) als auch von der Cocoa-GUI (`KeyboardBackend.m`) geladen.
- Profilnamen und Tastenbeschriftungen können Unicode-Escape-Sequenzen (z. B. `\uXXXX`) enthalten, die ohne externe Bibliotheken nativ decodiert werden.
- Hex-Farbwerte (`#RRGGBB` oder `RRGGBB`) werden strikt validiert (Länge 6 oder 7 Zeichen, ausschließlich Hex-Digits `[0-9a-fA-F]`).

### Display P3 / Wide-Gamut Farbraum-Parsing
- macOS `NSColorPanel` liefert auf Liquid Retina Displays Fließkommawerte im Display P3 Farbraum, die außerhalb des sRGB-Normbereichs `[0.0, 1.0]` liegen können (z. B. negative Werte oder Werte > 1.0).
- Alle Farbwerte müssen vor der Konvertierung in 8-Bit-Ganzzahlen (`uint8_t`) mathematisch geklemmt werden (`ClampColorChannel`), um Integer-Wrapping und Farbverzerrungen auf den LEDs zu verhindern:
  ```c
  static inline uint8_t ClampColorChannel(CGFloat val) {
      if (val <= 0.0) return 0;
      if (val >= 1.0) return 255;
      return (uint8_t)round(val * 255.0);
  }
  ```
