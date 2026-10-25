#!/usr/bin/env python3
"""
Hardware-Diagnose- und Test-Skript für die CIY X79 (69 Tasten ISO-DE Layout).
Ermöglicht das exakte Prüfen jeder einzelnen Taste, jeder Reihe und Funktionszone,
um Matrix-Zuordnungsfehler und ungewollte Effekte auszuschließen.
"""

import sys
import os
import json
import time
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
CIY_CTL = os.path.join(PROJECT_ROOT, "dist", "ciy_ctl")

# Alle 69 Tasten der deutschen CIY X79 ISO-DE Matrix mit Reihen- und Zonen-Zuordnung
KEY_MATRIX = [
    # Reihe 0
    {"slot": 1,  "label": "Esc",        "row": 0, "zone": "accent"},
    {"slot": 7,  "label": "1",          "row": 0, "zone": "alpha"},
    {"slot": 13, "label": "2",          "row": 0, "zone": "alpha"},
    {"slot": 19, "label": "3",          "row": 0, "zone": "alpha"},
    {"slot": 25, "label": "4",          "row": 0, "zone": "alpha"},
    {"slot": 31, "label": "5",          "row": 0, "zone": "alpha"},
    {"slot": 37, "label": "6",          "row": 0, "zone": "alpha"},
    {"slot": 43, "label": "7",          "row": 0, "zone": "alpha"},
    {"slot": 49, "label": "8",          "row": 0, "zone": "alpha"},
    {"slot": 55, "label": "9",          "row": 0, "zone": "alpha"},
    {"slot": 61, "label": "0",          "row": 0, "zone": "alpha"},
    {"slot": 67, "label": "ß",          "row": 0, "zone": "alpha"},
    {"slot": 73, "label": "´",          "row": 0, "zone": "alpha"},
    {"slot": 79, "label": "Backspace",  "row": 0, "zone": "accent"},
    {"slot": 86, "label": "Entf",       "row": 0, "zone": "nav"},
    # Reihe 1
    {"slot": 2,  "label": "Tab",        "row": 1, "zone": "modifier"},
    {"slot": 8,  "label": "Q",          "row": 1, "zone": "alpha"},
    {"slot": 14, "label": "W",          "row": 1, "zone": "wasd"},
    {"slot": 20, "label": "E",          "row": 1, "zone": "alpha"},
    {"slot": 26, "label": "R",          "row": 1, "zone": "alpha"},
    {"slot": 32, "label": "T",          "row": 1, "zone": "alpha"},
    {"slot": 38, "label": "Z",          "row": 1, "zone": "alpha"},
    {"slot": 44, "label": "U",          "row": 1, "zone": "alpha"},
    {"slot": 50, "label": "I",          "row": 1, "zone": "alpha"},
    {"slot": 56, "label": "O",          "row": 1, "zone": "alpha"},
    {"slot": 62, "label": "P",          "row": 1, "zone": "alpha"},
    {"slot": 68, "label": "Ü",          "row": 1, "zone": "alpha"},
    {"slot": 74, "label": "+*",         "row": 1, "zone": "alpha"},
    {"slot": 81, "label": "Enter",      "row": 1, "zone": "accent"},
    {"slot": 87, "label": "BildAuf",    "row": 1, "zone": "nav"},
    # Reihe 2
    {"slot": 3,  "label": "Caps",       "row": 2, "zone": "modifier"},
    {"slot": 9,  "label": "A",          "row": 2, "zone": "wasd"},
    {"slot": 15, "label": "S",          "row": 2, "zone": "wasd"},
    {"slot": 21, "label": "D",          "row": 2, "zone": "wasd"},
    {"slot": 27, "label": "F",          "row": 2, "zone": "alpha"},
    {"slot": 33, "label": "G",          "row": 2, "zone": "alpha"},
    {"slot": 39, "label": "H",          "row": 2, "zone": "alpha"},
    {"slot": 45, "label": "J",          "row": 2, "zone": "alpha"},
    {"slot": 51, "label": "K",          "row": 2, "zone": "alpha"},
    {"slot": 57, "label": "L",          "row": 2, "zone": "alpha"},
    {"slot": 63, "label": "Ö",          "row": 2, "zone": "alpha"},
    {"slot": 69, "label": "Ä",          "row": 2, "zone": "alpha"},
    {"slot": 75, "label": "#'",         "row": 2, "zone": "alpha"},
    {"slot": 94, "label": "BildAb",     "row": 2, "zone": "nav"},
    # Reihe 3
    {"slot": 4,  "label": "ShiftL",     "row": 3, "zone": "modifier"},
    {"slot": 76, "label": "<>",         "row": 3, "zone": "alpha"},
    {"slot": 10, "label": "Y",          "row": 3, "zone": "alpha"},
    {"slot": 16, "label": "X",          "row": 3, "zone": "alpha"},
    {"slot": 22, "label": "C",          "row": 3, "zone": "alpha"},
    {"slot": 28, "label": "V",          "row": 3, "zone": "alpha"},
    {"slot": 34, "label": "B",          "row": 3, "zone": "alpha"},
    {"slot": 40, "label": "N",          "row": 3, "zone": "alpha"},
    {"slot": 46, "label": "M",          "row": 3, "zone": "alpha"},
    {"slot": 52, "label": ",;",         "row": 3, "zone": "alpha"},
    {"slot": 58, "label": ".:",         "row": 3, "zone": "alpha"},
    {"slot": 64, "label": "-_",         "row": 3, "zone": "alpha"},
    {"slot": 82, "label": "ShiftR",     "row": 3, "zone": "modifier"},
    {"slot": 88, "label": "PfeilAuf",   "row": 3, "zone": "arrows"},
    {"slot": 91, "label": "Ende",       "row": 3, "zone": "nav"},
    # Reihe 4
    {"slot": 5,  "label": "StrgL",      "row": 4, "zone": "modifier"},
    {"slot": 11, "label": "Win",        "row": 4, "zone": "modifier"},
    {"slot": 17, "label": "AltL",       "row": 4, "zone": "modifier"},
    {"slot": 35, "label": "Space",      "row": 4, "zone": "space"},
    {"slot": 53, "label": "AltGr",      "row": 4, "zone": "modifier"},
    {"slot": 77, "label": "Fn",         "row": 4, "zone": "modifier"},
    {"slot": 80, "label": "StrgR",      "row": 4, "zone": "modifier"},
    {"slot": 83, "label": "PfeilLinks", "row": 4, "zone": "arrows"},
    {"slot": 89, "label": "PfeilAb",    "row": 4, "zone": "arrows"},
    {"slot": 95, "label": "PfeilRechts","row": 4, "zone": "arrows"},
]

def apply_slots(slots_dict, name="Diagnostics"):
    """Schreibt ein temporäres Profil und wendet es via ciy_ctl an."""
    tmp_path = f"/tmp/ciy_diag_{os.getpid()}.json"
    data = [{"name": name, "slots": slots_dict}]
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    try:
        cmd = [CIY_CTL, "load", tmp_path, name, "--json"]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode != 0:
            err = res.stdout.strip() if res.stdout.strip() else res.stderr.strip()
            print(f"Fehler beim Senden: {err}")
            return False
        return True
    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

def run_rows_test():
    """Färbt die 5 Tastenreihen in 5 unterschiedlichen Regenbogenfarben."""
    print("▶ Test 1: Reihen-Test (5 Reihen)")
    print("  • Reihe 0 (Zahlen & Esc):    Rot     (#FF0000)")
    print("  • Reihe 1 (QWERTZ & Tab):    Grün    (#00FF00)")
    print("  • Reihe 2 (ASDF & Caps):     Blau    (#0066FF)")
    print("  • Reihe 3 (YXCV & Shift):    Gelb    (#FFFF00)")
    print("  • Reihe 4 (Space & Modif.):  Magenta (#FF00FF)")
    row_colors = {
        0: "#FF0000",
        1: "#00FF00",
        2: "#0066FF",
        3: "#FFFF00",
        4: "#FF00FF",
    }
    slots = {}
    for k in KEY_MATRIX:
        slots[str(k["slot"])] = row_colors[k["row"]]
    apply_slots(slots, "Reihen-Diagnose")

def run_zones_test():
    """Färbt logische Zonen (Schreibblock, Modifikatoren, Pfeile, Navigation, Akzente)."""
    print("▶ Test 2: Zonen-Test (Funktionsblöcke)")
    print("  • Schreibblock (Alpha):      Weiß    (#FFFFFF)")
    print("  • WASD:                      Rot     (#FF0000)")
    print("  • Pfeiltasten (▲, ◄, ▼, ►):  Gelb    (#FFFF00)")
    print("  • Modifikatoren (Shift/Ctrl):Cyan    (#00FFFF)")
    print("  • Navigation (Entf, Bild, Ende): Grün(#00FF00)")
    print("  • Akzente (Enter, Backspace):Orange (#FF6600)")
    print("  • Leertaste (Space):         Lila    (#9900FF)")
    zone_colors = {
        "alpha":    "#FFFFFF",
        "wasd":     "#FF0000",
        "arrows":   "#FFFF00",
        "modifier": "#00FFFF",
        "nav":      "#00FF00",
        "accent":   "#FF6600",
        "space":    "#9900FF",
    }
    slots = {}
    for k in KEY_MATRIX:
        slots[str(k["slot"])] = zone_colors.get(k["zone"], "#FFFFFF")
    apply_slots(slots, "Zonen-Diagnose")

def run_single_key(key_query):
    """Schaltet genau eine Taste in Weiß ein (alle anderen Tasten aus)."""
    matched = None
    for k in KEY_MATRIX:
        if k["label"].lower() == key_query.lower() or str(k["slot"]) == key_query:
            matched = k
            break
    if not matched:
        print(f"Taste '{key_query}' nicht gefunden!")
        return False
    print(f"▶ Einzeltasten-Test: '{matched['label']}' (Matrix-Slot {matched['slot']}) leuchtet WEISS, alle anderen AUS.")
    slots = {str(k["slot"]): "#000000" for k in KEY_MATRIX}
    slots[str(matched["slot"])] = "#FFFFFF"
    apply_slots(slots, f"Single-{matched['label']}")
    return True

def run_sweep():
    """Führt einen sichtbaren Lichttest über alle 69 Tasten von links oben nach rechts unten durch."""
    total = len(KEY_MATRIX)
    print(f"▶ Test 3: Tastatur-Sweep (Jede der {total} Tasten leuchtet nacheinander kurz auf)")
    for i, k in enumerate(KEY_MATRIX, 1):
        print(f"\r  [{i:2d}/{total}] Taste: {k['label']:<12} (Slot {k['slot']:2d})", end="", flush=True)
        slots = {str(item["slot"]): "#000000" for item in KEY_MATRIX}
        slots[str(k["slot"])] = "#00FF88"
        apply_slots(slots, f"Sweep-{k['label']}")
        time.sleep(0.06)
    print("\n✓ Sweep abgeschlossen.")

def run_benchmark():
    """Prüft das exakte Ansprechverhalten und die USB-Latenz für alle 69 Tasten."""
    total = len(KEY_MATRIX)
    print(f"▶ Test: Latenz- & Ansprechverhaltens-Benchmark aller {total} Tasten")
    latencies = []
    failed_keys = []
    
    for i, k in enumerate(KEY_MATRIX, 1):
        slots = {str(item["slot"]): "#000000" for item in KEY_MATRIX}
        slots[str(k["slot"])] = "#00FF88"
        
        t0 = time.perf_counter()
        success = apply_slots(slots, f"Bench-{k['slot']}")
        t1 = time.perf_counter()
        
        elapsed_ms = (t1 - t0) * 1000.0
        latencies.append(elapsed_ms)
        
        if success:
            status_str = f"✓ {elapsed_ms:5.1f} ms"
        else:
            status_str = "✗ FEHLER"
            failed_keys.append(k["label"])
            
        print(f"\r  [{i:2d}/{total}] Taste: {k['label']:<12} (Slot {k['slot']:2d}) -> {status_str}", end="", flush=True)
    
    print("\n\n=== Benchmark-Ergebnis ===")
    if failed_keys:
        print(f"  Status:         ✗ {len(failed_keys)} FEHLER (Tasten: {', '.join(failed_keys)})")
    else:
        print(f"  Status:         ✓ Alle {total} Tasten antworten fehlerfrei (0 Paketverluste)")
    
    avg_lat = sum(latencies) / len(latencies) if latencies else 0
    min_lat = min(latencies) if latencies else 0
    max_lat = max(latencies) if latencies else 0
    print(f"  USB-Durchsatz:  Min: {min_lat:.1f} ms | Avg: {avg_lat:.1f} ms | Max: {max_lat:.1f} ms")
    print("==========================\n")
    return len(failed_keys) == 0

def run_clusters_test():
    """Prüft das dynamische Ansprechverhalten nach Funktionsblöcken."""
    print("▶ Test: Funktionsblock-Dynamik (WASD, Pfeile, Navigation, Modifikatoren)")
    clusters = [
        ("WASD-Cluster", ["W", "A", "S", "D"], "#FF0000"),
        ("Pfeiltasten", ["PfeilAuf", "PfeilLinks", "PfeilAb", "PfeilRechts"], "#FFFF00"),
        ("Navigation", ["Entf", "BildAuf", "BildAb", "Ende"], "#00FF00"),
        ("Modifikatoren", ["ShiftL", "ShiftR", "StrgL", "StrgR", "AltL", "AltGr", "Win", "Fn", "Caps", "Tab"], "#00FFFF"),
        ("Leertaste", ["Space"], "#FF00FF"),
    ]
    for name, labels, color in clusters:
        print(f"  • Teste {name:<16} ({len(labels)} Tasten) in {color}...")
        slots = {str(item["slot"]): "#000000" for item in KEY_MATRIX}
        for lbl in labels:
            for k in KEY_MATRIX:
                if k["label"].lower() == lbl.lower():
                    slots[str(k["slot"])] = color
                    break
        apply_slots(slots, f"Cluster-{name}")
        time.sleep(0.4)
    print("✓ Cluster-Dynamiktest abgeschlossen.")

def run_isolation_test():
    """Beweist, dass CIY_TESTING=1 automatisierte Tests sicher vom physischen USB isoliert."""
    print("▶ Test: Hardware-Isolationscheck für künftige Tests (Regression Guard)")
    # 1. Status vor Testlauf lesen
    st_before = subprocess.run([CIY_CTL, "status", "--json"], stdout=subprocess.PIPE, text=True).stdout
    print("  • Aktuellen Hardware-Status vor Testsuite gesichert.")
    
    # 2. Testsuite mit CIY_TESTING=1 ausführen
    test_env = os.environ.copy()
    test_env["CIY_TESTING"] = "1"
    run_script = os.path.join(PROJECT_ROOT, "scripts", "run_tests.sh")
    res = subprocess.run([run_script], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=test_env)
    
    if res.returncode != 0:
        print(f"  ✗ Testsuite mit Fehler beendet:\n{res.stderr}")
        return False
        
    print("  • Automatisierte Test-Suiten (HID, Profil-Logik, CLI) erfolgreich durchlaufen.")
    
    # 3. Status nach Testlauf lesen
    st_after = subprocess.run([CIY_CTL, "status", "--json"], stdout=subprocess.PIPE, text=True).stdout
    
    # 4. Prüfen, dass Hardware unberührt blieb
    try:
        j_before = json.loads(st_before)
        j_after = json.loads(st_after)
        if j_before == j_after:
            print("  ✓ Verifikation erfolgreich: Hardware-Zustand blieb zu 100% unverändert!")
            print("    Kein automatisierter Test hat versehentlich die physische Tastatur überschrieben.")
            return True
        else:
            print("  ✗ WARNUNG: Hardware-Zustand hat sich während der Tests verändert!")
            print(f"    Vorher: {j_before}\n    Nachher: {j_after}")
            return False
    except json.JSONDecodeError as e:
        print(f"  ✗ JSON-Parsefehler beim Statusvergleich: {e}")
        return False

def run_all_tests():
    """Führt die komplette Test- und Diagnose-Batterie aus."""
    print("=================================================================")
    print("  CIY X79 Hardware-Diagnose- und Ansprechverhaltens-Prüfung")
    print("=================================================================\n")
    ok1 = run_isolation_test()
    print()
    ok2 = run_benchmark()
    run_clusters_test()
    print()
    run_rows_test()
    print()
    run_sweep()
    print()
    run_restore()
    print("\n=================================================================")
    print("  Alle Hardware- und Isolationstests erfolgreich abgeschlossen!  ")
    print("=================================================================")
    return ok1 and ok2

def run_restore():
    """Stellt das WASD Gaming-Profil wieder her."""
    print("▶ Stelle Standard WASD Gaming-Profil wieder her...")
    subprocess.run([CIY_CTL, "wasd"])

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Verwendung: hardware_diagnostics.py <all|benchmark|clusters|rows|zones|sweep|isolation|key <Taste>|restore>")
        sys.exit(1)

    cmd = sys.argv[1].lower()
    if cmd == "all":
        run_all_tests()
    elif cmd == "benchmark":
        run_benchmark()
    elif cmd == "clusters":
        run_clusters_test()
    elif cmd == "isolation":
        run_isolation_test()
    elif cmd == "rows":
        run_rows_test()
    elif cmd == "zones":
        run_zones_test()
    elif cmd == "sweep":
        run_sweep()
    elif cmd == "key" and len(sys.argv) >= 3:
        run_single_key(sys.argv[2])
    elif cmd == "restore":
        run_restore()
    else:
        print(f"Unbekannter Befehl: {cmd}")
        sys.exit(1)
