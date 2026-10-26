# CIY X79 RGB macOS Steuerung & Protokolldokumentation

Eine native, vollständige macOS-Lösung (Apple Silicon & Intel) zur Ansteuerung der Beleuchtung, Effekte und individueller Tasten-Farbprofile (z. B. WASD-Gaming-Profil) für die mechanische Tastatur **CIY X79** (ISO-DE Layout, 65%, 69 Tasten).

**Sprachen / Languages:** [🇩🇪 Deutsch](README.md) | [🇬🇧 English](README_EN.md) | [🇨🇳 简体中文](README_ZH.md) | [🇷🇺 Русский](README_RU.md)

---

## Inhaltsverzeichnis
1. [Übersicht, Layout-Fokus & Schnellstart](#1-übersicht-layout-fokus--schnellstart)
2. [Installation & macOS Gatekeeper (Erststart)](#2-installation--macos-gatekeeper-erststart)
3. [Die gelösten Kernprobleme & Hardware-Erkenntnisse](#3-die-gelösten-kernprobleme--hardware-erkenntnisse)
4. [USB-HID Protokoll-Referenz](#4-usb-hid-protokoll-referenz)
5. [Vollständige Tastenmatrix & LED-Indizes (69 Tasten ISO-DE)](#5-vollständige-tastenmatrix--led-indizes-69-tasten-iso-de)
6. [Alle Beleuchtungsmodi & Farbpaletten](#6-alle-beleuchtungsmodi--farbpaletten)
7. [Die macOS-App (CIYX79Control.app)](#7-die-macos-app-ciyx79controlapp)
8. [Das Terminal-Tool (ciy_ctl)](#8-das-terminal-tool-ciy_ctl)
9. [Architektur & Schichtenmodell](#9-architektur--schichtenmodell)
10. [Automatisierte Tests & Hardware-Diagnose](#10-automatisierte-tests--hardware-diagnose)
11. [Kompilierung & Build-Prozess](#11-kompilierung--build-prozess)
12. [Sicherheit, Datenschutz & Ressourcen](#12-sicherheit-datenschutz--ressourcen)
13. [Lizenz, Markenhinweis & Haftungsausschluss](#13-lizenz-markenhinweis--haftungsausschluss)

---

## 1. Übersicht, Layout-Fokus & Schnellstart

* **Tastatur-Modell:** CIY X79 mechanische Tastatur (65% Formfaktor, Hot-Swap)
* **Mikrocontroller:** Sinowealth MCU (8051-basierter Controller)
* **USB-Identifikatoren:**
  * **Vendor ID (VID):** `0x258A` (Sino Wealth)
  * **Product ID (PID):** `0x0049`
  * **USB Interface:** Interface `1` (Vendor-Defined HID Class)

### Layout-Fokus: Deutsches ISO-DE Layout (69 physische Tasten)
Diese Software wurde speziell für die **deutsche ISO-DE Version** der CIY X79 entwickelt:
* **Exakte Tastenanzahl (69 Tasten):** Während 65%-Tastaturen im ANSI-US-Format meist 67 oder 68 Tasten besitzen, umfasst das deutsche ISO-DE Layout exakt **69 physische Tasten** mit eigenen Schaltern und einzeln steuerbaren RGB-LEDs:
  * 61 Tasten im Haupt-Schreibblock (inkl. zweizeiligem ISO-Enter, `< > |` neben linker Shift-Taste und `# '` in Reihe 2)
  * 8 Tasten im Navigations- und Pfeilblock (`Entf`, `BildAuf`, `BildAb`, `Ende` + 4 Pfeiltasten).
* **ISO-Besonderheiten:** Vollständige native Unterstützung aller deutschen Umlaute (`Ä`, `Ö`, `Ü`, `ß`), Akzente (`´ \``) und Sonderzeichen.
* **Unterschied zu ANSI-US:** Die internationale ANSI-US Version der CIY X79 besitzt nur 67 oder 68 Tasten (flache einzeilige Enter-Taste, keine `< > |`-Taste). Das USB-HID-Protokoll ist identisch; der interaktive Canvas der GUI und die Diagnose-Tools bilden jedoch das authentische deutsche ISO-Layout ab.

### Kompatibilitätsmatrix & Taxonomie

Um klare Erwartungen zu setzen und ungetestete Hardware-Behauptungen zu vermeiden, nutzt dieses Projekt eine verbindliche 4-Stufen-Taxonomie:

* **Maintainer-verified:** Modell, Revision und alle Kernfunktionen wurden physisch vom Maintainer auf echter Hardware unter macOS verifiziert.
* **Community-verified:** Ein nachvollziehbarer Hardware-Testbericht eines Besitzers liegt vor (inkl. macOS-Version, CPU-Architektur und Protokollnachweis); der physische Maintainer-Test steht noch aus.
* **Experimental / unverified:** Unbestätigte Unterstützung (z. B. auf Basis von Firmware-/MCU-Ähnlichkeiten oder theoretischen Mappings); keine Funktionsgarantie.
* **Unsupported:** Ausdrücklich reserviert für nachweislich inkompatible Geräte oder Revisionen (z. B. inkompatible MCU, abweichende USB-Paketformate oder fehlgeschlagene Hardware-Tests) – nicht für bloß ungetestete Modelle.

| Modell | Layout | VID:PID | Interface | Status | Getesteter Umfang | Belege / Referenz |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **CIY X79** | ISO-DE (69 Tasten) | `0x258A:0x0049` | 1 | **Maintainer-verified** | Status, Modi 0..20, Custom Planar RGB, Hotplug Re-Sync, macOS 12..15 (arm64 & x86_64) | Baseline v1.0 / v1.1 |
| **CIY X79** | ANSI-US (68 Tasten) | `0x258A:0x0049` | 1 | **Experimental / unverified** | Protokoll identisch vermutet; physische Tastenmatrix & LED-Mapping ausstehend | [Issue-Vorlage](.github/ISSUE_TEMPLATE/device_layout_request.md) |
| **Generische Sinowealth Tastaturen** | Diverse | Diverse | * | **Unsupported** | Firmware-Protokolle weichen ab; Fail-Closed-Schutz verhindert Fehlzugriffe | Protokoll-Spec |

### Sofortige Nutzung:

* **Grafische Benutzeroberfläche:**
  Doppelklick auf [CIYX79Control.app](file:///Users/juliengenau/Agentic%20Sandbox/CIY_X79_macOS/CIYX79Control.app) im Finder. Die App startet direkt im macOS Dock mit interaktiver Tastatur-Vektoransicht und Profilverwaltung.
* **Terminal-Tool (CLI):**
  ```bash
  # Status abfragen (mit Verbindungsprüfung)
  ./ciy_ctl status
  ./ciy_ctl status --json

  # Vollflächige Einzelfarbe mit Helligkeitsflag (-b 0..5)
  ./ciy_ctl fill "#FF5500" -b 4

  # WASD-Gaming-Profil aktivieren (WASD Rot, Basis Blau)
  ./ciy_ctl wasd

  # Kombiniertes Profil (WASD Rot, Pfeiltasten Gelb, Basis Blau)
  ./ciy_ctl wasd-arrows

  # Vollflächig statisch Rot
  ./ciy_ctl all-red
  ```

---

## 2. Installation & macOS Gatekeeper (Erststart)

Da dieses Tool als kostenloses Open-Source-Projekt ohne kostenpflichtiges Apple-Entwicklerzertifikat (99 €/Jahr) bereitgestellt wird, greift beim ersten Start auf anderen Macs der macOS-Sicherheitsdienst **Gatekeeper** (*„Entwickler kann nicht verifiziert werden“* oder *„App ist beschädigt“*).

### Einmalige Freischaltung für Drittnutzer:

* **Methode 1 (Grafisch – Empfohlen):**
  1. Die heruntergeladene `CIYX79Control.app` in den Ordner `/Applications` (Programme) verschieben.
  2. Im Finder mit **Rechtsklick** (oder `Control` + Klick) auf die App klicken und **„Öffnen“** wählen.
  3. Im Dialogfenster auf **„Öffnen“** (bzw. *„Trotzdem öffnen“*) klicken. Danach startet die App künftig immer direkt per einfachem Doppelklick.

* **Methode 2 (Terminal):**
  Das von macOS bei Downloads gesetzte Quarantäne-Attribut kann mit einem einzigen Terminal-Befehl entfernt werden:
  ```bash
  xattr -cr /Applications/CIYX79Control.app
  ```

---

## 3. Die gelösten Kernprobleme & Hardware-Erkenntnisse

Zur Herstellung der Interoperabilität unter macOS (gemäß § 69e UrhG und § 3 GeschGehG) wurde die USB-HID-Kommunikation und Steuerungslogik der CIY X79 analysiert. Dabei wurden vier entscheidende Hardware-Besonderheiten aufgedeckt:

### A. Das Modus-Mysterium (Warum Regenbogen statt Custom?)
* **Modus 19 (`0x13`):** Ist das offizielle Profil `<tc_916_led14> Customize </tc_916_led14>`. Nur in diesem Modus liest der Controller die individuellen RGB-Farbwerte aus seinem internen XRAM-Speicher!
* **Modus 20 (`0x14`):** Ist `<tc_916_led2> Ripple </tc_916_led2>` (eine animierte Regenbogen-Welle). Wurde zuvor fälschlicherweise Modus 20 adressiert, schaltete die Tastatur absichtlich in diesen Animationseffekt.
* **Modus 15 (`0x0F`):** Ist `<tc_916_led15> Solid </tc_916_led15>` (statische Einzelfarbe für das gesamte Board aus der vordefinierten Farbpalette).

### B. Die verbindliche Sende-Reihenfolge
Wird der Modus 19 aktiviert, bevor der Farbspeicher gefüllt und synchronisiert ist, greift die Firmware auf Standardeffekte zurück. Die offizielle Reihenfolge lautet zwingend:
1. **`Report 0x0B` (379 Bytes)** senden: Schreibt die RGB-Farbebenen in den XRAM.
2. **50 ms Pause** (`usleep(50000)` / `Sleep(50)`): Der Mikrocontroller verarbeitet und übernimmt den Puffer.
3. **`Report 0x0A` (42 Bytes)** mit Modus 19 (`0x13`) senden: Schaltet das Custom-Profil blitzschnell und ohne Flackern aktiv.

### C. Der +1 Index-Offset
In der Firmware wird Hardware-Slot 1 (`Esc`) im Speicher intern auf Slot 0 gespiegelt (`XRAM[0x0A60] = XRAM[0x0A61]`). Dadurch sind sämtliche physischen LED-Adressen im Puffer um exakt **+1 verschoben**:
$$\text{LED\_Index} = \text{Matrix\_Slot} + 1$$

* **W:** Slot `14` $\rightarrow$ **Index `15`**
* **A:** Slot `9` $\rightarrow$ **Index `10`**
* **S:** Slot `15` $\rightarrow$ **Index `16`**
* **D:** Slot `21` $\rightarrow$ **Index `22`**
* **Pfeil Hoch (▲):** Slot `88` $\rightarrow$ **Index `89`**
* **Pfeil Links (◄):** Slot `83` $\rightarrow$ **Index `84`**
* **Pfeil Runter (▼):** Slot `89` $\rightarrow$ **Index `90`**
* **Pfeil Rechts (►):** Slot `95` $\rightarrow$ **Index `96`**

### D. Volatiler XRAM vs. Hardware-Cycling per `Fn + ;`
Die Sinowealth MCU besitzt für Per-Key-Matrizen keinen persistenten Flash-ROM, sondern hält benutzerdefinierte Farben im flüchtigen **XRAM**. Wird an der Tastatur per Shortcut (`Fn + ;`) durch Effekte geschaltet, überschreiben die integrierten Animationsroutinen den RAM. Über die macOS-App oder das CLI-Tool kann das Wunschprofil in weniger als 50 ms per Klick wiederhergestellt werden.

### E. Multi-User Lockfile-Sicherheit (`/tmp/ciy_x79_bus_<UID>.lock`)
Um gleichzeitige Zugriffe auf den USB-Endpunkt zwischen Cocoa-Hintergrund-Queue (`_hwQueue`) und CLI-Befehlen (`ciy_ctl`) abzusichern, nutzt der Treiber dateibasierte Mutexe. Anstelle eines unsicheren globalen Lockfiles verwendet das System benutzerspezifische Lockfiles mit der User-ID (`/tmp/ciy_x79_bus_<UID>.lock`) und striktem Modus `0600` (nur der aktuelle Benutzer hat Zugriffsrechte). Ein konfigurierter 2,0-Sekunden-Timeout verhindert blockierende Endlosschleifen.

### F. Robuste JSON Unicode-Escape-Dekodierung (`\uXXXX`)
Standard-JSON-Serialisierer (z. B. Pythons `json.dump` oder Web-Generatoren) kodieren Umlaute und Sonderzeichen standardmäßig als ASCII-Fluchtsequenzen (`\u00df` für `ß`, `\u00e4` für `ä`, `\u00fc` für `ü`). Der integrierte C-Parser in [`ciy_hid.c`](file:///Users/juliengenau/Agentic%20Sandbox/CIY_X79_macOS/src/core/ciy_hid.c) dekodiert alle `\uXXXX`-Sequenzen automatisch nach standardkonformem UTF-8. Profilnamen mit deutschen Sonderzeichen werden damit systemweit einheitlich verarbeitet.

### G. Wide-Gamut (Display P3) Farbklemmung
Auf modernen Macs mit Apple Liquid Retina / Display P3 Bildschirmen können native Farbwähler (`NSColorPanel`) Farbkoordinaten außerhalb des Standard-sRGB-Gamuts liefern (Kanalwerte `< 0.0` oder `> 1.0`). Die Farb-Engine klemmt alle Farbkanäle mathematisch strikt auf `[0, 255]`, bevor der planare USB-Puffer generiert wird. Dies schließt Integer-Überläufe und unvorhersehbare LED-Farben kategorisch aus.

### H. Hardware-Testisolation (`CIY_TESTING=1`)
Automatisierte Unit- und Integrationstests (CI/CD) müssen unabhängig von physisch angeschlossener Hardware reproduzierbar durchlaufen. Wird die Umgebungsvariable `CIY_TESTING=1` gesetzt, schaltet der USB-Treiber automatisch auf ein internes Mock-HID-Device um. Sämtliche Schreibbefehle (Reports `0x0A`, `0x0B`) werden rein im RAM validiert – das reale USB-Interface wird niemals angesprochen und physische Beleuchtungen bleiben unangetastet.

---

## 4. USB-HID Protokoll-Referenz

Die Steuerung erfolgt über **USB Control Transfers** an Endpoint 0 (Interface 1):
* **Set Feature:** `bmRequestType = 0x21` (`kUSBOut | kUSBClass | kUSBInterface`), `bRequest = 0x09` (`kHIDRqSetReport`)
* **Get Feature:** `bmRequestType = 0xA1` (`kUSBIn | kUSBClass | kUSBInterface`), `bRequest = 0x01` (`kHIDRqGetReport`)
* **wValue:** `(kHIDReportTypeFeature << 8) | ReportID`
* **wIndex:** `1` (Interface-Nummer)

| Report ID | Puffergröße | Richtung | Zweck |
|---|---|---|---|
| **`0x0A`** | 42 Bytes | Get / Set Feature | Globale Steuerung (Modus, Palette, Tempo, Helligkeit) |
| **`0x0B`** | 379 Bytes | Set Feature | Planare RGB-Matrix (3 Farbebenen à 126 Bytes) |
| **`0x09`** | 505 Bytes | Get / Set Feature | Tastenbelegung / Key-Remapping (126 Matrix-Slots) |

### Struktur von `Report 0x0A` (42 Bytes):
* `Byte 0`: `0x0A` (Report ID)
* `Byte 1`: Modus (`0` = Aus, `1..18` = Effekte, `15` = Statisch Einfarbig, `19` = Customize / Per-Key)
* `Byte 2`: Farbpalette (`0..10`, siehe Tabelle unten)
* `Byte 3`: Tempo invertiert (`4 - speed` mit Werten `0` = langsam bis `4` = schnell)
* `Byte 4`: Helligkeit (`0` = aus/dunkel bis `5` = maximal)
* `Bytes 5..41`: Reserviert (`0x00`)

### Struktur von `Report 0x0B` (379 Bytes Planar RGB):
* `Byte 0`: `0x0B` (Report ID)
* `Bytes 1..126`: **Ebene 0: ROT** (Helligkeitswert `0x00` bis `0xFF` für Index `0..125`)
* `Bytes 127..252`: **Ebene 1: GRÜN** (`0x00` bis `0xFF` für Index `0..125`)
* `Bytes 253..378`: **Ebene 2: BLAU** (`0x00` bis `0xFF` für Index `0..125`)

---

## 5. Vollständige Tastenmatrix & LED-Indizes (69 Tasten ISO-DE)

Ermittelt durch die Analyse der HID-Matrix-Struktur, Hardware-Latenz-Benchmarks und USB-Paket-Offsets der Tastatur. Das deutsche ISO-DE Layout besitzt **69 physische Tasten** (die hohe ISO-Enter-Taste belegt den Matrix-Slot 81 und erstreckt sich mechanisch über die Reihen 1 und 2):

| Reihe | Tastenbeschriftung (ISO-DE) | Matrix Slot | USB LED-Index (`Slot + 1`) | Breite (U) |
|---|---|---|---|---|
| **Reihe 0** | **Esc** | 1 | 2 | 1.0 |
| | **1, 2, 3, 4, 5, 6, 7, 8, 9, 0** | 7, 13, 19, 25, 31, 37, 43, 49, 55, 61 | 8, 14, 20, 26, 32, 38, 44, 50, 56, 62 | 1.0 |
| | **ß** | 67 | 68 | 1.0 |
| | **´ (Akzent)** | 73 | 74 | 1.0 |
| | **⌫ Backspace** | 79 | 80 | 2.0 |
| | **Entf (Del)** | 86 | 87 | 1.0 |
| **Reihe 1** | **⇥ Tab** | 2 | 3 | 1.5 |
| | **Q** | 8 | 9 | 1.0 |
| | **W** | **14** | **15** | 1.0 |
| | **E, R, T, Z, U, I, O, P** | 20, 26, 32, 38, 44, 50, 56, 62 | 21, 27, 33, 39, 45, 51, 57, 63 | 1.0 |
| | **Ü** | 68 | 69 | 1.0 |
| | **+ \*** | 74 | 75 | 1.0 |
| | **↵ Enter (oben)** | 81 | 82 | 1.5 |
| | **Bild ⇡ (PgUp)** | 87 | 88 | 1.0 |
| **Reihe 2** | **⇪ Caps Lock** | 3 | 4 | 1.75 |
| | **A** | **9** | **10** | 1.0 |
| | **S** | **15** | **16** | 1.0 |
| | **D** | **21** | **22** | 1.0 |
| | **F, G, H, J, K, L** | 27, 33, 39, 45, 51, 57 | 28, 34, 40, 46, 52, 58 | 1.0 |
| | **Ö** | 63 | 64 | 1.0 |
| | **Ä** | 69 | 70 | 1.0 |
| | **# '** | 75 | 76 | 1.0 |
| | **↵ Enter (unten)** | 81 | 82 | 1.25 |
| | **Bild ⇣ (PgDn)** | 94 | 95 | 1.0 |
| **Reihe 3** | **⇧ Shift Links** | 4 | 5 | 1.25 |
| | **< > \|** | 76 | 77 | 1.0 |
| | **Y, X, C, V, B, N, M** | 10, 16, 22, 28, 34, 40, 46 | 11, 17, 23, 29, 35, 41, 47 | 1.0 |
| | **, ;** | 52 | 53 | 1.0 |
| | **. :** | 58 | 59 | 1.0 |
| | **- _** | 64 | 65 | 1.0 |
| | **⇧ Shift Rechts** | 82 | 83 | 1.75 |
| | **Pfeil Hoch (▲)** | **88** | **89** | 1.0 |
| | **Ende (End)** | 91 | 92 | 1.0 |
| **Reihe 4** | **Strg Links** | 5 | 6 | 1.25 |
| | **Win / Cmd** | 11 | 12 | 1.25 |
| | **Alt Links** | 17 | 18 | 1.25 |
| | **Leertaste (Space)** | 35 | 36 | 6.25 |
| | **AltGr** | 53 | 54 | 1.25 |
| | **Fn** | 77 | 78 | 1.0 |
| | **Strg Rechts** | 80 | 81 | 1.0 |
| | **Pfeil Links (◄)** | **83** | **84** | 1.0 |
| | **Pfeil Runter (▼)** | **89** | **90** | 1.0 |
| | **Pfeil Rechts (►)** | **95** | **96** | 1.0 |

---

## 6. Alle Beleuchtungsmodi & Farbpaletten

### Beleuchtungsmodi (0 .. 20):
| Modus | Interner Name / Effekt | Beschreibung |
|---|---|---|
| **0** | `Aus (Off)` | Beleuchtung komplett abgeschaltet |
| **1** | `Welle / Regenbogen (Wave)` | Farbverlauf horizontal über das Keyboard |
| **2** | `Atmung (Breathing)` | Pulsierendes Ein- und Ausblenden |
| **3** | `Reaktiv / Ripple` | Welle breitet sich von gedrückter Taste aus |
| **4** | `Regentropfen (Raindrops)` | Zufälliges Aufleuchten einzelner Tasten |
| **5** | `Neon-Strom` | Fließende Farbstreifen |
| **6** | `Sternenhimmel` | Sanftes Funkeln im Hintergrund |
| **7** | `Schlange / Snake` | Zickzack-Muster über die Tastenreihen |
| **8** | `Farbzyklus (Spectrum)` | Sanfter Farbübergang aller Tasten synchron |
| **9** | `Kreisel / Vortex` | Rotierende Farbwirbel |
| **10** | `Komet / Meteor` | Horizontale Lichtspuren |
| **11** | `Aurora / Polarlicht` | Weiche diffuse Wellen |
| **12** | `Windmühle` | Rotierende Sternflügel |
| **13** | `Blüte / Ripple Bloom` | Kreisförmiges Aufblühen |
| **14** | `Laser-Spur` | Horizontale Strahlen bei Tastenanschlag |
| **15** | **`Statisch / Solide (Solid)`** | **Statische Vollfarbe (aus Farbpalette)** |
| **16** | `Wellenbrecher` | Sich begegnende Lichtwellen |
| **17** | `Glühen` | Permanentes Leuchten mit leichten Akzenten |
| **18** | `Funkenflug` | Schnelle Lichtpartikel |
| **19** | **`Customize (Matrix)`** | **Echtes Per-Key Custom-RGB aus XRAM (Report 0x0B)** |
| **20** | `Ripple / Regenbogen-Welle` | Schnelle Regenbogen-Welle |

### Farbpaletten (0 .. 10):
| Index | Farbe | Index | Farbe |
|---|---|---|---|
| **0** | Rot | **6** | Lila / Violett |
| **1** | Orange | **7** | Weiß |
| **2** | Gelb | **8** | Bunt / Regenbogen |
| **3** | Grün | **9** | Dynamischer Farbwechsel 1 |
| **4** | Cyan / Türkis | **10** | Dynamischer Farbwechsel 2 |
| **5** | Blau | | |

---

## 7. Die macOS-App (`CIYX79Control.app`)

Die native macOS-Anwendung wurde in Objective-C/Cocoa geschrieben und benötigt weder Drittanbieter-Runtimes noch externe Frameworks. Sie arbeitet nach dem **Single-Source-of-Truth-Prinzip**: Sämtliche an die Tastatur übertragenen Beleuchtungen sind vollständige, gespeicherte Profile.

### Kernmerkmale & Workflow:
1. **100 % Profil-Zentriert:**
   * Es gibt keine fehleranfälligen Ad-hoc-Befehle oder isolierte Modus-Umschaltungen mehr.
   * Sämtliche Daten an die Tastatur laufen ausschließlich über den atomaren 2-Stufen-Sende-Kanal (Report `0x0B` Planar RGB $\rightarrow$ 50 ms Pause $\rightarrow$ Report `0x0A` Modus 19).
2. **Entkoppeltes Bearbeiten (0 USB-Traffic im Editor):**
   * Das Anklicken von Tasten und Zuweisen von Farben verändert ausschließlich die visuelle Darstellung im Editor.
   * Keine USB-Überlastung oder Verzögerung beim schnellen Gestalten mehrerer Tasten.
   * Sobald Tasten geändert werden, meldet die Statuszeile: `● Taste bearbeitet (Profil „...“ ungespeichert)`.
3. **Integrierte Standard- & Eigene Profile:**
   * **`🎮 WASD Gaming (Rot / Basis Blau)`:** WASD hervorgehoben in Rot, restliche Tasten in Blau.
   * **`🎮 WASD Stealth (Grün / Basis Aus)`:** Nur WASD leuchtet in Grün, alle anderen Tasten aus.
   * **`🎯 Pfeiltasten (Gelb / Basis Blau)`:** Pfeiltasten in Gelb, restliche Tasten in Blau.
   * **`🕹 WASD + Pfeile (Rot & Gelb / Basis Blau)`:** WASD Rot, Pfeiltasten Gelb, Basis Blau.
   * **`🔴 Vollrot (Alles Rot)`:** Vollflächig statisch Rot.
   * **`🌌 Cyberpunk (Cyan / Lila)`:** WASD Cyan, Pfeile Gelb, Basis Violett.
   * **`💼 Büro / Clean (Dezent Weiß)`:** Sanftes weißes Arbeitslicht.
   * **`⚫ Beleuchtung Aus`:** Schaltet alle LEDs ab.
4. **Profil-Aktionen & Menü-Verwaltung:**
   * **`Senden (↵)` (Enter-Taste):** Überträgt das aktuell ausgewählte Profil sofort an die Tastatur (ideal nach manuellem Umschalten an der Tastatur per `Fn + ;` oder via `⌘R`).
   * **`Speichern & Senden` (`⌘S`):** Sichert die aktuellen Tastenfarben aus dem Editor im Profil und sendet sie simultan an die Tastatur.
   * **`+ Neu...` (`⌘N`):** Speichert die aktuellen Farben als neues individuelles Profil ab.
   * **`Umbenennen` & `Löschen`:** Komfortable Verwaltung der Profilliste (mindestens ein Profil bleibt geschützt erhalten).
   * **`Profile exportieren...` (`⌘E`) & `Profile importieren...` (`⌘O`):** Profile als `.json`-Dateien auf der Festplatte sichern, mit anderen Nutzern teilen oder einspielen (über das Menü *Ablage* oder das Dropdown *Mehr ▾*).
   * **`Alle...`:** Füllt im Editor alle 68 Tasten simultan mit einer gewählten Basisfarbe.
5. **Erweiterte Farbwahl & Hardware-Steuerung:**
   * **macOS-Farbrad (`NSColorWell`):** Neben der 8-Farben-Schnellpalette öffnet der Farbwähler das native Farbrad für stufenlose RGB- und Hex-Farben.
   * **Helligkeitsregler (`☀️` Slider 1..5):** Stufenlose Helligkeitseinstellung für das Custom-Profil direkt in der Editor-Leiste.
   * **Direkte Tastatur-Eingabe:** Das Drücken einer physischen Taste (z. B. `W`, `Esc`, `Enter` oder Pfeiltasten) wählt diese Taste im Vektor-Canvas sofort an.
   * **macOS-Hauptmenüleiste:** Vollwertiges Systemmenü mit Standard-Kurzbefehlen (`⌘Q`, `⌘W`, `⌘H`, `⌘N`, `⌘S`, `⌘E`, `⌘O`, `⌘R`, `⌘U`).
6. **Automatischer Hotplug Re-Sync:**
   * Überwacht IOKit-Events (`IOServiceAddMatchingNotification`). Wird das USB-Kabel ab- und wieder angesteckt, verbindet sich die App neu und sendet das aktive Profil automatisch an die Tastatur.
   * Profile werden persistent in `NSUserDefaults` (`CIYX79CustomProfiles_v1`) gespeichert.
7. **Automatischer Profilwechsel bei App-Fokus & Menüleisten-Symbol (Tray / Menu Bar Extra):**
   * **Menüleisten-Symbol (Status Item):** Die App integriert sich nativ mit einem adaptiven Tastatursymbol (`NSStatusItem`) in die macOS-Menüleiste (rechts oben). Beim Schließen des Hauptfensters (`⌘W` oder roter Schließen-Button) bleibt die App lautlos im Hintergrund aktiv (0,0 % CPU-Last).
   * **Schnellsteuerung aus der Menüleiste:**
     * Verbindungsstatus in Echtzeit (*CIY X79: Verbunden / Getrennt*)
     * *„Hauptfenster öffnen...“* zum sofortigen Wiederherstellen der Benutzeroberfläche (stellt auch minimierte Fenster aus dem Dock per `deminiaturize` wieder her)
     * Schnelles Umschalten zwischen allen Profilen per Klick (mit Haken am aktiven Profil und Tag-Anzeige wie `[Steam]`)
     * Schnelles Ein-/Ausschalten des Auto-Triggers (`⚡ Auto-Trigger`)
     * Vollständiges Beenden der App (`⌘Q`)
   * **App-Verknüpfung mit automatischer Deduplizierung:** Profile können über den Button *„App wählen...“* direkt mit Spielen oder Programmen (z. B. Steam, CS:GO, VS Code) verknüpft werden. Wird eine App einem neuen Profil zugewiesen, wird die alte Verknüpfung in anderen Profilen automatisch konfliktfrei entfernt.
   * **100 % sicher & unprivilegiert:** Nutzt die offizielle macOS-API `NSWorkspaceDidActivateApplicationNotification` – **ohne** Berechtigungen für Bedienungshilfen (*Accessibility*), **ohne** Eingabeüberwachung (*Input Monitoring*) und **ohne** Keylogger.
   * **Intelligentes Debouncing (250 ms):** Verhindert störendes LED-Flackern und unnötigen USB-Traffic bei schnellem `⌘ + Tab`-Wechseln.
   * **Resilienter Standard-Fallback:** Wird zu einer nicht verknüpften App gewechselt, stellt die App automatisch das definierte Standard-Profil (z. B. *Büro / Clean Weiß*) wieder her. Ungültige oder verwaiste Fallback-IDs werden beim Start automatisch erkannt und geheilt.
   * **Sauberer Teardown (`applicationWillTerminate:`):** Beim Beenden werden alle IOKit-Monitoring-Ports und Benachrichtigungen vollständig deregistriert und freigegeben.

---

## 8. Das Terminal-Tool (`ciy_ctl`)

Für automatisierte Shell-Skripte, Menüleisten-Widgets (BitBar, SketchyBar), Raycast, Alfred oder puristische Bedienung ohne GUI:

```bash
# Aktuellen Status anzeigen (Standard oder maschinenlesbar als JSON)
./ciy_ctl status
./ciy_ctl status --json   # {"connected":true,"mode":19,"mode_name":"...","brightness":5,"speed":4}

# Vollfarben & Hex-Töne sofort anwenden:
# Syntax: ./ciy_ctl fill <#RRGGBB> [-b 0..5]
./ciy_ctl fill "#FF5500"             # Alle Tasten in Orange (Standardhelligkeit 5/5)
./ciy_ctl fill "#00FF88" -b 3        # Helligkeit via Flag (-b oder --brightness)
./ciy_ctl fill 0088FF 2              # Positionale Helligkeit (abwärtskompatibel)
./ciy_ctl fill "#0088FF" --json      # Mit strukturierter JSON-Rückmeldung

# Exportierte GUI-Profile (JSON) anwenden:
# Syntax: ./ciy_ctl load <profile.json> [Profilname|Index] [-b 0..5]
./ciy_ctl load meinkeyboard.json                        # Erstes Profil der Datei laden
./ciy_ctl load meinkeyboard.json "Cyberpunk" -b 4       # Gezielt nach Name mit Helligkeit 4
./ciy_ctl load meinkeyboard.json 2 -b 3                 # 2. Profil der Datei laden (1-basierter Index)
./ciy_ctl load meinkeyboard.json "Büro" --json          # JSON-Status für Skripte

# Profil hardwarefrei validieren (ohne USB-Zugriff):
# Syntax: ./ciy_ctl validate-profile <profile.json> [--json]
./ciy_ctl validate-profile meinkeyboard.json            # Syntax, Slots (0..124) und Farben (#RGB) prüfen
./ciy_ctl validate-profile meinkeyboard.json --json     # Maschinenlesbares Ergebnis für CI/CD

# Profilübertragung simulieren (--dry-run ohne USB-I/O):
# Syntax: ./ciy_ctl --dry-run load <profile.json> [Profilname|Index] [-b 0..5] [--json]
./ciy_ctl --dry-run load meinkeyboard.json              # Zeigt geplanten Ablauf (0x0B -> 50ms -> 0x0A)
./ciy_ctl --dry-run load meinkeyboard.json --json       # Detaillierter Übertragungsplan als JSON

# Standard-Gaming-Profile:
./ciy_ctl wasd          # WASD Rot, Basis Blau
./ciy_ctl wasd-arrows   # WASD Rot, Pfeile Gelb, Basis Blau
./ciy_ctl all-red       # Vollflächig statisch Rot

# Beliebige Firmware-Parameter manuell setzen:
# Syntax: ./ciy_ctl set <Modus> [Farbe] [Tempo] [Helligkeit]
./ciy_ctl set 15 0 1 5  # Modus 15 (Statisch), Rot, Tempo 1, Helligkeit 5
./ciy_ctl set 0 0 0 0   # Beleuchtung komplett aus

# Alle Modi und Farben auflisten (Text oder JSON)
./ciy_ctl list-modes
./ciy_ctl list-modes --json

# Zsh-Autovervollständigung aktivieren:
# Einbindung in ~/.zshrc:
# fpath=(/Pfad/zu/CIY_X79_macOS/scripts/completions $fpath)
# autoload -Uz compinit && compinit

# Hilfe & Optionen
./ciy_ctl --help
```

> [!TIP]
> **Eindeutige Argument-Auflösung bei `load`:**
> Folgt nach dem Dateinamen ein einzelnes Argument (z. B. `./ciy_ctl load meinkeyboard.json 3`), wird dies immer strikt als **Profilselektor** (hier: das 3. Profil der Datei) ausgewertet. Um die Helligkeit zu steuern, empfiehlt sich das explizite Flag `-b` bzw. `--brightness` (z. B. `-b 3`).

---

## 9. Repository-Struktur & Architektur

### Verzeichnisstruktur
```
CIY_X79_macOS/
├── src/
│   ├── core/                    # Reine C-USB-Treiber-Schicht (IOKit & HID)
│   │   ├── ciy_hid.h            # USB HID Header, Planar-RGB- und JSON-Parser
│   │   └── ciy_hid.c            # USB Control Requests, Low-Level Protocol, Mock-Device
│   ├── layouts/                 # Entkoppelte Layout-Definitionen & Tasten-Modell
│   │   ├── ciy_layout.h         # Model für CIYLogicalKey, CIYKeySurface, CIYKeyboardLayout
│   │   └── ciy_x79_iso_de.c     # 69 logische Tasten & 70 Zeichenflächen der CIY X79 ISO-DE
│   ├── app/                     # Native Cocoa-Applikation (Objective-C)
│   │   ├── main.m
│   │   ├── AppDelegate.h / .m   # UI Lifecycle, Menüs, Export/Import, Farbauswahl
│   │   ├── CIYKeyboardView.h / .m # Canvas-View (69 Tasten ISO-DE Vektoren)
│   │   └── KeyboardBackend.h / .m # Thread-sicherer Treiber, P3-Clamp & Hotplug
│   └── cli/                     # Kommandozeilen-Werkzeug
│       └── ciy_ctl.c            # CLI ciy_ctl (Status, JSON, Fill, Load, -b Flag)
├── resources/                   # App-Grafiken & Assets
│   └── AppIcon.icns
├── scripts/                     # Build-, Test- und Packaging-Skripte
│   ├── build_app.sh             # Universal Binary Compiler & Codesigning (-O2)
│   ├── create_dmg.sh            # Komprimiertes Disk Image (UDZO)
│   ├── run_tests.sh             # Führt alle Test-Suiten isoliert aus
│   ├── hardware_diagnostics.py  # Hardware-Diagnose, Latenz-Benchmark & Sweep
│   └── completions/             # Shell Autovervollständigung
│       └── _ciy_ctl             # Zsh Completion Definition
├── tests/                       # Automatisierte Test-Suiten
│   ├── fixtures/                # Valide & ungültige Profil-JSON-Testdateien
│   ├── test_ciy_hid.c           # C-Protokoll-, Hex-, Unicode- & Lockfile-Tests
│   ├── test_profile_logic.m     # Canvas-, P3-Clamp- & Profil-Speicherlogik Tests
│   └── test_ciy_ctl.sh          # End-to-End CLI-Tests (Syntax, -b, Error-Handling)
├── dist/                        # Kompilierte Universal Binaries & DMG
│   ├── CIYX79Control.app
│   ├── ciy_ctl
│   └── CIYX79Control-1.0.2.dmg
├── README.md                    # Umfassende Dokumentation & Anleitung
├── LICENSE                      # MIT-Lizenz
└── .gitignore                   # Git-Ausschlussregeln
```

### Schichtenmodell

Das Projekt ist modular nach dem Single-Responsibility-Prinzip in klare Schichten gegliedert:

```
┌────────────────────────────────────────────────────────┐
│                      Benutzeroberfläche                │
│    CIYX79Control.app (AppDelegate.m)  /  ciy_ctl.c     │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                  Interaktive Canvas-View               │
│                    CIYKeyboardView.m                   │
│         (69-Tasten ISO-DE Vektor-Rendering & Klicks)   │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                    Cocoa Hardware-Treiber              │
│                     KeyboardBackend.m                  │
│       (Thread-Sicherheit, _hwQueue, P3-Farbklemmung)   │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                Reine C USB-HID Engine                  │
│                   ciy_hid.c / ciy_hid.h                │
│  (Report 0x0A, 0x0B Planar RGB, IOKit & CIY_TESTING)   │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                 CIY X79 Hardware (USB)                 │
│         VID: 0x258A | PID: 0x0049 | Interface 1        │
└────────────────────────────────────────────────────────┘
```

1. **`src/core/` (`ciy_hid.c` / `ciy_hid.h`):**
   * Reine C-Schicht ohne UI-Abhängigkeiten.
   * Sendet USB Control Requests an Interface 1 via IOKit.
   * Benutzerspezifisches Lockfile (`/tmp/ciy_x79_bus_<UID>.lock`, `0600`) mit Timeout.
   * Integrierter virtueller Hardware-Mock bei gesetzter `CIY_TESTING`-Umgebungsvariable.
   * Planare Pufferbauer und JSON-Parser mit RFC 8259 Unicode-Escape-Dekodierung (`\uXXXX`).
2. **`src/app/` (`KeyboardBackend.m` / `KeyboardBackend.h`):**
   * Cocoa-Singleton (`[KeyboardBackend shared]`).
   * Thread-Sicherheit durch serielle Hintergrund-Queue (`_hwQueue`) und asynchrone Initialisierung.
   * Mathematische Farbklemmung für Wide-Gamut/Display P3 Farbräume.
   * IOKit-Hotplug-Überwachung.
3. **`src/app/` (`CIYKeyboardView.m` / `CIYKeyboardView.h`):**
   * `NSView`-Subklasse mit präzisem 65% ISO-DE Vektordesign (69 Tasten).
   * Crash-sichere sRGB-Farbextraktion (`[color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]]`).
   * Maus-Hover und Key-Selection Tracking.
4. **`src/app/` (`AppDelegate.m` / `AppDelegate.h`):**
   * App-Lebenszyklus, Layout-Aufbau, Farbpalette und Profil-Verwaltung mit Namen-Sanitization.
5. **`src/cli/` (`ciy_ctl.c`):**
   * Leichtgewichtiges C-Kommandozeilen-Tool für Skripte und Terminalnutzung mit sauberer Optionsextraktion (`-b / --brightness`).

---

## 10. Automatisierte Tests & Hardware-Diagnose

### A. Automatisierte Test-Suiten (`scripts/run_tests.sh`)

Das Projekt beinhaltet eine vollständig hardware-isolierte automatisierte Test-Batterie. Über den Guard `CIY_TESTING=1` wird sichergestellt, dass **kein Test jemals die physisch angeschlossene Tastatur beschreibt**:

```bash
./scripts/run_tests.sh
```

**Enthaltene Test-Suiten:**
1. **`tests/test_ciy_hid.c` (HID- & Protokoll-Schicht):**
   * Planarer RGB-Pufferaufbau für WASD, Pfeiltasten und Vollfarben.
   * Grenzwerte, Out-of-Bounds-Slots (< 0 oder $\ge$ 126) und Integer-Overflow-Schutz.
   * Validierung der JSON-Parser-Logik inkl. UTF-8 BOM, Sonderzeichen und `\uXXXX`-Unicode-Escapes.
   * Reentranz des Bus-Locks und Berechtigungsprüfung (`0600`) der benutzerspezifischen Lockdatei.
2. **`tests/test_profile_logic.m` (Cocoa-App & Backend):**
   * Hex-zu-NSColor- und NSColor-zu-Hex-Konvertierung inklusive strikter Fehlererkennung (`ciy_parse_hex_color`).
   * P3-Farbklemmung (`ClampColorChannel`) gegen Integer-Wrapping.
   * Profil-Lifecycle (Erstellen, Aktualisieren, Umbenennen, Sanitisieren überlanger Namen, Löschen).
   * Vollständige Abdeckung aller Farbslots im Canvas-View.
   * App-Verknüpfungs-Deduplizierung und Resilienz gegen verwaiste Fallback-Profile.
3. **`tests/test_ciy_ctl.sh` (CLI Integrationstests):**
   * Hilfe- und Optionsprüfung (`--help`, `-h`).
   * Text- und JSON-Ausgabe bei `list-modes`.
   * Fehlerbehandlung bei ungültigen Optionen und Bereichsverletzungen von `-b 0..5`.
   * Eindeutige Auflösung von `load <datei> [Profil|Index] [-b 0..5]` (Finding 5).

### B. Hardware-Diagnose & Latenz-Benchmark (`scripts/hardware_diagnostics.py`)

Wenn eine echte CIY X79 angeschlossen ist, kann das interaktive Diagnoseskript genutzt werden, um das Ansprechverhalten jeder Taste auf der physikalischen Matrix exakt zu prüfen:

```bash
# 1. Gesamte Diagnosebatterie ausführen (Isolation -> Benchmark -> Zonen -> Sweep -> Restore)
python3 scripts/hardware_diagnostics.py all

# 2. Latenz-Benchmark über alle 69 Tasten (USB-Quittierungszeiten in ms messen)
python3 scripts/hardware_diagnostics.py benchmark

# 3. Sichtbares LED-Lauflicht über alle 69 Tasten (Reihe 0 bis 4)
python3 scripts/hardware_diagnostics.py sweep

# 4. Funktionsblock-Dynamik (WASD, Pfeile, Navigation, Modifikatoren)
python3 scripts/hardware_diagnostics.py clusters

# 5. Gezielte Einzeltaste prüfen (z. B. Esc, Enter, Space, PfeilAuf, <>)
python3 scripts/hardware_diagnostics.py key Space

# 6. Standard WASD-Gaming-Profil wiederherstellen
python3 scripts/hardware_diagnostics.py restore
```

---

## 11. Kompilierung & Packaging (Universal Binary & DMG)

Alle Komponenten werden mit Clang (`-O2`) und nativen macOS-Frameworks kompiliert. Es werden keine externen Paketmanager (Homebrew, CocoaPods etc.) benötigt:

### 1. Universal Binary App & CLI kompilieren:
```bash
./scripts/build_app.sh
```
Das Skript:
* Kompiliert sowohl die GUI-App als auch das CLI-Tool `ciy_ctl` mit Compiler-Optimierung (`-O2`) als **Universal Binary** (`-arch arm64 -arch x86_64`), lauffähig auf Apple Silicon (M1–M4) und Intel-Macs.
* Bettet das hochauflösende Tastatur-App-Icon (`AppIcon.icns`) sowie alle mehrsprachigen Dokumentationen (`README*.md`) direkt in das App-Bundle ein.
* Schreibt die `Info.plist` und signiert beide Artefakte (App und CLI-Tool `ciy_ctl`) per Ad-hoc-Codesigning (`codesign --force --sign -`).

### 2. Installationsdatei (DMG) erstellen:
```bash
./scripts/create_dmg.sh
```
Erzeugt die distributionsfertige Datei `CIYX79Control-1.0.2.dmg` mit Drag-and-Drop-Verknüpfung zum `/Applications`-Ordner, eingebettetem CLI-Tool `ciy_ctl`, Lizenz und Dokumentation.

### 3. CLI-Tool manuell kompilieren:
```bash
clang -O2 -arch arm64 -arch x86_64 -Wall -Wextra -framework IOKit -framework CoreFoundation src/core/ciy_hid.c src/cli/ciy_ctl.c -o dist/ciy_ctl
```

---

## 12. Sicherheit, Datenschutz & Ressourcen

* **Kein Keylogger / Keine Eingabeüberwachung:**
  Die Anwendung benötigt und fordert **weder** Berechtigungen für Bedienungshilfen (*Accessibility*) **noch** Eingabeüberwachung (*Input Monitoring*). Es wird zu keinem Zeitpunkt registriert oder mitgelesen, welche Tasten getippt werden.
* **Keine Kernel-Treiber / Keine Systemerweiterungen:**
  Die Kommunikation erfolgt rein im unprivilegierten Benutzerbereich (*User Space*) über die offizielle `IOKit`-API von Apple.
* **100 % Offline:**
  Die Binärdateien enthalten keine Netzwerk-Sockets, keine Telemetrie und keine externen Server-Verbindungen.
* **0,0 % CPU-Ruhelast:**
  Nach dem Senden eines Profils verbraucht das Programm im Ruhezustand 0,0 % Prozessorleistung. Es gibt keine ressourcenfressenden Abfrage-Schleifen.

---

## 13. Lizenz, Markenhinweis & Haftungsausschluss

### Lizenz
Dieses Projekt ist unter der **[MIT-Lizenz](LICENSE)** lizenziert. Sie erlaubt die freie Nutzung, Modifikation und Weitergabe unter Wahrung des Urheberrechtshinweises.

### Markenhinweis & Disclaimer (Trademark Notice)
„CIY“, „Sinowealth“, „Apple“, „macOS“ sowie alle weiteren in dieser Dokumentation oder im Quelltext erwähnten Markennamen, Warenzeichen und Produktbezeichnungen sind Eigentum ihrer jeweiligen Rechteinhaber. Ihre Nennung dient ausschließlich der sachlichen Information über die Bestimmung, Funktionsweise und Kompatibilität dieser Software als interoperables Zubehör gemäß **§ 23 Abs. 1 Nr. 3 MarkenG**. Dieses Projekt ist eine unabhängige Open-Source-Gemeinschaftsentwicklung und steht in **keiner geschäftlichen, personellen oder offiziellen Verbindung** zu den Herstellern der Tastatur oder den genannten Betriebssystemen.

### Haftungsausschluss (Deutsches Recht)
Die Nutzung dieser Software erfolgt auf eigene Verantwortung. Das Programm sendet USB-Steuerbefehle und konfiguriert hardwarespezifische Register und Speicherbereiche (XRAM) des Mikrocontrollers. Soweit gesetzlich zulässig, wird die Haftung des Autors gemäß **§ 521 BGB (Schenkungsrecht)** auf Vorsatz und grobe Fahrlässigkeit beschränkt. Für etwaige Schäden an Hardware (z. B. Tastatur-Controller, LEDs, USB-Ports) oder Datenverlust wird keine Haftung oder Gewährleistung übernommen.
