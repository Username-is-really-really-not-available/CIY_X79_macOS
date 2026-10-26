---
name: Neues Gerät / Layout einreichen
about: Reiche Testergebnisse oder ein neues Tastaturlayout für die CIY-X79-Familie ein
title: "[Hardware/Layout]: <Hersteller / Modell> (<Layout>)"
labels: hardware-test, layout
assignees: ''
---

## 1. Geräteinformationen
* **Hersteller & Modell:** (z. B. CIY X79, Sinowealth 65%)
* **Physisches Layout:** (z. B. ANSI-US mit 68 Tasten, ISO-UK mit 69 Tasten)
* **Revisions- / PCB-Aufdrucke (falls bekannt):** 
* **Tastenanzahl:** (z. B. 68 physische Tasten)

## 2. USB-Identifikatoren
* **Vendor ID (VID):** `0x258A`
* **Product ID (PID):** `0x0049`
* **USB-Interface:** `1`

*(Ausgabe von `system_profiler SPUSBDataType | grep -A 10 -B 2 -i "0x258a"` hier einfügen)*

## 3. Testumgebung
* **macOS-Version:** (z. B. macOS 14.5 Sonoma, macOS 15.1 Sequoia)
* **CPU-Architektur:** [ ] Apple Silicon (M1/M2/M3/M4)  [ ] Intel (x86_64)

## 4. Hardwarefreier Trockenlauf (Dry-Run)
*Ergebnis von `./ciy_ctl --dry-run load <dein_profil.json>`:*
```text

```

## 5. Durchgeführte Hardware-Tests (falls zutreffend)
- [ ] **Passives Erkennen:** Gerät wird von `ciy_ctl status` fehlerfrei erkannt
- [ ] **Moduswechsel:** Firmware-Modi (z. B. `ciy_ctl set 1 0 2 4`) schalten zuverlässig um
- [ ] **Per-Key Matrix:** Tasten leuchten an der physisch korrekten Position
- [ ] **Hotplug:** Nach Abziehen und Wiederanstecken erfolgt der Re-Sync automatisch
- [ ] **Keine Hardware-Transfers bei Dry-Run:** `--dry-run load` sendet keine Signale an die Tastatur

## 6. Matrix- & LED-Zuordnungsbelege
Falls Abweichungen vom bestehenden ISO-DE Mapping (`matrix_slot + 1`) festgestellt wurden, bitte hier dokumentieren:
```text
Taste Esc:   Slot X -> LED Y
Taste Enter: Slot X -> LED Y
...
```

## 7. Sicherheitsbestätigung
- [x] Ich bestätige, dass alle übermittelten Protokollauszüge frei von personenbezogenen Daten und Passwörtern sind.
- [x] Mir ist bekannt, dass dieses Projekt keinerlei Keylogging oder Eingabeüberwachung durchführt.
