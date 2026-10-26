# Beitrags- und Kompatibilitätsrichtlinien (Contributing Guide)

Vielen Dank für dein Interesse, das CIY X79 RGB macOS-Projekt zu erweitern!

Dieses Projekt verfolgt einen qualitäts- und sicherheitsorientierten Ansatz (**„Compatibility Foundation“**): Bevor neue Tastaturlayouts oder Geräte offiziell als kompatibel deklariert werden, müssen sie transparent, reproduzierbar und hardware-verifiziert sein.

---

## 1. Kompatibilitäts-Taxonomie

Um Missverständnisse und ungetestete Kompatibilitätsversprechen zu vermeiden, nutzt das Projekt vier verbindliche Stufen:

* **Maintainer-verified:** Das konkrete Modell/Revision und alle Funktionen wurden vom Maintainer physisch auf echter Hardware unter macOS verifiziert. (Aktuell: CIY X79 ISO-DE).
* **Community-verified:** Ein nachvollziehbarer Hardware-Testbericht eines Gerätebesitzers liegt vor (inkl. Hardwaredaten, macOS-Version, CPU-Architektur und Protokollnachweis); die Maintainer-Prüfung steht noch aus.
* **Experimental / unverified:** Unbestätigte Unterstützung (z. B. theoretische Zuordnungen anhand von Firmware-/MCU-Ähnlichkeiten); keine Garantie für Funktion oder Zuverlässigkeit.
* **Unsupported:** Ausdrücklich reserviert für nachweislich inkompatible Geräte oder Revisionen (abweichende Firmware, inkompatibles Protokoll, fehlgeschlagene Hardwaretests) – nicht für bloß ungetestete Modelle.

---

## 2. Datenschutz & Sicherheitsregeln

1. **Kein Keylogging / Input Monitoring:** Dieses Projekt liest niemals Tastatureingaben ab. Es werden weder Accessibility-Rechte noch Input-Monitoring-Berechtigungen benötigt oder angefordert. Beiträge, die derartige Hooks einführen, werden abgelehnt.
2. **Log-Bereinigung:** Bereinige alle Diagnoseausgaben und Logs vor dem Hochladen um persönliche Daten, Benutzernamen oder Seriennummern.
3. **Fail-Closed:** Unbekannte VID/PID-Kombinationen dürfen niemals Schreibzugriffe auslösen.
4. **Offline-by-Default:** Das Projekt nutzt keinerlei Netzwerkverbindungen, Telemetrie oder Cloud-Dienste.

---

## 3. Workflow für neue Layouts & Hardware

### Schritt 1: Hardware-Identifikation (Passives Auslesen)
Erfasse die genauen USB-Deskriptoren deines Geräts im Terminal:

```bash
# USB-Deskriptoren passiv ermitteln:
system_profiler SPUSBDataType | grep -A 10 -B 2 -i "0x258a"
```

Notiere:
* Genaue Produktbezeichnung und Revision (Aufdruck auf Gehäuse/Platine).
* Physisches Layout (z. B. ANSI-US mit 68 Tasten, ISO-UK etc.).
* Vendor ID (VID), Product ID (PID) und USB-Interface-Nummer.

### Schritt 2: Layoutmodell & Tastenmatrix ermitteln
Die CIY X79 nutzt im ISO-DE-Layout die feste Zuordnungsregel `led_index = matrix_slot + 1`. **Diese Regel darf nicht blind auf andere MCUs oder Tastaturen übertragen werden.**

Nutze die hardwarefreien Werkzeuge zur Vorbereitung:
```bash
# 1. Profilentwurf ohne USB-Zugriff validieren:
./ciy_ctl validate-profile mein_neues_layout.json

# 2. Übertragungsplan hardwarefrei simulieren (Dry-Run):
./ciy_ctl --dry-run load mein_neues_layout.json
```

### Schritt 3: Schreibende Hardware-Tests
> [!CAUTION]
> Führe schreibende Tests (z. B. Einzelfarben oder Tasten-Sweeps) nur durch, wenn VID/PID und das Report-Format (`Report 0x0B` und `Report 0x0A`) nachweislich mit deiner Tastatur übereinstimmen.

Teste:
1. Reagiert die Beleuchtung auf `Report 0x0B` gefolgt von `Report 0x0A` (Modus 19)?
2. Werden Tasten an den erwarteten physischen Positionen beleuchtet?
3. Tritt nach Abstecken und Wiederanstecken (Hotplug) der erwartete Re-Sync ein?

### Schritt 4: Bericht einreichen
Erstelle ein GitHub-Issue unter Verwendung der Vorlage [Device / Layout Request](.github/ISSUE_TEMPLATE/device_layout_request.md).

---

## 4. Richtlinien für Code-Beiträge (Pull Requests)

* **Zero-Bloat macOS:** Keine Abhängigkeiten von Homebrew, Node, Docker oder vollem Xcode.app. Der Build muss direkt über `clang` funktionieren (`-O2`, Universal Binary `arm64`/`x86_64`, `-mmacosx-version-min=12.0`).
* **Trennung von Modell und View:** Layout-Metadaten gehören in `src/layouts/ciy_layout.h` und dedizierte Quellcodedateien (`src/layouts/ciy_<modell>_<layout>.c`), getrennt in logische Tasten (`CIYLogicalKey`) und grafische Zeichenflächen (`CIYKeySurface`).
* **Testabdeckung:** Alle Tests in `./scripts/run_tests.sh` müssen mit Adress- und Undefined-Behavior-Sanitizern (`-fsanitize=address,undefined`) fehlerfrei durchlaufen.
* **Universal Binary:** `./scripts/build_app.sh` muss sauber durchlaufen und beide Architekturen unterstützen.
