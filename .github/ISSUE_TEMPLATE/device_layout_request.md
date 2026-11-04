---
name: "Neues Gerät / Layout einreichen (DE/EN)"
about: "Testergebnisse oder ein neues Tastaturlayout einreichen / Submit test results or a new layout"
title: "[Hardware/Layout]: <Hersteller / Manufacturer> <Modell / Model> (<Layout>)"
labels: hardware-test, layout
assignees: ''
---

## 1. Geräteinformationen / Device Information

* **Hersteller & Modell / Manufacturer & model:** (z. B. CIY X79 / e.g. CIY X79)
* **Physisches Layout / Physical layout:** (z. B. ANSI-US mit 68 Tasten / e.g. ANSI-US with 68 keys)
* **Revisions- / PCB-Aufdrucke / Revision / PCB markings (if known):**
* **Tastenanzahl / Key count:** (z. B. 68 / e.g. 68)

## 2. USB-Identifikatoren / USB Identifiers

* **Vendor ID (VID):** `0x____` (bitte eintragen / please fill in)
* **Product ID (PID):** `0x____` (bitte eintragen / please fill in)
* **USB-Interface / USB interface:** `__` (bitte eintragen / please fill in)

```text
(Ausgabe von / Output of: system_profiler SPUSBDataType | grep -A 10 -B 2 -i "<VID>")
```

## 3. Testumgebung / Test Environment

* **macOS-Version / macOS version:** (z. B. macOS 14.5 Sonoma, macOS 15.1 Sequoia)
* **CPU-Architektur / CPU architecture:** [ ] Apple Silicon (M1/M2/M3/M4)  [ ] Intel (x86_64)

## 4. Hardwarefreier Trockenlauf / Hardware-free Dry Run

*Ergebnis von / Result of `./ciy_ctl --dry-run load <dein_profil.json / your_profile.json>`:*
```text

```

## 5. Durchgeführte Hardware-Tests / Hardware Tests Performed

- [ ] **Passives Erkennen / Passive detection:** Gerät wird von `ciy_ctl status` fehlerfrei erkannt / Device is detected by `ciy_ctl status`
- [ ] **Moduswechsel / Mode switching:** Firmware-Modi (z. B. `ciy_ctl set 1 0 2 4`) schalten zuverlässig um / Firmware modes switch reliably
- [ ] **Per-Key Matrix / Per-key matrix:** Tasten leuchten an der physisch korrekten Position / Keys light up at the correct physical positions
- [ ] **Hotplug:** Nach Abziehen und Wiederanstecken erfolgt der Re-Sync automatisch / Resynchronization happens automatically after unplugging and reconnecting
- [ ] **Keine Hardware-Transfers bei Dry-Run / No hardware transfers during dry run:** `--dry-run load` sendet keine Signale an die Tastatur / sends no signals to the keyboard

## 6. Matrix- & LED-Zuordnungsbelege / Matrix & LED Mapping Evidence

Falls Abweichungen vom bestehenden ISO-DE Mapping (`matrix_slot + 1`) festgestellt wurden, bitte hier dokumentieren / If deviations from the existing ISO-DE mapping were found, document them here:
```text
Taste / Key Esc:   Slot X -> LED Y
Taste / Key Enter: Slot X -> LED Y
...
```

## 7. Sicherheitsbestätigung / Security Confirmation

- [x] Ich bestätige, dass alle übermittelten Protokollauszüge frei von personenbezogenen Daten und Passwörtern sind. / I confirm that all submitted protocol excerpts are free of personal data and passwords.
- [x] Mir ist bekannt, dass dieses Projekt keinerlei Keylogging oder Eingabeüberwachung durchführt. / I understand that this project does not perform any keylogging or input monitoring.
