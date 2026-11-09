# Workspace- & Dateistruktur-Regeln

## 1. Regel
- **Konfigurations- und Regeldateien** (wie `AGENTS.md`) dürfen **ausschließlich im Workspace-Root-Verzeichnis** liegen.
- Niemals Regel-, Prompt- oder Agenten-Konfigurationsdateien in Subfoldern/Unterprojekten anlegen oder dorthin kopieren.

## 2. Abgrenzung: Verlinkte Detail-Dokumentation
- Von Root-Dateien verlinkte **Referenzdokumentation** (z. B. dieses Verzeichnis `docs/agent/`) ist keine Regeldatei im Sinne von Regel 1: Sie wird nicht automatisch geladen, sondern nur bei Bedarf über Links aus dem Root-[`AGENTS.md`](../../AGENTS.md) gelesen (Progressive Disclosure).
- Die kanonische Agenten-Konfiguration verbleibt zwingend im Root (`AGENTS.md`).

## 3. Bestehende Struktur
```
CIY_X79_macOS/
├── AGENTS.md                      # Minimale Root-Regeln + Links
└── docs/agent/                    # Verlinkte Detail-Dokumentation
    ├── api-quotas.md
    ├── build-verification.md
    ├── data-formats.md
    ├── hardware-protocol.md
    ├── hardware-safety.md
    ├── tool-discipline.md
    └── workspace-structure.md
```
