# Build, Tests & Verifikation

Ausführliche Build-, Test- und Verifikationsanweisungen für den CIY X79 RGB macOS Controller. Die kompakten Standardbefehle stehen im Root-[`AGENTS.md`](../../AGENTS.md).

---

## 1. Weiterführende Dokumentation
- Für Hardware-Sicherheitsregeln (Layouts, Pacing, Timer, Overlays): [`hardware-safety.md`](hardware-safety.md)
- Für Low-Level USB-HID Transfers, MCU-Verhalten & den Test-Mock-Modus (`CIY_TESTING=1`): [`hardware-protocol.md`](hardware-protocol.md)

---

## 2. Tests ausführen
- **Standardbefehl** (vom Workspace-Root): `./scripts/run_tests.sh`
- Umfasst HID-Tests, ASan- & UBSan-geprüfte Unit-Tests sowie CLI-Tests.
- Läuft mit `CIY_TESTING=1` und kompiliert die Testbinaries mit `-DCIY_ENABLE_TEST_HOOKS=1` (virtuelles Mock-HID-Device im Arbeitsspeicher; kein IOKit-Handle, kein USB-Transfer an physische Hardware) und `CIY_SANITIZE=1`. `build_app.sh` definiert das Makro nicht und kompiliert die Hooks aus.
- Zum Test-Mock-Modus: siehe [`hardware-protocol.md`](hardware-protocol.md#5-hardware-isolation-bei-automatisierten-tests-ciy_testing1).

## 3. App & CLI bauen
- **Standardbefehl** (vom Workspace-Root): `./scripts/build_app.sh`
- Erzeugt Universal Binaries (`arm64` + `x86_64`): `dist/CIYX79Control.app` und `dist/ciy_ctl`.
- Compiler-Flags: `-mmacosx-version-min=12.0`, `-O2`, `-Wall -Wextra`, `-fobjc-arc`, Ad-hoc-Codesign.

## 4. CLI einzeln kompilieren (optional)
Für schnelle CLI-only-Iteration ohne App-Bundle (entspricht dem CLI-Schritt von `build_app.sh`):

```bash
clang -O2 -mmacosx-version-min=12.0 -arch arm64 -arch x86_64 -Wall -Wextra \
  -framework IOKit -framework CoreFoundation \
  src/core/ciy_hid.c src/layouts/ciy_x79_iso_de.c src/cli/ciy_ctl.c \
  -o dist/ciy_ctl
```
