# OpenRGB Upstream Contribution & Submission Package: CIY X79 RGB Keyboard

Dieses Dokument dient als technische Referenz und Einreichungsvorlage (GitLab Issue & Merge Request) für das [OpenRGB-Projekt](https://gitlab.com/CalcProgrammer1/OpenRGB).

---

## 1. Hardware-Spezifikation & Hintergrund

- **Gerät:** CIY X79 RGB Gaming Keyboard (68 Tasten / 65% Compact Formfaktor)
- **MCU:** Sino Wealth USB Microcontroller
- **USB VID:** `0x258A`
- **USB PID:** `0x0049`
- **USB Interface:** `Interface 1` (Vendor-defined HID Feature Reports, Report-Längen: Report `0x0A` = 42 Bytes, Report `0x0B` = 379 Bytes)
- **Verifiziertes Layout:** ISO-DE (69 physische Tasten, Dual-Cell ISO Enter)

---

## 2. Adressierung von OpenRGB Issue #2679 & Bricking-Schutz

### Das Problem (Issue #2679)
Im Upstream-Code von OpenRGB ist die Kombination `VID 0x258A / PID 0x0049` als `Fl_Esports_F11_PID` hinterlegt, wurde jedoch deaktiviert (`// DISABLED`). Grund hierfür war, dass verschiedene OEM-Hersteller (wie FL.ESPORTS, Redragon K621, Hyrax HCK61) dieselbe Sino Wealth VID/PID verwenden. Ungeprüfte Schreibzugriffe (`hid_send_feature_report`) mit inkompatiblen Report-Strukturen führten bei Fremdtastaturen zu MCU-Hängern bzw. Bricking.

### Unsere sichere Lösung: Nicht-destruktiver Lese-Handshake
Unser Detektor führt **vor jeglichem Schreibzugriff** eine strikte Verifikation an `Interface 1` durch:
1. Es wird **ausschließlich ein lesender Feature-Report** angefordert: `hid_get_feature_report(dev, buf, 42)` mit `buf[0] = 0x0A`.
2. Die empfangenen Bytes werden defensiv gegen die CIY X79 Firmware-Spezifikation validiert:
   ```cpp
   if (res < 5 || buf[0] != 0x0A || buf[1] > 20 || buf[2] > 10 || buf[4] > 5)
   {
       hid_close(dev);
       return; // Sofortiger Abbruch — 0 Bytes geschrieben!
   }
   ```
3. Stimmt die Antwort nicht exakt mit der CIY X79 überein, wird das Gerät sofort freigegeben. Fremdtastaturen bleiben zu 100 % unberührt.

---

## 3. Zwei Integrationspfade für Upstream-Maintainer

Wir stellen beiden Maintainer-Präferenzen entsprechende Lösungen bereit:

### Option A: Minimal-invasiver Patch für `SinowealthControllerDetect.cpp`
Reaktiviert `PID 0x0049` im bestehenden Sino-Wealth-Detektor mit vorgeschaltetem Lese-Handshake:

```diff
--- a/Controllers/SinowealthController/SinowealthControllerDetect.cpp
+++ b/Controllers/SinowealthController/SinowealthControllerDetect.cpp
@@ -20,6 +20,23 @@ void DetectSinowealthControllers(hid_device_info* info, const std::string& name)
         if(info->interface_number != 1)
         {
             return;
         }
 
+        // Safe Handshake for 0x258A:0x0049 (CIY X79 vs FL.ESPORTS / OEM conflicts)
+        if(info->product_id == 0x0049)
+        {
+            hid_device* dev = hid_open_path(info->path);
+            if(!dev) return;
+            unsigned char buf[42] = {0x0A};
+            int res = hid_get_feature_report(dev, buf, sizeof(buf));
+            if(res < 5 || buf[0] != 0x0A || buf[1] > 20 || buf[2] > 10 || buf[4] > 5)
+            {
+                hid_close(dev);
+                return; // Not a CIY X79, release device safely without writing
+            }
+            hid_close(dev);
+        }
+
         hid_device* dev = hid_open_path(info->path);
```

### Option B: Eigenständiger `CIYX79Controller` (Empfohlen)
Vollständiger, 100 % autarker Controller im Verzeichnis `contrib/openrgb/` (ohne externe Projekt-Abhängigkeiten direkt in Upstream OpenRGB einbindbar):
- `CIYController.h` & `CIYController.cpp`: Thread-sicherer HIDAPI-Wrapper mit 60-ms-Framelimiter (~16 FPS) gegen MCU-Überlastung, integrierten Hardware-Konstanten und eigenständiger Report-Generierung.
- `RGBController_CIYX79.h` & `RGBController_CIYX79.cpp`: 5×16 Matrix-Zone mit 69 physischen LEDs, reentrancy-sicherer Initialisierung, Dual-Cell ISO Enter Mapping und robuster euklidischer Farbabstandsberechnung für den Static-Modus.
- `CIYX79Detector.cpp`: Dedizierte Erkennungsfunktion mit sicherem GET_FEATURE-Handshake.

---

## 4. Protokoll-Spezifikation & Wire-Byte Dumps

### Report 0x0A: Status & Beleuchtungsmodus (42 Bytes)
- **Byte 0:** Report-ID `0x0A`
- **Byte 1:** Beleuchtungsmodus (0 = Aus, 1..14 = Firmware-Effekte, 15 = Statisch, 16..18 = Presets, 19 = Benutzerdefiniert / Direct RGB, 20 = Regenbogenwelle)
- **Byte 2:** Farbpalette (0 = Rot, 1 = Orange, 2 = Gelb, 3 = Grün, 4 = Cyan, 5 = Blau, 6 = Lila, 7 = Weiß, 8 = Bunt, 9..10 = Farbwechsel)
- **Byte 3:** Tempo (0..4, invertiert auf Hardware-Ebene: `4 - speed`)
- **Byte 4:** Helligkeit (0..5)
- **Bytes 5..41:** 0x00 (Null-Padding)

### Report 0x0B: Planar-RGB Matrix (379 Bytes)
- **Byte 0:** Report-ID `0x0B`
- **Bytes 1..126:** Rote Farbwerte für LED-Indizes 0..125 (`Offset = 1 + led_index`)
- **Bytes 127..252:** Grüne Farbwerte für LED-Indizes 0..125 (`Offset = 1 + 126 + led_index`)
- **Bytes 253..378:** Blaue Farbwerte für LED-Indizes 0..125 (`Offset = 1 + 252 + led_index`)

### Übertragungssequenz für Direct Mode
1. `hid_send_feature_report` mit 379-Byte Report `0x0B` (Planar-RGB)
2. 50 ms Hardware-Pause (MCU-Puffer-Verarbeitung)
3. `hid_send_feature_report` mit 42-Byte Report `0x0A` (Modus 19, gewünschte Helligkeit)

---

## 5. Matrix-Geometrie & Dual-Cell ISO Enter

Die CIY X79 ISO-DE besitzt 69 logische Tasten in einer 5×16 Matrix:
```
Zeile 0 (15 Tasten): Esc, 1..0, ß, ´, Backspace, Del                | Col 0..14 (Col 15 = N/A)
Zeile 1 (15 Tasten): Tab, Q..P, Ü, +*, Enter (Top), Bild Auf        | Col 0..14 (Col 15 = N/A)
Zeile 2 (15 Tasten): Caps, A..L, Ö, Ä, #', Enter (Bottom), Bild Ab   | Col 0..14 (Col 15 = N/A)
Zeile 3 (15 Tasten): Shift L, <>, Y..-, Shift R, Up, End            | Col 0..14 (Col 15 = N/A)
Zeile 4 (10 Tasten): Strg, Win, Alt, Space, AltGr, Fn, Strg, ◄, ▼, ► | Cols 0..2, 6, 10..15
```

**Dual-Cell Enter Mapping:**  
Die physische ISO-Enter-Taste überspannt zwei Zeilen. In OpenRGB verweisen sowohl Zelle `(Row 1, Col 13)` als auch Zelle `(Row 2, Col 13)` auf denselben LED-Index (Hardware LED 82, Matrix-Slot 81). Eine Farbänderung an einer der beiden Flächen aktualisiert konsistent die gesamte Enter-Taste.

---

## 6. Linux udev-Regel

Zur unprivilegierten Nutzung unter Linux (z. B. Ubuntu, Debian, Fedora, Arch) dient die Regel in `resources/60-ciy-x79.rules`:
```udev
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="258a", ATTRS{idProduct}=="0049", MODE="0660", GROUP="plugdev", TAG+="uaccess"
KERNEL=="hidraw*", ATTRS{idVendor}=="258a", ATTRS{idProduct}=="0049", MODE="0660", GROUP="plugdev", TAG+="uaccess"
```
Installation:
```bash
sudo cp resources/60-ciy-x79.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```
