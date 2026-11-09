# Hardware-Sicherheit & Layout-Disziplin

Sicherheitskritische Coding-Regeln für den CIY X79 RGB macOS Controller: Layout-Disziplin, USB-Pacing, Timer-Sicherheit und Overlay-Schutz. Die kompakten Regeln stehen im Root-[`AGENTS.md`](../../AGENTS.md).

---

## 1. Weiterführende Dokumentation
- Für Low-Level USB-HID Transfers, MCU-Verhalten & Bus-Lockfile: [`hardware-protocol.md`](hardware-protocol.md)
- Für Report-Formate, LED-Index-Offsets & Farb-Klemmung: [`data-formats.md`](data-formats.md)

---

## 2. Keine spekulativen Layouts
- Niemals Tastaturlayouts oder Tastenmatrizen (z. B. ANSI-US) ohne physisch vorhandene Test-Hardware in den Produktionscode aufnehmen.
- Ungetestete Hardware bleibt im Community-Bereich (Mapping-Tools, Issues), bis empirische Berichte vorliegen.
- *Hintergrund:* Falsche Matrix-Mappings sind ohne Test-Hardware nicht verifizierbar und führen zu fehlerhaften LED-Zuordnungen.

## 3. Interaktives Pacing als Standard
- Diagnose- und Probing-Befehle (`sweep`, `probe-led`) müssen **standardmäßig strikt interaktiv** sein: `[Enter]` für nächste Taste, `[q]` für Abbruch.
- Schnelle automatische USB-Schleifen (< 500 ms) sind als Default verboten.
- *Hintergrund:* Verhindert XRAM-Pufferüberläufe und Bus-Lock-Kollisionen (Lockfile-Timeout 2,0 s — siehe [`hardware-protocol.md`](hardware-protocol.md#4-multi-process-bus-lockfile)).

## 4. Schlafzustand & Timer-Sicherheit
- Asynchrone Debounce-Timer und Queues müssen bei System-Suspend (`onSystemWillSleep:`) und App-Beendigung zwingend storniert und invalidiert werden.
- Mechanismen: `cancelPreviousPerformRequestsWithTarget` / `[_debounceTimer invalidate]`.
- *Hintergrund:* Verhindert Feuer-Event-Überreste nach Wake sowie Race Conditions mit dem USB-Dispatch.

## 5. Ephemerer Overlay-Schutz
- Dynamische Statusanzeigen (Akku, Monitoring) dürfen niemals persistente Profil-Matrizen (`_keyColors`, `NSUserDefaults`) mutieren.
- Stattdessen: rein flüchtiges Overlay rendern und erst im transienten USB-Dispatch-Puffer mischen.
- *Hintergrund:* Schützt persistente Benutzerprofile vor Überschreibung durch temporäre Statusdaten.
