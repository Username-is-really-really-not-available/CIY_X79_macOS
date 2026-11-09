# Hardware-Protokoll & MCU-Besonderheiten

Referenzdokumentation für Low-Level USB-HID Transfers, Sinowealth MCU-Verhalten, Timing-Regeln und Thread-/Prozess-Synchronisation.

---

## 1. USB-HID Kommunikation

* **Vendor ID (VID):** `0x258A` (Sino Wealth)
* **Product ID (PID):** `0x0049`
* **Target Interface:** Interface `1` (Vendor-Defined HID Class; Interface `0` ist das Standard-Boot-Keyboard).
* **Transfer-Typ:** USB Control Transfers an Endpoint 0 via IOKit (`IOUSBDeviceInterface::DeviceRequestTO()`; bewusst kein `USBDeviceOpen()`).
* **Report-Typ:** Feature Report (`kIOHIDReportTypeFeature`).
* **Gerätespezifischer Scope:** VID `0x258A` / PID `0x0049` wird von verschiedenen OEM-Tastaturen geteilt (u. a. Hyrax HCK61, Redragon K628Pro). Manche besitzen nur 1 Interface; die vorliegende Spezifikation ist strikt für die CIY X79 (ISO-DE, Interface 1) validiert.

---

## 2. Kritische Übertragungsreihenfolge (Modus 19 Custom Per-Key RGB)

Report 0x0B schreibt Daten direkt in den flüchtigen MCU-XRAM und setzt nicht voraus, dass Modus 19 vorher aktiv ist. Beim Setzen benutzerdefinierter Per-Key LED-Farben muss eine strikte Drei-Schritt-Sequenz eingehalten werden:

1. **Planar-Puffer senden:** `Report 0x0B` (379 Bytes Planar RGB) an Interface 1 übertragen.
2. **Konservative Wartezeit (~50 ms):** `usleep(50000)` einlegen (empirisch ab 40 ms stabil, < 30 ms fehlerhaft; 50 ms mit Sicherheitsreserve analog OEM-Windows-Treiber `Sleep(50)`).
3. **Modus aktivieren:** `Report 0x0A` mit `mode = 19 (0x13)` senden.

> **Achtung:** Wird die Reihenfolge vertauscht oder die 50 ms Pause ausgelassen, verwirft die MCU den XRAM-Puffer und schaltet in Standardeffekte zurück.

---

## 3. Volatiler XRAM

* Die Sinowealth MCU speichert benutzerdefinierte Farbmatrix-Daten ausschließlich im flüchtigen **XRAM** (kein interner Flash-Speicher).
* Sobald Hardware-Tastenkombinationen wie `Fn + ;` an der Tastatur betätigt werden, überschreibt die Firmware den XRAM sofort mit internen Animationsmustern.
* Benutzerdefinierte Farbprofile müssen in solchen Fällen über die Software (App oder CLI) neu synchronisiert werden.

---

## 4. Multi-Process Bus Lockfile

Zur Vermeidung von Race Conditions und kollidierenden Control Transfers zwischen dem Cocoa-Hintergrund-Thread (`_hwQueue`) und dem CLI-Werkzeug (`ciy_ctl`):
* **Lockfile-Pfadauflösung:**
  * **macOS (Standard):** Primär im geschützten Per-User-Temp-Verzeichnis aus `confstr(_CS_DARWIN_USER_TEMP_DIR)` (`/var/folders/.../T/ciy_x79_bus_<UID>.lock`, Verzeichnismodus `0700`).
  * **Fallback (POSIX / Linux):** `$TMPDIR/ciy_x79_bus_<UID>.lock` bzw. `/tmp/ciy_x79_bus_<UID>.lock`.
  * **Testmodus (`CIY_TESTING=1`):** Prozess-isoliertes Lockfile `/tmp/ciy_x79_bus_<UID>_<PID>.lock` (bzw. im jeweiligen Temp-Verzeichnis) mit automatischer Bereinigung via `atexit`-Cleanup-Handler.
* **Sicherheitsbegründung (Per-UID-Isolierung):** Die Nutzung des `0700`-geschützten Darwin-User-Temp-Verzeichnisses sowie die Einbindung der `<UID>` verhindern Cross-User-Symlink-Angriffe (CWE-59) und lokale Denial-of-Service-Blockaden in einem gemeinsam genutzten `/tmp`-Verzeichnis (z. B. bei macOS Fast User Switching). Zusätzlich wird das Lockfile mit `O_CREAT | O_NOFOLLOW | O_CLOEXEC` geöffnet und per `fstat` gegen unreguläre Dateien oder fremde UIDs validiert.
* **Berechtigung:** `0600` (zugriffssicher pro macOS User-ID).
* **Timeout:** Maximal 2,0 Sekunden via `flock(fd, LOCK_EX | LOCK_NB)`. Bei Timeout wird abgebrochen.
* **Scope & akzeptiertes Restrisiko (SEC-T4-002):** Der Lock serialisiert Prozesse derselben UID; die 0x0B → 50 ms → 0x0A-Sequenz bleibt innerhalb einer UID atomar, weil `ciy_execute_transfer_plan()` den Lock über alle Schritte hält. Bei Fast User Switching können zwei UIDs Control Transfers überlappen; die MCU kann den XRAM-Puffer verwerfen und LEDs auf einen Standardeffekt zurücksetzen. Kein Speicherkorruptions- oder Privilegienaufstiegsrisiko; erneutes Anwenden des Profils behebt es.
* **Warum kein `USBDeviceOpen()`:** Die CIY X79 ist ein Composite Device; Interface 0 wird vom macOS-HID-Treiber beansprucht. `USBDeviceOpen()` liefert für unprivilegierte Prozesse `kIOReturnExclusiveAccess`; `USBDeviceOpenSeize()` benötigt Root und würde Tastatureingaben unterbrechen. Die bestehende `DeviceRequestTO()`-Nutzung an Interface 1 bleibt daher korrekt.
* **Warum keine Named POSIX Semaphore:** Ein globaler Lock würde lokalen DoS ermöglichen (dauerhaftes Halten), hat nach einem Absturz kein automatisches Stale-Lock-Recovery, ist von umask-/Berechtigungsproblemen betroffen und müsste den 50-ms-Zeitstempel zusätzlich global speichern. Für ein Low/Low-Restrisiko ist die Per-UID-Isolation die sicherere Wahl.
* `ciy_bus_lock()` wird um USB-Transfers bzw. komplette Transferpläne gehalten (nicht um `ciy_open()` selbst); nach Abschluss erfolgt die Freigabe per `ciy_bus_unlock()`.

---

## 5. Hardware-Isolation bei automatisierten Tests (`CIY_TESTING=1`)

* Automatisierte Unit- und Integrationstests müssen **stets** mit gesetzter Umgebungsvariable `CIY_TESTING=1` **und** mit `-DCIY_ENABLE_TEST_HOOKS=1` kompiliert werden.
* `./scripts/run_tests.sh` setzt beides automatisch. `build_app.sh` definiert das Makro nicht; Release-Binaries enthalten daher keine `CIY_TESTING`-/`CIY_FAIL_ON_HARDWARE_IO`-Pfade und ignorieren diese Umgebungsvariablen.
* In diesem Modus arbeitet `ciy_open()` mit einem virtuellen Mock-HID-Device im Arbeitsspeicher, bindet keine IOKit-Handles und überträgt keine USB-Pakete an physisch angeschlossene Hardware.
