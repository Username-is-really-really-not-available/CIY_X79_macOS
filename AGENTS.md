# CIY X79 RGB macOS Controller

Native C11 & Objective-C (Cocoa/AppKit) Controller für die CIY X79 RGB Tastatur unter macOS — null externe Abhängigkeiten, unprivilegierter IOKit User-Space (USB-HID Control Transfers, Universal Binary `arm64`/`x86_64`). Low-Level-Protokoll & MCU-Verhalten: [`docs/agent/hardware-protocol.md`](docs/agent/hardware-protocol.md).

## Build- & Laufzeitumgebung
- Zero external dependencies: Keine Paketmanager oder externen Build-Systeme (`brew`, `npm`, `xcodebuild`, `docker`) voraussetzen oder aufrufen — C/Objective-C direkt mit `clang` kompilieren (Debug: `-g`).
- Zielplattform: strikt macOS 12.0+ (Apple Silicon & Intel); alle macOS-Builds setzen `-mmacosx-version-min=12.0` (CI läuft auf `macos-latest`).
- Linux-Cloud-Umgebungen: Cocoa-/AppKit-/IOKit-Targets bauen ausschließlich auf macOS; Core-Header, `ciy_hid_hidapi.c` und `contrib/openrgb/` sind plattformunabhängig unter Linux testbar.

## Befehle
- **Tests:** `./scripts/run_tests.sh` — HID-, ASan- & UBSan-geprüfte Unit-Tests sowie CLI-Tests mit `CIY_TESTING=1`
- **App & CLI bauen:** `./scripts/build_app.sh` — erzeugt Universal Binary `dist/CIYX79Control.app` und `dist/ciy_ctl` (Ad-hoc-Codesign)

## Nicht-verhandelbare Sicherheitsregeln
1. **Keine spekulativen Layouts:** Tastaturlayouts/-matrizen (z. B. ANSI-US) nur mit physisch vorhandener Test-Hardware in Produktionscode aufnehmen; ungetestete Hardware bleibt im Community-Bereich.
2. **Interaktives Pacing:** Diagnose-/Probing-Befehle (`sweep`, `probe-led`) standardmäßig strikt interaktiv (`[Enter]` nächste Taste, `[q]` Abbruch); automatische USB-Schleifen < 500 ms als Default verboten.
3. **Timer-Sicherheit:** Asynchrone Debounce-Timer/Queues bei System-Suspend (`onSystemWillSleep:`) und App-Beendigung stornieren & invalidieren.
4. **Ephemerer Overlay-Schutz:** Dynamische Statusanzeigen (Akku, Monitoring) dürfen persistente Profil-Matrizen (`_keyColors`, `NSUserDefaults`) niemals mutieren — nur flüchtiges Overlay im transienten USB-Dispatch-Puffer mischen.

→ Begründungen & Mechanismen: [`docs/agent/hardware-safety.md`](docs/agent/hardware-safety.md)

## Agent-Verhalten (Token- & Tool-Disziplin)
- Shell-Aufrufe bündeln (`&&`); Diagnose-Ausgaben zwingend filtern (`| tail -n 40`/`grep`); keine Re-Reads gelesener Dateien; Reviews strikt auf Git-Diff beschränken.
- Agenten-Konfigurationsdateien (z. B. `AGENTS.md`) liegen ausschließlich im Workspace-Root.

→ Details: [`docs/agent/tool-discipline.md`](docs/agent/tool-discipline.md), [`docs/agent/workspace-structure.md`](docs/agent/workspace-structure.md)

## Detaillierte Anleitungen (nur bei Bedarf lesen)
- [Build & Verifikation](docs/agent/build-verification.md) — Test-, Build- & CLI-Kompilierungsdetails
- [Hardware-Sicherheit & Layout-Disziplin](docs/agent/hardware-safety.md)
- [Tool- & Token-Disziplin](docs/agent/tool-discipline.md)
- [Workspace- & Dateistruktur-Regeln](docs/agent/workspace-structure.md)
- [API-Quotas, Hardware-Timings & Parsing](docs/agent/api-quotas.md) — **vor** Änderungen am API-Parsing/Quota-Tracking lesen
- [Hardware-Protokoll & MCU-Besonderheiten](docs/agent/hardware-protocol.md)
- [Datenformate & Matrix-Layout](docs/agent/data-formats.md)
