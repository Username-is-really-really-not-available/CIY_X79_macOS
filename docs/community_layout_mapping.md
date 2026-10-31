# Community Layout Mapping Guide: CIY X79 (ANSI-US & Varianten)

Dieser Leitfaden richtet sich an Besitzer einer **CIY X79 mit ANSI-US** oder anderen physischen Tastaturlayouts, die bei der Erweiterung der Layout-Unterstützung mithelfen möchten.

---

## 1. Warum gibt es noch kein ANSI-US Standard-Layout?

Gemäß den Entwicklungsrichtlinien dieses Projekts werden **keine spekulativen oder ungetesteten Layouts** in den Produktionscode aufgenommen. Da den Entwicklern physisch ein ISO-DE Modell vorliegt, ist dieses zu 100 % verifiziert. 

Um ANSI-US (oder ISO-UK, ANSI-Nordic etc.) offiziell zu unterstützen, benötigen wir eine verifizierte Zuordnung aller Matrix-Slots und LED-Indizes zu den physischen Tasten.

---

## 2. Methode A: Zero-Install WebHID Community Mapper (Empfohlen)

Nutzen Sie den browserbasierten WebHID Mapper direkt in Chrome, Edge, Arc oder Brave ohne Installation:
- **Online (GitHub Pages):** `https://[username].github.io/CIY_X79_macOS/mapper/`
- **Lokal:** Starten Sie `python3 -m http.server 8000` im Repository und öffnen Sie `http://localhost:8000/docs/mapper/`.

Der WebHID Mapper bietet ein interaktives Vektor-Canvas für ISO-DE und ANSI-US, führt Sie per Sweep schrittweise durch alle LEDs und generiert mit einem Klick das fertige JSON für ein GitHub-Issue.

---

## 3. Methode B: Terminal-CLI `ciy_ctl` (Für Entwickler)

Stellen Sie sicher, dass das CLI-Werkzeug `ciy_ctl` gebaut ist:
```bash
./scripts/build_app.sh
# oder direkt kompilieren:
clang -O2 -Wall -Wextra src/core/ciy_hid.c src/layouts/ciy_x79_iso_de.c src/cli/ciy_ctl.c -framework IOKit -framework CoreFoundation -o dist/ciy_ctl
```

---

## 3. Schrittweiser Durchlauf mit `ciy_ctl sweep`

Der Befehl `sweep` schaltet schrittweise alle möglichen LED-Plätze der Sino-Wealth-Matrix (Slots 0 bis 124, LED-Indizes 1 bis 125) einzeln durch.

### Interaktiver Modus (Standard)
Starten Sie den Sweep im Terminal:
```bash
./dist/ciy_ctl sweep
```

**Bedienelemente im Sweep:**
- `[Enter]` oder `[n]`: Schaltet zur nächsten LED weiter.
- `[p]`: Springt zur vorherigen LED zurück.
- `[q]`: Beendet den Sweep vorzeitig.
- `Ctrl+C`: Bricht sofort ab. Dank Signal-Handling (`SIGINT`) wird das Terminal sofort wiederhergestellt und die Tastatur in ihren ursprünglichen Beleuchtungsstatus zurückversetzt (keine hängenden LEDs!).

### Automatischer Modus (`--auto`)
Falls Sie den Durchlauf lieber automatisch ablaufen lassen möchten:
```bash
./dist/ciy_ctl sweep --auto --delay-ms 750
```
*(Hinweis: Die Mindestverzögerung beträgt 100 ms, empfohlen werden 500–750 ms).*

---

## 4. Gezielte Tastenprüfung mit `ciy_ctl probe-led`

Möchten Sie eine bestimmte LED oder einen Matrix-Slot gezielt ansteuern:
```bash
# LED-Index 15 mit Farbe Grün testen:
./dist/ciy_ctl probe-led 15 0 255 0

# Matrix-Slot 14 mit Hex-Farbe Blau testen:
./dist/ciy_ctl probe-led --slot 14 #0000FF

# Mit Helligkeit 3 ansteuern:
./dist/ciy_ctl probe-led 2 255 0 0 -b 3
```

---

## 5. Erstellen und Einreichen eines Layout-Mappings

Notieren Sie für jede aufleuchtende LED die entsprechende physische Taste auf Ihrem Modell:

| Matrix-Slot | LED-Index (= Slot + 1) | Physische Taste (z. B. ANSI-US) |
|---|---|---|
| 1 | 2 | Esc |
| 7 | 8 | 1 |
| ... | ... | ... |
| 79 | 80 | Backspace |
| 81 | 82 | Enter (1-zeilig bei ANSI) |

### JSON-Export als Vorlage nutzen
Sie können das bestehende ISO-DE Layout als strukturelle Vorlage exportieren:
```bash
./dist/ciy_ctl export-layout --json > ciy_x79_ansi_us.json
```

Passen Sie die Tastenbezeichnungen und Matrix-Slots entsprechend Ihren Notizen an.

### Validierung des neuen Profils
Vor dem Absenden können Sie Ihre Datei hardwarefrei validieren:
```bash
./dist/ciy_ctl validate-profile ciy_x79_ansi_us.json
```

### Einreichung
Erstellen Sie ein Issue oder einen Pull Request auf GitHub mit:
1. Ihrer `ciy_x79_ansi_us.json` oder Tabelle.
2. Einem Foto Ihrer CIY X79 Tastatur mit der Tastenbeschriftung.
3. Der Bestätigung, dass die Tasten via `probe-led` erfolgreich angesteuert wurden.
