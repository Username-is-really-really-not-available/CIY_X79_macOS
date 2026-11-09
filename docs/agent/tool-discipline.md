# Tool- & Token-Disziplin

Regeln für effizienten Agenten-Betrieb in diesem Workspace: Kontext schlank halten, Tool-Aufrufe bündeln, Reviews diff-basiert. Die kompakten Regeln stehen im Root-[`AGENTS.md`](../../AGENTS.md).

---

## 1. Regeln

1. **Shell-Aufrufe bündeln:** Verkettung via `&&`; niemals Einzel-Turns für aufeinanderfolgende Tests/Builds verschwenden.
2. **Ausgaben filtern:** Diagnose-Tools (`otool`, `vtool`, Compiler, Test-Suites) zwingend per `| tail -n 40` oder `grep` filtern. Kein ungefiltertes `otool -l`.
3. **Keine Re-Reads:** Bereits gelesene Dateien im selben Task nicht erneut komplett via `read_file` laden.
4. **Diff-basierte Reviews:** Code-Reviews strikt auf den Git-Diff beschränken (`git diff HEAD~1`, `git diff --staged` oder gezielte Dateien). Kein unaufgefordertes, präemptives Einlesen vollständiger Quellcodedateien oder des gesamten Repositorys.

---

## 2. Abgrenzung: Effizienz vs. Hardware-Pacing
- Regel 1 (Bündeln) gilt für Tests, Builds und allgemeine Shell-Operationen.
- Sie gilt **nicht** für Hardware-Probing: Diagnose-Befehle (`sweep`, `probe-led`) bleiben aus Hardware-Sicherheitsgründen strikt interaktiv — siehe [`hardware-safety.md`](hardware-safety.md#3-interaktives-pacing-als-standard).
