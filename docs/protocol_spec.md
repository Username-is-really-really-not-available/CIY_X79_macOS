# CIY X79 (ISO-DE) - USB-HID Protokoll- & Hardware-Spezifikation

Spezifikation der USB-HID-Kommunikation, Datenformate, Hardware-Timings und LED-Matrix-Zuordnungen der CIY X79 Tastatur. Entwickelt als unabhängige Referenz für Treiberentwickler (z. B. OpenRGB, Linux `hid-core`, IOKit).

> [!IMPORTANT]
> **Gerätevalidierung & Abgrenzung (VID/PID `258A:0049`):**  
> Die USB-Kennung `VID 0x258A / PID 0x0049` (Sino Wealth HID-Controller) ist keine modellspezifische Kennung, sondern wird von mehreren unterschiedlichen OEM-Tastaturen verwendet (u. a. *Hyrax HCK61*, *Maroc4Gaming M68*, *Redragon K628Pro*).  
> **Diese Spezifikation ist strikt für die CIY X79 (ISO-DE, 69 Tasten) validiert.**  
> Wesentliches Unterscheidungsmerkmal: Während Geräte wie die *Hyrax HCK61* lediglich über ein einzelnes USB-Interface (`bNumInterfaces = 1`) verfügen, besitzt die CIY X79 zwei Interfaces:  
> - **Interface 0:** Standard USB-Boot-Tastatur (Tastatureingaben via HID-Interrupt-Transfers)  
> - **Interface 1:** Vendor-Defined HID-Klasse (RGB- & Konfigurationssteuerung via Feature-Reports)  
> Universelle Treiber wie OpenRGB dürfen Geräte mit `258A:0049` daher nicht allein anhand von VID/PID initialisieren, sondern müssen Interface 1 und den Report-Deskriptor prüfen.

---

## 1. USB-Identifikatoren & Low-Level Endpoints

* **Vendor ID (VID):** `0x258A` (Sino Wealth)
* **Product ID (PID):** `0x0049`
* **Target Interface:** Interface `1` (Vendor-Defined HID Class; `bInterfaceClass = 3`, `bInterfaceSubClass = 0`, `bInterfaceProtocol = 0`)
* **Transfer-Methode:** USB Control Transfers an Endpoint 0
  * **Set Feature:** `bmRequestType = 0x21` (`kUSBOut | kUSBClass | kUSBInterface`), `bRequest = 0x09` (`kHIDRqSetReport`), `wValue = (kHIDReportTypeFeature << 8) | ReportID`, `wIndex = 1`
  * **Get Feature:** `bmRequestType = 0xA1` (`kUSBIn | kUSBClass | kUSBInterface`), `bRequest = 0x01` (`kHIDRqGetReport`), `wValue = (kHIDReportTypeFeature << 8) | ReportID`, `wIndex = 1`

---

## 2. Report-Definitionen & Wire-Formate

### Report 0x0A (42 Bytes) – Globale Beleuchtungssteuerung
Wird per `Get Feature` (Lesen des aktuellen Status) und `Set Feature` (Setzen) übertragen:
* `Byte 0`: `0x0A` (Report-ID)
* `Byte 1`: Modus (`0..20`):
  * `0` (`0x00`): Aus / Off
  * `1..14` (`0x01..0x0E`): Firmware-Animationseffekte (Welle, Atmung, Neon, Schlange etc.)
  * `15` (`0x0F`): Statisch / Solid (Einzelfarbe aus der Farbpalette in `Byte 2`)
  * `16..18` (`0x10..0x12`): Firmware-Effekte (Wellenbrecher, Glühen, Funkenflug)
  * `19` (`0x13`): Customize (Planare Per-Key RGB-Matrix aus dem MCU-XRAM)
  * `20` (`0x14`): Ripple / Regenbogen-Welle
* `Byte 2`: Farbpalette (`0..10`, Preset-Farben; aktiv bei Modus 15 und Einzelfarb-Animationen)
* `Byte 3`: Geschwindigkeit invertiert: `4 - speed` (Wertebereich `0..4`, `0` = langsamste Stufe, `4` = schnellste Stufe)
* `Byte 4`: Helligkeit (`0` = dunkel/aus bis `5` = maximale Helligkeit)
* `Bytes 5..41`: Reserviert / Padding (`0x00`)

### Report 0x0B (379 Bytes) – Planare Per-Key RGB-Matrix
Wird ausschließlich per `Set Feature` an Interface 1 übertragen:
* `Byte 0`: `0x0B` (Report-ID)
* `Bytes 1..126`: **Ebene 0: ROT** (Helligkeitswerte `0x00..0xFF` für LED-Index `0..125`)
* `Bytes 127..252`: **Ebene 1: GRÜN** (Helligkeitswerte `0x00..0xFF` für LED-Index `0..125`)
* `Bytes 253..378`: **Ebene 2: BLAU** (Helligkeitswerte `0x00..0xFF` für LED-Index `0..125`)

> [!NOTE]
> **Rolle von Report 0x0B:**  
> Report 0x0B ist ein reiner Speicher-Schreibbefehl in den flüchtigen XRAM der MCU. Er setzt **nicht** voraus, dass Modus 19 bereits vor der Übertragung aktiv ist.  
> Vielmehr wird zuerst Report 0x0B übertragen, um den XRAM-Speicher zu füllen. Erst danach weist Report 0x0A mit Modus 19 die Firmware an, die LEDs aus diesem Puffer zu rendern.

### Report 0x09 (505 Bytes) – Tastenbelegung & Key-Remapping
Wird per `Get Feature` / `Set Feature` an Interface 1 übertragen:
* `Byte 0`: `0x09` (Report-ID)
* `Bytes 1..504`: 126 Tasten-Slots à 4 Bytes (Scancode-Mapping pro Matrix-Slot).

---

## 3. Hardware-Timing & Sende-Reihenfolge (Modus 19)

Beim Setzen benutzerdefinierter Farben muss folgende Reihenfolge eingehalten werden:

```text
[Host] -- Report 0x0B (379 Bytes Set Feature) ------> [MCU: XRAM-Puffer füllen]
[Host] -- Pause ~50 ms (konservative Wartezeit) ------> [MCU: Pufferübernahme / Latch]
[Host] -- Report 0x0A (42 Bytes, Modus 19) ----------> [MCU: LEDs auf XRAM umschalten]
```

1. **Schritt 1:** `Report 0x0B` (379 Bytes Planar RGB) an Interface 1 senden (schreibt Rohfarbwerte in den flüchtigen XRAM).
2. **Schritt 2: Konservative Wartezeit von 50 ms** (`usleep(50000)` / `Sleep(50)`):
   * **Empirische Messung:** Am realen Testgerät führten Pausen unter $30\text{ ms}$ reproduzierbar zum Verwerfen des Puffers (MCU verbleibt im vorherigen Effekt). Ab $\ge 40\text{ ms}$ erfolgte die Pufferübernahme in Testläufen stabil.
   * **Einordnung des 50-ms-Werts:** Es liegt kein Beleg für eine zwingende Mindestdauer von exakt 50 ms vor; die tatsächliche Mindestlatenz liegt empirisch im Bereich von $30\text{ bis }40\text{ ms}$. Der Wert von 50 ms dient als konservative Wartezeit mit Sicherheitsmarge (Puffer für Host-Scheduler-Schwankungen und USB-Jitter) und entspricht dem im offiziellen Windows-OEM-Treiber vorgefundenen Aufruf `Sleep(50)`.
3. **Schritt 3:** `Report 0x0A` (42 Bytes, Modus 19, gewählte Helligkeit) an Interface 1 senden (Firmware schaltet die LED-Treiber auf den XRAM-Puffer um).

* **Fehlerfolge bei zu kurzem Delay:** Wird das Intervall $< 30\text{ ms}$ gewählt oder die Pause ausgelassen, verwirft die MCU den neuen XRAM-Puffer und schaltet in eine werkseitige Standard-Animation zurück.
* **Volatiler XRAM:** Die Per-Key-RGB-Matrix liegt ausschließlich im flüchtigen SRAM der MCU. Hardware-Shortcuts an der Tastatur (z. B. `Fn + ;` für Effekt-Cycling) überschreiben diesen Speicher sofort mit internen Firmware-Mustern.

---

## 4. Indexierung & Adressierungsarchitektur

Die CIY X79 unterscheidet strikt zwischen vier Adressierungsräumen:

| Begriff | Wertebereich | Ebene | Beschreibung |
|---|---|---|---|
| **Matrix-Slot** | `0..124` | Hardware PCB | Logischer Kreuzungspunkt der $6 \times 21$ Tastenmatrix-Schaltung. |
| **LED-Index** | `0..125` | Puffer-Array | 0-basierter Index im 126-Byte-Array jeder Farbebene von Report 0x0B. |
| **Report-Byte-Offset** | `0..378` | USB-Wire-Format | Absoluter Byte-Offset im übertragenen 379-Byte Feature-Report. |
| **XRAM-Adresse** | `0x0A60` ff. | MCU-Firmware | Physische Byte-Adresse im internen 8051-XRAM der Sino-Wealth-MCU. |

### Der Hardware +1 Index-Offset & die Esc-Spiegelung

Die Tastenmatrix der Tastatur ist elektrisch in Spalten zu je 6 Zeilen organisiert (`Matrix_Slot = Spalte * 6 + Zeile`):
* Die erste physische Taste in Spalte 0 ist **`Esc` auf Matrix-Slot 1** (Zeile 1).
* Matrix-Slot 0 (Zeile 0 in Spalte 0) ist auf dem PCB **nicht mit einem Taster bestückt**.

Die Sino-Wealth-Firmware ordnet dem PCB-Matrix-Slot $S$ den LED-Puffer-Index $I$ im planaren Report über einen festen Versatz zu:
$$\text{LED\_Index} = \text{Matrix\_Slot} + 1$$

```text
PCB Matrix-Slot:         [ Slot 0 (leer) ]   [ Slot 1 (Esc) ]   [ Slot 2 (Tab) ]  ...  [ Slot 14 (W) ]
Firmware XRAM-Adresse:    0x0A60              0x0A61             0x0A62            ...   0x0A6E
Report 0x0B LED-Index:   Index 0 (Header)    Index 1 (Spiegel)  Index 2 (Esc)           Index 15 (W)
```

**Bedeutung der Indizes im Report 0x0B:**
* **LED-Index 0:** Entspricht keinem Matrix-Slot ($S = -1$); dient als Null-Offset im Puffer.
* **LED-Index 1:** Entspräche Slot 0 ($0 + 1 = 1$). Da Slot 0 auf dem PCB physisch nicht existiert und die Firmware intern `XRAM[0x0A60]` mit `XRAM[0x0A61]` spiegelt, steuert Index 1 keine separate LED an.
* **LED-Index 2:** Physische LED der Taste **`Esc`** ($\text{Slot } 1 + 1 = 2$).
* **LED-Index 3:** Physische LED der Taste **`Tab`** ($\text{Slot } 2 + 1 = 3$).

**Absolute Byte-Offsets im USB-Report 0x0B für Taste $K$ mit LED-Index $I$:**
* Rot-Kanal: `Byte[1 + I]` (für `Esc`: Byte 3)
* Grün-Kanal: `Byte[127 + I]` (für `Esc`: Byte 129)
* Blau-Kanal: `Byte[253 + I]` (für `Esc`: Byte 255)

### Durchgängige mathematische Zuordnungsformel

Für jeden gültigen Matrix-Slot $S \in [0, 124]$ ergibt sich die Abbildung über alle vier Schichten deterministisch:

1. **Report-Array-Index $I(S)$** ($0$-basierter Index pro Farbebene im 126-Byte Payload):
   $$I(S) = S + 1 \quad \text{mit } I \in [1, 125]$$
2. **Report-Byte-Offset $B_c(S)$** ($0$-basierter Index im 379-Byte USB-Report `buf[0..378]`):
   Für Farbebene $c \in \{0, 1, 2\}$ ($\text{Rot} = 0$, $\text{Grün} = 1$, $\text{Blau} = 2$) mit Byte 0 als Report-ID `0x0B`:
   $$B_c(S) = 1 + c \cdot 126 + I(S) = 1 + c \cdot 126 + (S + 1) = S + 2 + c \cdot 126$$
   * **Rot-Kanal ($c = 0$):** $B_{\text{Rot}}(S) = S + 2$
   * **Grün-Kanal ($c = 1$):** $B_{\text{Grün}}(S) = S + 128$
   * **Blau-Kanal ($c = 2$):** $B_{\text{Blau}}(S) = S + 254$
3. **Firmware XRAM-Adresse $\text{XRAM}_c(S)$** (Sino-Wealth 8051 interner RAM):
   Basisadresse `0x0A60`, Ebenenlänge $126 = 0x7E$ Bytes:
   $$\text{XRAM}_c(S) = 0x0A60 + c \cdot 0x7E + S$$
   * **Rot-Kanal ($c = 0$):** $\text{XRAM}_{\text{Rot}}(S) = 0x0A60 + S$
   * **Grün-Kanal ($c = 1$):** $\text{XRAM}_{\text{Grün}}(S) = 0x0ADE + S$
   * **Blau-Kanal ($c = 2$):** $\text{XRAM}_{\text{Blau}}(S) = 0x0B5C + S$

### Behandlung von Report-Array-Index 0

* **Mathematisch / Adressierung:** Da $I = S + 1$ mit $S \ge 0$ strikt $I \ge 1$ bedingt, existiert **kein Matrix-Slot $S$**, der auf $I = 0$ abbildet ($S = -1$ ist unzulässig).
* **Wire-Offsets von $I = 0$:**
  * Rot: `buf[1]` ($B_0 = 1 + 0$)
  * Grün: `buf[127]` ($B_1 = 127 + 0$)
  * Blau: `buf[253]` ($B_2 = 253 + 0$)
* **Host-Treiber:** Wird im Treiber (`ciy_send_planar_rgb`) durch `memset(rgb, 0, 378)` vor jedem Profilaufbau statisch auf `0x00` gehalten.
* **Firmware / Hardware:** Dient firmwareintern als Puffer-Padding / Dummy-Header. Die Sinowealth-PWM-Engine beginnt die zyklische LED-Ausgabe bei Adresse `0x0A61` (`Esc`) und ignoriert den Index 0 im LED-Refresh.

### Verifikation: Slot 0, Esc (Slot 1) und Tab (Slot 2)

| Parameter | $I = 0$ (Dummy-Header) | Matrix-Slot 0 (unbestückt) | Esc (Slot 1) | Tab (Slot 2) |
|---|---|---|---|---|
| **Matrix-Slot $S$** | *Kein Slot ($S = -1$)* | `0` | `1` | `2` |
| **Report-Array-Index $I$** | `0` | `1` ($0 + 1$) | `2` ($1 + 1$) | `3` ($2 + 1$) |
| **Byte-Offset Rot ($c=0$)** | `buf[1]` | `buf[2]` | `buf[3]` | `buf[4]` |
| **Byte-Offset Grün ($c=1$)** | `buf[127]` | `buf[128]` | `buf[129]` | `buf[130]` |
| **Byte-Offset Blau ($c=2$)** | `buf[253]` | `buf[254]` | `buf[255]` | `buf[256]` |
| **XRAM Rot ($c=0$)** | *N/A (nicht im Puffer)* | `0x0A60` | `0x0A61` | `0x0A62` |
| **XRAM Grün ($c=1$)** | *N/A (nicht im Puffer)* | `0x0ADE` | `0x0ADF` | `0x0AE0` |
| **XRAM Blau ($c=2$)** | *N/A (nicht im Puffer)* | `0x0B5C` | `0x0B5D` | `0x0B5E` |
| **Physische Taste** | Keine | Keine (PCB unbestückt) | **`Esc`** (Reihe 0, Spalte 0) | **`Tab`** (Reihe 1, Spalte 0) |
| **Implementierung** | Immer `0x00` | Akzeptiert in JSON-Parser | `KEYS[0] = {1, 2, "Esc"}` | `KEYS[15] = {2, 3, "⇥ Tab"}` |
| **Hardware-Nachweis** | Im PWM-Zyklus ignoriert | `0x0A60` spiegelt `0x0A61` | Physische LED leuchtet | Physische LED leuchtet |

**Wichtige Referenz-Tasten:**
* `Esc`: Matrix-Slot 1 $\rightarrow$ LED-Index **2**
* `W`: Matrix-Slot 14 $\rightarrow$ LED-Index **15**
* `A`: Matrix-Slot 9 $\rightarrow$ LED-Index **10**
* `S`: Matrix-Slot 15 $\rightarrow$ LED-Index **16**
* `D`: Matrix-Slot 21 $\rightarrow$ LED-Index **22**
* Pfeil ▲: Matrix-Slot 88 $\rightarrow$ LED-Index **89**
* Pfeil ◄: Matrix-Slot 83 $\rightarrow$ LED-Index **84**
* Pfeil ▼: Matrix-Slot 89 $\rightarrow$ LED-Index **90**
* Pfeil ►: Matrix-Slot 95 $\rightarrow$ LED-Index **96**

### Puffergrenze & Matrix-Slot 125

Eine $6 \times 21$ Matrix besitzt theoretisch 126 Kreuzungspunkte (Slots `0..125`). Im Scancode-Remapping (Report 0x09) sind auch alle 126 Slots vorhanden. Für die RGB-Steuerung (Report 0x0B) ist der adressierbare Bereich jedoch strikt auf **`Matrix-Slot 0..124`** begrenzt:

1. **Firmware-Puffergrenze (Off-by-One Schutz):**  
   Report 0x0B stellt pro Farbebene genau 126 Bytes (`LED_Index 0..125`) bereit. Da $\text{LED\_Index} = \text{Matrix\_Slot} + 1$ gilt und `LED_Index 0` als Firmware-Offset reserviert ist, belegt Slot **124** den letzten Pufferplatz `LED_Index 125`:
   $$\text{Matrix\_Slot } 124 \rightarrow \text{LED\_Index } 125 \quad (\text{Byte 126 Rot, Byte 252 Grün, Byte 378 Blau})$$
   Ein hypothetischer `Matrix-Slot 125` würde auf `LED_Index 126` abgebildet. Dies liegt außerhalb der 126-Byte-Ebene und würde im Übertragungspuffer die nachfolgende Farbebene überschreiben (Rot in Grün-Kanal, Grün in Blau-Kanal, Blau über das Report-Ende). Host-Treiber müssen Slots $> 124$ daher zwingend verwerfen.
2. **Hardware-Bestückung der CIY X79:**  
   Die CIY X79 ist ein kompaktes 69-Tasten-Keyboard (65%-Layout). Physisch auf dem PCB bestückt sind ausschließlich Matrix-Slots von **Slot 1 (Esc) bis Slot 95 (Pfeiltaste ►)**. Die Matrix-Slots **96 bis 125 sind auf dem PCB der CIY X79 physisch weder mit Tastern noch mit LEDs bestückt**. Dass Slot 125 im RGB-Protokoll nicht darstellbar ist, betrifft daher keine reale Hardware-Taste.

---

## 5. Beleuchtungsmodi (0..20)

| Modus (Dez / Hex) | Name | Typ | Verhalten / Quelle |
|---|---|---|---|
| `0` (`0x00`) | Aus / Off | Inaktiv | LEDs dunkel |
| `1` (`0x01`) .. `14` (`0x0E`) | Animationseffekte (Welle, Atmung, Neon, Schlange etc.) | Firmware-Presets | Intern generierte Firmware-Muster |
| `15` (`0x0F`) | Statisch / Solid | Globale Einzelfarbe | Verwendet Farbpalette aus `Byte 2` |
| `16` (`0x10`) .. `18` (`0x12`) | Wellenbrecher, Glühen, Funkenflug | Firmware-Presets | Intern generierte Firmware-Muster |
| `19` (`0x13`) | **Customize (Per-Key RGB)** | XRAM-Puffer | Visualisiert den per `Report 0x0B` gesendeten Puffer |
| `20` (`0x14`) | Ripple / Regenbogen-Welle | Firmware-Animation | Reaktiv auf Tastenanschläge |

---

## 6. Tastenmatrix & LED-Indizes (ISO-DE, 69 Tasten)

Vollständiges Mapping aller 69 physischen Tasten der CIY X79 ISO-DE:

| Reihe | Tasten | Matrix-Slots (`0..124`) | USB-LED-Indizes (`Slot + 1`) |
|---|---|---|---|
| **Reihe 0** | Esc, 1..0, ß, ´, ⌫ Backspace, Entf | 1, 7, 13, 19, 25, 31, 37, 43, 49, 55, 61, 67, 73, 79, 86 | 2, 8, 14, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74, 80, 87 |
| **Reihe 1** | Tab, Q, W, E, R, T, Z, U, I, O, P, Ü, + \*, Enter (oben), Bild ⇡ | 2, 8, 14, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74, 81, 87 | 3, 9, 15, 21, 27, 33, 39, 45, 51, 57, 63, 69, 75, 82, 88 |
| **Reihe 2** | Caps, A, S, D, F, G, H, J, K, L, Ö, Ä, # ', Enter (unten), Bild ⇣ | 3, 9, 15, 21, 27, 33, 39, 45, 51, 57, 63, 69, 75, 81, 94 | 4, 10, 16, 22, 28, 34, 40, 46, 52, 58, 64, 70, 76, 82, 95 |
| **Reihe 3** | Shift L, < > \|, Y, X, C, V, B, N, M, , ;, . :, - _, Shift R, ▲, Ende | 4, 76, 10, 16, 22, 28, 34, 40, 46, 52, 58, 64, 82, 88, 91 | 5, 77, 11, 17, 23, 29, 35, 41, 47, 53, 59, 65, 83, 89, 92 |
| **Reihe 4** | Strg L, Win/Cmd, Alt L, Leertaste, AltGr, Fn, Strg R, ◄, ▼, ► | 5, 11, 17, 35, 53, 77, 80, 83, 89, 95 | 6, 12, 18, 36, 54, 78, 81, 84, 90, 96 |

> [!NOTE]
> Die ISO-Enter-Taste belegt physisch Matrix-Slot 81 (LED-Index 82). Im UI-Tastaturlayout wird sie aus zwei Darstellungsflächen (1.5U in Reihe 1 und 1.25U in Reihe 2) zusammengesetzt.

---

## 7. Verifizierungsstatus & Evidenzbasis

Zur eindeutigen Nachvollziehbarkeit für Treiberentwickler unterscheidet diese Spezifikation strikt zwischen empirisch am Gerät beobachteten Ergebnissen, Disassembly-/Trace-Befunden und mathematischen Ableitungen:

| Protokoll-Merkmal | Methodischer Status | Konkreter Nachweis / Beleg |
|---|---|---|
| **USB-Endpunkte & Dual-Interface (`258A:0049`)** | **Am realen Gerät gemessen** | USB-Deskriptor-Analyse via IOKit (`ioreg`) und `lsusb -v`: Bestätigt 2 Interfaces; Interface 1 (`bInterfaceClass = 3`, `bInterfaceSubClass = 0`) antwortet auf Feature-Reports 0x0A und 0x0B. |
| **Report 0x0A (42 Bytes, Modi 0..20, Palette, Speed, Brightness)** | **Am realen Gerät gemessen** | Alle Modi 0..20 sowie Parameter über `ciy_ctl set` an realer Hardware angesteuert; Status-Rücklesung via `Get Feature` erfolgreich verifiziert. |
| **Report 0x0B (379 Bytes Planar RGB)** | **Am realen Gerät gemessen** | Planare 3-Ebenen-Payloads übertragen; LEDs leuchten mit den gesetzten 8-Bit-RGB-Werten. |
| **Konservative Wartezeit (~50 ms) vor Modus 19** | **Am realen Gerät & OEM-Treiber** | *Gerätemessung:* Delays $< 30\text{ ms}$ führen reproduzierbar zu Pufferverlust; ab $\ge 40\text{ ms}$ stabil. 50 ms dient als konservative Empfehlung.<br>*OEM-Code:* Der offizielle Windows-Treiber (`X79.exe`) führt vor Modus 19 explizit `Sleep(50)` aus. |
| **Volatiler XRAM (Fn + ; Überschreibung)** | **Am realen Gerät gemessen** | Nach manuellem Druck von `Fn + ;` an der physischen Tastatur wird der Per-Key-Puffer sofort durch die interne Firmware-Animation ersetzt. |
| **Tastenmatrix aller 69 Tasten (ISO-DE)** | **Am realen Gerät gemessen** | Jede Taste wurde isoliert mit Einzelfarbe (`#FF0000` auf `#000000`) per `scripts/hardware_diagnostics.py` angesteuert und optisch verifiziert. |
| **XRAM-Basisadresse `0x0A60` & Esc-Spiegelung** | **Reverse Engineering (Disassembly)** | Disassembly der Sino-Wealth-8051-Firmware: Endpoint-Routine kopiert Report 0x0B ab Adresse `0x0A60` und spiegelt `0x0A60` mit `0x0A61`. |
| **Report 0x09 (Key-Remapping, 505 Bytes)** | **Reverse Engineering (USB-Trace)** | Wireshark/USBPcap-Trace des OEM-Windows-Treibers beim Tasten-Remapping: 126 Slots $\times$ 4 Bytes + 1 Byte Report-ID. |
| **Formel `LED_Index = Matrix_Slot + 1`** | **Aus Matrix-Modell abgeleitet** | Resultiert aus der $6 \times 21$ PCB-Verschaltung (Slot 0 unbestückt) in Kombination mit der internen Firmware-Spiegelung. |

### 7.1 Testumfang, Hardwarestand & Messmethode des Timings

* **Geprüfter Hardware- & Firmwarestand:**
  * Modell: CIY X79 (ISO-DE, 69 physische Tasten)
  * USB-Identifikatoren: VID `0x258A`, PID `0x0049`, Geräterevision (`bcdDevice`) `0x0100`
  * USB-Interfaces: Interface 0 = Boot Keyboard (`bInterfaceClass = 3`, `bInterfaceSubClass = 1`, `bInterfaceProtocol = 1`), Interface 1 = Vendor-Defined HID (`03:00:00`)
  * Controller-Typ: Sino Wealth 8051-kompatibler USB-Controller (SH68F90 / SH68F88 Plattform)
  * USB-String-Deskriptoren: iManufacturer = „SinoWealth“, iProduct = „Gaming Keyboard“
* **Messmethode (Host-Ebene):**
  * Plattform: macOS 12.0+ (getestet auf Apple Silicon M-Serie und Intel x86_64) via IOKit Control Transfers (`IOHIDDeviceSetReport` an Endpoint 0).
  * Testsequenz: Übertragung von Report 0x0B $\rightarrow$ Host-Pause via `nanosleep()` / `usleep()` variiert von $0\text{ ms}$ bis $60\text{ ms}$ in 5-ms- bzw. 10-ms-Schritten $\rightarrow$ Übertragung von Report 0x0A (Modus 19) $\rightarrow$ optische Prüfung der LED-Aktivierung sowie Status-Rücklesung via `Get Feature`.
  * Empirischer Befund:
    * $\Delta t < 30\text{ ms}$: Reproduzierbarer Pufferverlust (MCU ignoriert Report 0x0B und bleibt in bestehender Animation).
    * $\Delta t = 30\text{ ms}$: Sporadische Übernahme, instabil.
    * $\Delta t \ge 40\text{ ms}$: 100% stabile Pufferübernahme in allen wiederholten Testreihen.
* **OEM-Treiber-Befund (Reverse Engineering):**
  * Disassembly der offiziellen Windows-Software (`X79.exe`): Vor dem Absenden des Reports 0x0A für Modus 19 ist ein expliziter Win32-API-Aufruf `Sleep(50)` fest einkompiliert.
* **Methodische Grenzen:**
  * Da keine Hardware-Logikanalysatoren an den internen Bussen des Mikrocontrollers angebracht wurden, basiert dieser Wert auf dem extern beobachteten Host-Verhalten. Die hardwareseitige Mindestlatenz liegt empirisch bei ca. 30–40 ms; die 50 ms sind eine konservativ gewählte Empfehlung mit Sicherheitsreserve.

---

> [!TIP]
> **Hinweis für Applikationsentwickler:**  
> Host-spezifische Implementierungsdetails der vorliegenden macOS-Referenzapp (z. B. der Multi-User Bus-Lock `/tmp/ciy_x79_bus_<UID>.lock`, die Display-P3 Farbraumklemmung gegen Floating-Point-Überläufe oder der Mock-Treiber für automatisierte CI/CD-Tests) sind in der internen Dokumentation ([`docs/agent/hardware-protocol.md`](file:///Users/juliengenau/Agentic%20Sandbox/CIY_X79_macOS/docs/agent/hardware-protocol.md) und [`docs/agent/data-formats.md`](file:///Users/juliengenau/Agentic%20Sandbox/CIY_X79_macOS/docs/agent/data-formats.md)) dokumentiert. Sie sind nicht Bestandteil des USB-Drahtprotokolls.
