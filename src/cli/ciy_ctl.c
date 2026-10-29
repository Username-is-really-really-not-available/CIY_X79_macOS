#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include "../core/ciy_hid.h"
#include "../layouts/ciy_layout.h"

// Sw_mode to effect names (from our verified ROM dump)
static const char *EFFECT_NAMES[] = {
    "Aus (Off)",                        // 0
    "Welle / Regenbogen (Wave)",        // 1
    "Atmung (Breathing)",               // 2
    "Reaktiv / Ripple",                 // 3
    "Regentropfen (Raindrops)",         // 4
    "Neon-Strom",                       // 5
    "Sternenhimmel",                    // 6
    "Schlange / Snake",                 // 7
    "Farbzyklus (Spectrum Cycle)",      // 8
    "Kreisel / Vortex",                 // 9
    "Komet / Meteor",                   // 10
    "Aurora / Polarlicht",              // 11
    "Windmühle",                        // 12
    "Blüte / Ripple Bloom",             // 13
    "Laser-Spur",                       // 14
    "Statisch / Solide (Solid Single Color)", // 15
    "Wellenbrecher",                    // 16
    "Glühen",                           // 17
    "Funkenflug",                       // 18
    "Benutzerdefiniert (Customize / Matrix)", // 19
    "Ripple / Regenbogen-Welle"         // 20
};

static const char *COLOR_NAMES[] = {
    "Rot",              // 0
    "Orange",           // 1
    "Gelb",             // 2
    "Grün",             // 3
    "Cyan / Türkis",    // 4
    "Blau",             // 5
    "Lila / Violett",   // 6
    "Weiß",             // 7
    "Bunt / Regenbogen",// 8
    "Farbwechsel 1",    // 9
    "Farbwechsel 2"     // 10
};

static void print_escaped_json_string(FILE *stream, const char *str) {
    if (!str) return;
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", stream); break;
            case '\\': fputs("\\\\", stream); break;
            case '\b': fputs("\\b", stream); break;
            case '\f': fputs("\\f", stream); break;
            case '\n': fputs("\\n", stream); break;
            case '\r': fputs("\\r", stream); break;
            case '\t': fputs("\\t", stream); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(stream, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, stream);
                }
                break;
        }
    }
}

// Parst eine Ganzzahl im Bereich [min, max]; false bei Text oder Bereichsverletzung.
static bool parse_range(const char *str, long min, long max, uint8_t *out) {
    if (!str) return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(str, &end, 10);
    if (errno != 0 || end == str || *end != '\0' || v < min || v > max) return false;
    *out = (uint8_t)v;
    return true;
}

static bool print_status(CIYDevice *dev) {
    CIYLightingState st;
    if (!ciy_get_state(dev, &st)) {
        fprintf(stderr, "Fehler beim Auslesen des Tastaturstatus.\n");
        return false;
    }

    const char *m_name = (st.mode <= CIY_MAX_MODE) ? EFFECT_NAMES[st.mode] : "Unbekannt";
    const char *c_name = (st.color <= CIY_MAX_COLOR) ? COLOR_NAMES[st.color] : "Unbekannt";

    printf("\n=== CIY X79 Tastatur-Status ===\n");
    printf("  Modus:       [%2u] %s\n", st.mode, m_name);
    printf("  Farbe:       [%2u] %s\n", st.color, c_name);
    printf("  Helligkeit:  %u / 5\n", st.brightness);
    printf("  Tempo:       %u / 4\n", st.speed);
    printf("================================\n\n");
    return true;
}

static bool print_status_json(CIYDevice *dev) {
    CIYLightingState st;
    if (!dev || !dev->dev || !ciy_get_state(dev, &st)) {
        printf("{\n  \"connected\": false,\n  \"error\": \"Fehler beim Auslesen des Tastaturstatus\"\n}\n");
        return false;
    }

    const char *m_name = (st.mode <= CIY_MAX_MODE) ? EFFECT_NAMES[st.mode] : "Unbekannt";
    const char *c_name = (st.color <= CIY_MAX_COLOR) ? COLOR_NAMES[st.color] : "Unbekannt";

    printf("{\n");
    printf("  \"connected\": true,\n");
    printf("  \"mode\": %u,\n", st.mode);
    printf("  \"mode_name\": \"%s\",\n", m_name);
    printf("  \"color\": %u,\n", st.color);
    printf("  \"color_name\": \"%s\",\n", c_name);
    printf("  \"brightness\": %u,\n", st.brightness);
    printf("  \"speed\": %u\n", st.speed);
    printf("}\n");
    return true;
}

static void print_modes_text(void) {
    printf("\nVerfügbare Beleuchtungsmodi:\n");
    for (int i = 0; i <= CIY_MAX_MODE; i++) {
        printf("  %2d: %s\n", i, EFFECT_NAMES[i]);
    }
    printf("\nVerfügbare Farbprofile:\n");
    for (int i = 0; i <= CIY_MAX_COLOR; i++) {
        printf("  %2d: %s\n", i, COLOR_NAMES[i]);
    }
    printf("\n");
}

static void print_modes_json(void) {
    printf("{\n  \"modes\": [\n");
    for (int i = 0; i <= CIY_MAX_MODE; i++) {
        printf("    {\"id\": %d, \"name\": \"%s\"}%s\n", i, EFFECT_NAMES[i], (i == CIY_MAX_MODE) ? "" : ",");
    }
    printf("  ],\n  \"colors\": [\n");
    for (int i = 0; i <= CIY_MAX_COLOR; i++) {
        printf("    {\"id\": %d, \"name\": \"%s\"}%s\n", i, COLOR_NAMES[i], (i == CIY_MAX_COLOR) ? "" : ",");
    }
    printf("  ]\n}\n");
}

// Extrahiert -b / --brightness / --brightness=<val> aus argv.
// Entfernt gefundene Flags aus argv und aktualisiert *argc.
static bool extract_brightness_option(int *argc, char **argv, uint8_t *out_brightness, bool *has_brightness, const char **err_str) {
    *has_brightness = false;
    *err_str = NULL;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--brightness") == 0) {
            if (i + 1 >= *argc) {
                *err_str = argv[i];
                return false;
            }
            uint8_t b = 0;
            if (!parse_range(argv[i + 1], 0, CIY_MAX_BRIGHTNESS, &b)) {
                *err_str = argv[i + 1];
                return false;
            }
            *out_brightness = b;
            *has_brightness = true;
            for (int j = i; j + 2 < *argc; j++) {
                argv[j] = argv[j + 2];
            }
            *argc -= 2;
            i--;
        } else if (strncmp(argv[i], "--brightness=", 13) == 0) {
            const char *val = argv[i] + 13;
            uint8_t b = 0;
            if (!parse_range(val, 0, CIY_MAX_BRIGHTNESS, &b)) {
                *err_str = val;
                return false;
            }
            *out_brightness = b;
            *has_brightness = true;
            for (int j = i; j + 1 < *argc; j++) {
                argv[j] = argv[j + 1];
            }
            (*argc)--;
            i--;
        }
    }
    return true;
}

static void print_export_layout_json(const CIYKeyboardLayout *layout) {
    if (!layout) return;
    printf("{\n");
    printf("  \"layout_id\": \"%s\",\n", layout->layout_id);
    printf("  \"display_name\": \"%s\",\n", layout->display_name);
    printf("  \"key_count\": %zu,\n", layout->key_count);
    printf("  \"surface_count\": %zu,\n", layout->surface_count);
    printf("  \"keys\": [\n");
    for (size_t i = 0; i < layout->key_count; i++) {
        const CIYLogicalKey *k = &layout->keys[i];
        int r_off = ciy_protocol_report_0b_r_offset(k->led_index);
        int g_off = ciy_protocol_report_0b_g_offset(k->led_index);
        int b_off = ciy_protocol_report_0b_b_offset(k->led_index);
        printf("    {\n");
        printf("      \"matrix_slot\": %d,\n", k->matrix_slot);
        printf("      \"led_index\": %d,\n", k->led_index);
        printf("      \"name\": \"");
        print_escaped_json_string(stdout, k->name);
        printf("\",\n");
        printf("      \"report_0b_r_offset\": %d,\n", r_off);
        printf("      \"report_0b_g_offset\": %d,\n", g_off);
        printf("      \"report_0b_b_offset\": %d\n", b_off);
        printf("    }%s\n", (i + 1 < layout->key_count) ? "," : "");
    }
    printf("  ],\n  \"surfaces\": [\n");
    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *s = &layout->surfaces[i];
        printf("    {\n");
        printf("      \"matrix_slot\": %d,\n", s->matrix_slot);
        printf("      \"label\": \"");
        print_escaped_json_string(stdout, s->label);
        printf("\",\n");
        printf("      \"row\": %d,\n", s->row);
        printf("      \"width_u\": %.2f\n", s->width_u);
        printf("    }%s\n", (i + 1 < layout->surface_count) ? "," : "");
    }
    printf("  ]\n}\n");
}

static void print_export_layout_text(const CIYKeyboardLayout *layout) {
    if (!layout) return;
    printf("\n=== CIY X79 (ISO-DE) Tastaturlayout & Report 0x0B Offsets ===\n");
    printf("Layout: %s [%s] (%zu Tasten, %zu Flächen)\n\n", layout->display_name, layout->layout_id, layout->key_count, layout->surface_count);
    printf("Logische Tasten (Matrix & Farb-Offsets im 379-Byte Report 0x0B):\n");
    printf("  Slot | LED | Name                 | R-Byte | G-Byte | B-Byte\n");
    printf("  -----+-----+----------------------+--------+--------+-------\n");
    for (size_t i = 0; i < layout->key_count; i++) {
        const CIYLogicalKey *k = &layout->keys[i];
        int r_off = ciy_protocol_report_0b_r_offset(k->led_index);
        int g_off = ciy_protocol_report_0b_g_offset(k->led_index);
        int b_off = ciy_protocol_report_0b_b_offset(k->led_index);
        printf("  %4d | %3d | %-20s | %6d | %6d | %6d\n",
               k->matrix_slot, k->led_index, k->name, r_off, g_off, b_off);
    }
    printf("\nGrafische Tastenflächen (UI-Geometrie):\n");
    printf("  Slot | Zeile | Breite   | Label\n");
    printf("  -----+-------+----------+----------------------\n");
    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *s = &layout->surfaces[i];
        printf("  %4d | %5d | %6.2f U | %s\n",
               s->matrix_slot, s->row, s->width_u, s->label);
    }
    printf("\n");
}

static void print_dry_run_plan(const CIYTransferPlan *plan, bool json_mode, bool dump_bytes) {
    if (json_mode) {
        printf("{\n");
        printf("  \"dry_run\": true,\n");
        printf("  \"hardware_opened\": false,\n");
        printf("  \"device\": {\n");
        printf("    \"name\": \"CIY X79\",\n");
        printf("    \"vid\": \"0x%04X\",\n", CIY_VENDOR_ID);
        printf("    \"pid\": \"0x%04X\",\n", CIY_PRODUCT_ID);
        printf("    \"interface\": %d\n", CIY_INTERFACE_NUM);
        printf("  },\n");
        printf("  \"profile\": \"");
        print_escaped_json_string(stdout, plan->profile_name[0] ? plan->profile_name : "Custom");
        printf("\",\n");
        printf("  \"keys\": %d,\n", plan->keys_count);
        printf("  \"brightness\": %u,\n", plan->brightness);
        printf("  \"plan\": [\n");
        for (size_t i = 0; i < plan->step_count; i++) {
            const CIYPlanStep *s = &plan->steps[i];
            printf("    {\n");
            printf("      \"step\": %zu,\n", i + 1);
            if (s->type == CIY_PLAN_STEP_REPORT) {
                printf("      \"type\": \"report\",\n");
                printf("      \"report_id\": \"0x%02X\",\n", s->report_id);
                printf("      \"bytes\": %u,\n", s->length);
                if (s->report_id == CIY_REPORT_0A_ID) {
                    printf("      \"mode\": %u,\n", s->mode);
                    if (s->mode != CIY_CUSTOM_MODE) {
                        printf("      \"color\": %u,\n", s->color);
                        printf("      \"speed\": %u,\n", s->speed);
                    }
                    printf("      \"brightness\": %u,\n", s->brightness);
                }
                if (dump_bytes) {
                    printf("      \"hex\": \"");
                    for (uint16_t b = 0; b < s->length; b++) {
                        printf("%02x", s->data[b]);
                    }
                    printf("\",\n");
                }
            } else if (s->type == CIY_PLAN_STEP_DELAY) {
                printf("      \"type\": \"delay\",\n");
                printf("      \"delay_ms\": %u,\n", s->delay_ms);
            }
            printf("      \"description\": \"%s\"\n", s->description);
            printf("    }%s\n", (i + 1 < plan->step_count) ? "," : "");
        }
        printf("  ],\n");
        printf("  \"result\": \"valid; hardware unchanged\"\n");
        printf("}\n");
    } else {
        printf("DRY RUN — no USB device opened; no HID transfers sent\n");
        printf("Device profile: CIY X79 (VID 0x%04X, PID 0x%04X, interface %d)\n",
               CIY_VENDOR_ID, CIY_PRODUCT_ID, CIY_INTERFACE_NUM);
        printf("Profile: %s\n", plan->profile_name[0] ? plan->profile_name : "Custom");
        printf("Plan: ");
        for (size_t i = 0; i < plan->step_count; i++) {
            const CIYPlanStep *s = &plan->steps[i];
            if (s->type == CIY_PLAN_STEP_REPORT) {
                if (s->report_id == CIY_REPORT_0A_ID) {
                    if (s->mode == CIY_CUSTOM_MODE) {
                        printf("Report 0x0A (%u bytes, mode %u, brightness %u)",
                               s->length, s->mode, s->brightness);
                    } else {
                        printf("Report 0x0A (%u bytes, mode %u, color %u, speed %u, brightness %u)",
                               s->length, s->mode, s->color, s->speed, s->brightness);
                    }
                } else {
                    printf("Report 0x%02X (%u bytes)", s->report_id, s->length);
                }
            } else {
                printf("wait %u ms", s->delay_ms);
            }
            if (i + 1 < plan->step_count) {
                printf(" -> ");
            }
        }
        printf("\n");
        if (dump_bytes) {
            printf("\nRaw Wire Bytes (Hex):\n");
            for (size_t i = 0; i < plan->step_count; i++) {
                const CIYPlanStep *s = &plan->steps[i];
                if (s->type == CIY_PLAN_STEP_REPORT) {
                    printf("  Schritt %zu (Report 0x%02X, %u Bytes):\n    ", i + 1, s->report_id, s->length);
                    for (uint16_t b = 0; b < s->length; b++) {
                        printf("%02x", s->data[b]);
                        if ((b + 1) % 32 == 0 && (b + 1) < s->length) {
                            printf("\n    ");
                        } else if ((b + 1) % 4 == 0 && (b + 1) < s->length) {
                            printf(" ");
                        }
                    }
                    printf("\n");
                }
            }
            printf("\n");
        }
        printf("Result: valid; hardware unchanged\n");
    }
}

static void print_help(const char *prog_name) {
    printf("\nVerwendung: %s [Befehl] [Optionen]\n\n", prog_name);
    printf("Verfügbare Befehle:\n");
    printf("  status                              Zeigt aktuellen Beleuchtungsstatus an (Standard)\n");
    printf("  fill <#RRGGBB> [-b 0..5]            Färbt alle Tasten in einer Farbe (Standardhelligkeit: 5)\n");
    printf("  load <Datei.json> [Name|Idx] [-b..] Wendet ein JSON-Beleuchtungsprofil auf die Tastatur an\n");
    printf("  wasd                                Aktiviert WASD-Gaming-Profil (WASD Rot, Basis Blau)\n");
    printf("  wasd-arrows                         Kombiniertes Profil (WASD Rot, Pfeile Gelb, Basis Blau)\n");
    printf("  all-red                             Aktiviert statisches Vollrot für alle Tasten\n");
    printf("  list-modes                          Listet alle 20 Modi und 11 Farbpaletten auf\n");
    printf("  set <Modus> [F] [T] [H]             Setzt Modus (0..20), Farbe (0..10), Tempo (0..4), Helligkeit (0..5)\n");
    printf("  export-layout                       Exportiert Tastenmatrix und Report 0x0B Offsets (JSON oder Tabelle)\n");
    printf("  validate-profile <Datei.json>       Validiert eine JSON-Profildatei ohne Hardwarezugriff\n");
    printf("  help, --help, -h                    Zeigt diese Hilfe an\n\n");
    printf("Optionen:\n");
    printf("  -b, --brightness <0..5>             Helligkeit für Befehle wie fill und load (Standard: 5)\n");
    printf("  --dry-run                           Simuliert Übertragung ohne Öffnen von USB-Geräten (für alle Schreibbefehle)\n");
    printf("  --dump-bytes                        Gibt Roh-Wire-Bytes (Hex) der generierten USB-Reports aus\n");
    printf("  --json                              Ausgabe im maschinenlesbaren JSON-Format\n\n");
}

int main(int argc, char **argv) {
    bool json_mode = false;
    bool dry_run = false;
    bool dump_bytes = false;
    const char *cmd = NULL;

    // 1. Zuerst prüfen, ob Hilfe angefordert wurde oder Optionen gesetzt sind
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 || (i == 1 && strcmp(argv[i], "help") == 0)) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        }
        if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        }
        if (strcmp(argv[i], "--dump-bytes") == 0) {
            dump_bytes = true;
        }
    }

    // 2. Optionale Helligkeits-Option (-b / --brightness) extrahieren
    uint8_t flag_brightness = CIY_MAX_BRIGHTNESS;
    bool has_flag_brightness = false;
    const char *bright_err = NULL;
    if (!extract_brightness_option(&argc, argv, &flag_brightness, &has_flag_brightness, &bright_err)) {
        if (json_mode) {
            printf("{\n  \"connected\": false,\n  \"error\": \"Ungültige Helligkeit '");
            print_escaped_json_string(stdout, bright_err);
            printf("' (erlaubt: 0..5)\"\n}\n");
        } else {
            fprintf(stderr, "Fehler: Ungültige Helligkeit '%s'. Erlaubter Bereich: 0..5.\n", bright_err ? bright_err : "");
        }
        return 1;
    }

    // 3. Befehl identifizieren (erstes Argument, das kein Flag ist)
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            cmd = argv[i];
            break;
        }
    }
    if (!cmd) {
        cmd = "status";
    }

    // 4. Auf unbekannte Optionen prüfen
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--json") != 0 && strcmp(argv[i], "--dry-run") != 0 && strcmp(argv[i], "--dump-bytes") != 0) {
                // Wenn es eine Zahl ist (z. B. negatives Argument bei set -1), übernimmt der Befehl die Prüfung
                if (argv[i][1] >= '0' && argv[i][1] <= '9') {
                    continue;
                }
                if (json_mode) {
                    printf("{\n  \"connected\": false,\n  \"error\": \"Unbekannte Option '");
                    print_escaped_json_string(stdout, argv[i]);
                    printf("'\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: Unbekannte Option '%s'.\n", argv[i]);
                    fprintf(stderr, "Rufen Sie '%s --help' für weitere Informationen auf.\n", argv[0]);
                }
                return 1;
            }
        }
    }

    if (dry_run && strcmp(cmd, "load") != 0 && strcmp(cmd, "fill") != 0 &&
        strcmp(cmd, "wasd") != 0 && strcmp(cmd, "wasd-arrows") != 0 &&
        strcmp(cmd, "all-red") != 0 && strcmp(cmd, "set") != 0) {
        if (json_mode) {
            printf("{\n  \"success\": false,\n  \"error\": \"--dry-run wird derzeit nur für den Befehl 'load' unterstützt\"\n}\n");
        } else {
            fprintf(stderr, "Fehler: --dry-run wird derzeit nur für den Befehl 'load' unterstützt.\n");
        }
        return 1;
    }

    if (strcmp(cmd, "list-modes") == 0) {
        if (json_mode) {
            print_modes_json();
        } else {
            print_modes_text();
        }
        return 0;
    }

    if (strcmp(cmd, "export-layout") == 0) {
        const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
        if (json_mode) {
            print_export_layout_json(layout);
        } else {
            print_export_layout_text(layout);
        }
        return 0;
    }

    if (strcmp(cmd, "validate-profile") == 0) {
        const char *file_arg = NULL;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "validate-profile") == 0) continue;
            if (argv[i][0] == '-') continue;
            if (!file_arg) {
                file_arg = argv[i];
            }
        }
        if (!file_arg) {
            if (json_mode) {
                printf("{\n  \"valid\": false,\n  \"error\": \"Fehlende Datei. Verwendung: ciy_ctl validate-profile <profile.json> [--json]\"\n}\n");
            } else {
                fprintf(stderr, "Verwendung: %s validate-profile <profile.json> [--json]\n", argv[0]);
            }
            return 1;
        }

        CIYValidationResult res;
        bool ok = ciy_validate_profile_file(file_arg, &res);
        if (ok) {
            if (json_mode) {
                printf("{\n  \"valid\": true,\n  \"profiles\": %d,\n  \"keys\": %d,\n  \"file\": \"",
                       res.profile_count, res.total_keys_count);
                print_escaped_json_string(stdout, file_arg);
                printf("\"\n}\n");
            } else {
                printf("Profil '%s' ist gültig (%d Profil(e), %d Tasten-Slot(s) definiert).\n",
                       file_arg, res.profile_count, res.total_keys_count);
            }
            return 0;
        } else {
            if (json_mode) {
                printf("{\n  \"valid\": false,\n  \"error\": \"");
                print_escaped_json_string(stdout, res.error_message);
                printf("\",\n  \"field\": \"");
                print_escaped_json_string(stdout, res.error_field);
                printf("\",\n  \"file\": \"");
                print_escaped_json_string(stdout, file_arg);
                printf("\"\n}\n");
            } else {
                if (res.error_field[0] != '\0') {
                    fprintf(stderr, "Validierungsfehler in '%s' (Feld '%s'): %s\n", file_arg, res.error_field, res.error_message);
                } else {
                    fprintf(stderr, "Validierungsfehler in '%s': %s\n", file_arg, res.error_message);
                }
            }
            return 1;
        }
    }

    if (dry_run) {
        if (strcmp(cmd, "load") == 0) {
            const char *file_arg = NULL;
            const char *target_profile = NULL;
            uint8_t brightness = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;

            const char *extra_args[2] = {NULL, NULL};
            int num_extras = 0;

            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "load") == 0 || strcmp(argv[i], "--json") == 0 ||
                    strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
                if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) continue;
                if (!file_arg) {
                    file_arg = argv[i];
                } else if (num_extras < 2) {
                    extra_args[num_extras++] = argv[i];
                }
            }

            if (!file_arg) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Datei. Verwendung: ciy_ctl --dry-run load <profile.json> [Profilname|Index] [-b 0..5]\"\n}\n");
                } else {
                    fprintf(stderr, "Verwendung: %s --dry-run load <profile.json> [Profilname|Index] [-b 0..5]\n", argv[0]);
                }
                return 1;
            }

            if (num_extras == 1) {
                target_profile = extra_args[0];
            } else if (num_extras >= 2) {
                target_profile = extra_args[0];
                if (!has_flag_brightness && !parse_range(extra_args[1], 0, CIY_MAX_BRIGHTNESS, &brightness)) {
                    if (json_mode) {
                        printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Helligkeit '");
                        print_escaped_json_string(stdout, extra_args[1]);
                        printf("' (erlaubt: 0..5)\"\n}\n");
                    } else {
                        fprintf(stderr, "Fehler: Ungültige Helligkeit '%s'. Erlaubter Bereich: 0..5.\n", extra_args[1]);
                    }
                    return 1;
                }
            }

            // Strikte Profilvalidierung ZUERST
            CIYValidationResult val_res;
            if (!ciy_validate_profile_file(file_arg, &val_res)) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"dry_run\": true,\n  \"hardware_opened\": false,\n  \"error\": \"");
                    print_escaped_json_string(stdout, val_res.error_message);
                    printf("\",\n  \"field\": \"");
                    print_escaped_json_string(stdout, val_res.error_field);
                    printf("\"\n}\n");
                } else {
                    if (val_res.error_field[0] != '\0') {
                        fprintf(stderr, "Fehler: Ungültiges Profil in '%s' (Feld '%s'): %s\n", file_arg, val_res.error_field, val_res.error_message);
                    } else {
                        fprintf(stderr, "Fehler: Ungültiges Profil in '%s': %s\n", file_arg, val_res.error_message);
                    }
                }
                return 1;
            }

            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            char profile_name[64] = {0};
            int slots_count = 0;
            if (!ciy_load_profile_file(file_arg, target_profile, rgb, profile_name, sizeof(profile_name), &slots_count)) {
                if (json_mode) {
                    if (target_profile) {
                        printf("{\n  \"success\": false,\n  \"dry_run\": true,\n  \"hardware_opened\": false,\n  \"error\": \"Profil '");
                        print_escaped_json_string(stdout, target_profile);
                        printf("' konnte in '");
                        print_escaped_json_string(stdout, file_arg);
                        printf("' nicht gefunden werden\"\n}\n");
                    } else {
                        printf("{\n  \"success\": false,\n  \"dry_run\": true,\n  \"hardware_opened\": false,\n  \"error\": \"Profil aus '");
                        print_escaped_json_string(stdout, file_arg);
                        printf("' konnte nicht geladen werden\"\n}\n");
                    }
                } else {
                    if (target_profile) {
                        fprintf(stderr, "Fehler: Profil '%s' konnte in '%s' nicht gefunden werden.\n", target_profile, file_arg);
                    } else {
                        fprintf(stderr, "Fehler: Profil aus '%s' konnte nicht geladen werden.\n", file_arg);
                    }
                }
                return 1;
            }

            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), brightness, profile_name, slots_count)) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"dry_run\": true,\n  \"hardware_opened\": false,\n  \"error\": \"Fehler beim Erstellen des Übertragungsplans\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler beim Erstellen des Übertragungsplans.\n");
                }
                return 1;
            }

            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "fill") == 0) {
            const char *color_arg = NULL;
            const char *bright_arg = NULL;
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "fill") == 0 || strcmp(argv[i], "--json") == 0 ||
                    strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
                if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) continue;
                if (!color_arg) color_arg = argv[i];
                else if (!bright_arg) bright_arg = argv[i];
            }
            if (!color_arg) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Hex-Farbe. Verwendung: ciy_ctl fill <#RRGGBB> [-b 0..5]\"\n}\n");
                } else {
                    fprintf(stderr, "Verwendung: %s fill <#RRGGBB> [-b 0..5]\n", argv[0]);
                }
                return 1;
            }
            uint8_t r = 0, g = 0, b = 0;
            if (!ciy_parse_hex_color(color_arg, &r, &g, &b)) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Hex-Farbe '");
                    print_escaped_json_string(stdout, color_arg);
                    printf("' (erwartet #RRGGBB oder RRGGBB)\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: Ungültige Hex-Farbe '%s'. Erwartet: #RRGGBB oder RRGGBB (z. B. #FF5500).\n", color_arg);
                }
                return 1;
            }
            uint8_t brightness = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            if (!has_flag_brightness && bright_arg && !parse_range(bright_arg, 0, CIY_MAX_BRIGHTNESS, &brightness)) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Helligkeit '");
                    print_escaped_json_string(stdout, bright_arg);
                    printf("' (erlaubt: 0..5)\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: Ungültige Helligkeit '%s'. Erlaubter Bereich: 0..5.\n", bright_arg);
                }
                return 1;
            }
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_build_solid_color_profile(rgb, r, g, b);
            char prof_name[64];
            snprintf(prof_name, sizeof(prof_name), "Vollfarbe #%02X%02X%02X", r, g, b);
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), brightness, prof_name, 69)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "wasd") == 0) {
            const int wasd_idx[] = {15, 10, 16, 22};
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_build_profile(rgb, wasd_idx, 4, true, false, true);
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), 5, "WASD Gaming (Rot / Basis Blau)", 4)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "wasd-arrows") == 0) {
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_build_wasd_and_arrows_profile(rgb, true);
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), 5, "WASD + Pfeiltasten (Rot & Gelb / Basis Blau)", 8)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "all-red") == 0) {
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_build_all_red_profile(rgb);
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), 5, "Alles Rot", 69)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "set") == 0) {
            const char *set_args[4] = {NULL, NULL, NULL, NULL};
            int num_set_args = 0;
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "set") == 0 || strcmp(argv[i], "--json") == 0 ||
                    strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
                if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) continue;
                if (num_set_args < 4) {
                    set_args[num_set_args++] = argv[i];
                }
            }
            if (num_set_args < 1) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Argumente. Verwendung: ciy_ctl set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\"\n}\n");
                } else {
                    fprintf(stderr, "Verwendung: %s set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\n", argv[0]);
                }
                return 1;
            }
            CIYLightingState st = {
                .mode = 0,
                .color = 0,
                .speed = 2,
                .brightness = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS
            };
            if (!parse_range(set_args[0], 0, CIY_MAX_MODE, &st.mode) ||
                (num_set_args > 1 && !parse_range(set_args[1], 0, CIY_MAX_COLOR, &st.color)) ||
                (num_set_args > 2 && !parse_range(set_args[2], 0, CIY_MAX_SPEED, &st.speed)) ||
                (num_set_args > 3 && !parse_range(set_args[3], 0, CIY_MAX_BRIGHTNESS, &st.brightness))) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\"\n}\n");
                } else {
                    fprintf(stderr, "Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\n");
                }
                return 1;
            }
            CIYTransferPlan plan;
            if (!ciy_build_state_transfer_plan(&plan, st)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        }
    }

    CIYDevice dev;
    if (!ciy_open(&dev)) {
        if (json_mode) {
            printf("{\n  \"connected\": false,\n  \"error\": \"CIY X79 Tastatur (VID: 0x%04X, PID: 0x%04X) nicht gefunden\"\n}\n",
                   CIY_VENDOR_ID, CIY_PRODUCT_ID);
        } else {
            fprintf(stderr, "Fehler: CIY X79 Tastatur (VID: 0x%04X, PID: 0x%04X) nicht gefunden.\n",
                    CIY_VENDOR_ID, CIY_PRODUCT_ID);
        }
        return 1;
    }

    int rc = 0;

    if (strcmp(cmd, "status") == 0) {
        if (json_mode) {
            if (!print_status_json(&dev)) rc = 1;
        } else {
            if (!print_status(&dev)) rc = 1;
        }
    } else if (strcmp(cmd, "fill") == 0) {
        const char *color_arg = NULL;
        const char *bright_arg = NULL;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "fill") == 0 || strcmp(argv[i], "--json") == 0) continue;
            if (!color_arg) color_arg = argv[i];
            else if (!bright_arg) bright_arg = argv[i];
        }

        if (!color_arg) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Hex-Farbe. Verwendung: ciy_ctl fill <#RRGGBB> [-b 0..5]\"\n}\n");
            } else {
                fprintf(stderr, "Verwendung: %s fill <#RRGGBB> [-b 0..5]\n", argv[0]);
            }
            rc = 1;
        } else {
            uint8_t r = 0, g = 0, b = 0;
            if (!ciy_parse_hex_color(color_arg, &r, &g, &b)) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Hex-Farbe '");
                    print_escaped_json_string(stdout, color_arg);
                    printf("' (erwartet #RRGGBB oder RRGGBB)\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: Ungültige Hex-Farbe '%s'. Erwartet: #RRGGBB oder RRGGBB (z. B. #FF5500).\n", color_arg);
                }
                rc = 1;
            } else {
                uint8_t brightness = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
                if (!has_flag_brightness && bright_arg && !parse_range(bright_arg, 0, CIY_MAX_BRIGHTNESS, &brightness)) {
                    if (json_mode) {
                        printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Helligkeit '");
                        print_escaped_json_string(stdout, bright_arg);
                        printf("' (erlaubt: 0..5)\"\n}\n");
                    } else {
                        fprintf(stderr, "Fehler: Ungültige Helligkeit '%s'. Erlaubter Bereich: 0..5.\n", bright_arg);
                    }
                    rc = 1;
                } else {
                    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
                    ciy_build_solid_color_profile(rgb, r, g, b);
                    if (!json_mode) {
                        printf("Sende Vollfarbton #%02X%02X%02X (Helligkeit %u/5) an Tastatur...\n", r, g, b, brightness);
                    }
                    if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), brightness)) {
                        if (json_mode) {
                            printf("{\n  \"success\": true,\n  \"action\": \"fill\",\n  \"color\": \"#%02X%02X%02X\",\n  \"brightness\": %u\n}\n",
                                   r, g, b, brightness);
                        } else {
                            printf("Farbe #%02X%02X%02X erfolgreich auf alle Tasten angewendet!\n", r, g, b);
                        }
                    } else {
                        if (json_mode) {
                            printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Übertragen der Farbe an die Tastatur\"\n}\n");
                        } else {
                            fprintf(stderr, "Fehler beim Übertragen der Farbe an die Tastatur.\n");
                        }
                        rc = 1;
                    }
                }
            }
        }
    } else if (strcmp(cmd, "load") == 0) {
        const char *file_arg = NULL;
        const char *target_profile = NULL;
        uint8_t brightness = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;

        const char *extra_args[2] = {NULL, NULL};
        int num_extras = 0;

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "load") == 0 || strcmp(argv[i], "--json") == 0) continue;
            if (argv[i][0] == '-') continue;
            if (!file_arg) {
                file_arg = argv[i];
            } else if (num_extras < 2) {
                extra_args[num_extras++] = argv[i];
            }
        }

        if (!file_arg) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Datei. Verwendung: ciy_ctl load <profile.json> [Profilname|Index] [-b 0..5]\"\n}\n");
            } else {
                fprintf(stderr, "Verwendung: %s load <profile.json> [Profilname|Index] [-b 0..5]\n", argv[0]);
            }
            rc = 1;
        } else {
            bool bright_error = false;
            if (num_extras == 1) {
                // Ein einzelnes Extra-Argument ist immer der Profilselektor (Name oder 1-basierter Index)
                target_profile = extra_args[0];
            } else if (num_extras >= 2) {
                target_profile = extra_args[0];
                if (!has_flag_brightness && !parse_range(extra_args[1], 0, CIY_MAX_BRIGHTNESS, &brightness)) {
                    bright_error = true;
                }
            }

            if (bright_error) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Helligkeit '");
                    print_escaped_json_string(stdout, extra_args[1]);
                    printf("' (erlaubt: 0..5)\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: Ungültige Helligkeit '%s'. Erlaubter Bereich: 0..5.\n", extra_args[1]);
                }
                rc = 1;
            } else {
                CIYValidationResult val_res;
                if (!ciy_validate_profile_file(file_arg, &val_res)) {
                    if (json_mode) {
                        printf("{\n  \"success\": false,\n  \"error\": \"");
                        print_escaped_json_string(stdout, val_res.error_message);
                        printf("\",\n  \"field\": \"");
                        print_escaped_json_string(stdout, val_res.error_field);
                        printf("\"\n}\n");
                    } else {
                        if (val_res.error_field[0] != '\0') {
                            fprintf(stderr, "Fehler: Ungültiges Profil in '%s' (Feld '%s'): %s\n", file_arg, val_res.error_field, val_res.error_message);
                        } else {
                            fprintf(stderr, "Fehler: Ungültiges Profil in '%s': %s\n", file_arg, val_res.error_message);
                        }
                    }
                    rc = 1;
                } else {
                    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
                    char profile_name[64] = {0};
                    int slots_count = 0;
                    if (!ciy_load_profile_file(file_arg, target_profile, rgb, profile_name, sizeof(profile_name), &slots_count)) {
                        if (json_mode) {
                            if (target_profile) {
                                printf("{\n  \"success\": false,\n  \"error\": \"Profil '");
                                print_escaped_json_string(stdout, target_profile);
                                printf("' konnte in '");
                                print_escaped_json_string(stdout, file_arg);
                                printf("' nicht gefunden werden\"\n}\n");
                            } else {
                                printf("{\n  \"success\": false,\n  \"error\": \"Profil aus '");
                                print_escaped_json_string(stdout, file_arg);
                                printf("' konnte nicht geladen werden\"\n}\n");
                            }
                        } else {
                            if (target_profile) {
                                fprintf(stderr, "Fehler: Profil '%s' konnte in '%s' nicht gefunden werden.\n", target_profile, file_arg);
                            } else {
                                fprintf(stderr, "Fehler: Profil aus '%s' konnte nicht geladen werden (Datei fehlt, zu groß oder ungültiges Format).\n", file_arg);
                            }
                        }
                        rc = 1;
                    } else {
                        if (!json_mode) {
                            if (profile_name[0] != '\0') {
                                printf("Wende Profil „%s“ (%d Tasten, Helligkeit %u/5) aus '%s' an...\n",
                                       profile_name, slots_count, brightness, file_arg);
                            } else {
                                printf("Wende Profil (%d Tasten, Helligkeit %u/5) aus '%s' an...\n",
                                       slots_count, brightness, file_arg);
                            }
                        }
                        if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), brightness)) {
                            if (json_mode) {
                                printf("{\n  \"success\": true,\n  \"action\": \"load\",\n  \"file\": \"");
                                print_escaped_json_string(stdout, file_arg);
                                printf("\",\n  \"name\": \"");
                                print_escaped_json_string(stdout, profile_name);
                                printf("\",\n  \"keys\": %d,\n  \"brightness\": %u\n}\n",
                                       slots_count, brightness);
                            } else {
                                printf("Profil erfolgreich auf Tastatur übertragen!\n");
                            }
                        } else {
                            if (json_mode) {
                                printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Übertragen des Profils an die Tastatur\"\n}\n");
                            } else {
                                fprintf(stderr, "Fehler beim Übertragen des Profils an die Tastatur.\n");
                            }
                            rc = 1;
                        }
                    }
                }
            }
        }
    } else if (strcmp(cmd, "wasd") == 0) {
        if (!json_mode) {
            printf("Aktiviere WASD Gaming-Profil (W, A, S, D in ROT, Basis in BLAU)...\n");
        }
        const int wasd_idx[] = {15, 10, 16, 22};
        uint8_t rgb[CIY_PLANAR_RGB_SIZE];
        ciy_build_profile(rgb, wasd_idx, 4, true, false, true);

        if (ciy_apply_custom_profile(&dev, rgb, sizeof(rgb))) {
            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"wasd\",\n  \"name\": \"WASD Gaming (Rot / Basis Blau)\",\n  \"brightness\": 5\n}\n");
            } else {
                printf("WASD Gaming-Profil erfolgreich angewendet!\n");
            }
        } else {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Anwenden des WASD-Profils\"\n}\n");
            } else {
                fprintf(stderr, "Fehler beim Anwenden des WASD-Profils.\n");
            }
            rc = 1;
        }
    } else if (strcmp(cmd, "wasd-arrows") == 0) {
        if (!json_mode) {
            printf("Aktiviere kombiniertes Profil (WASD in ROT, Pfeiltasten in GELB, Basis in BLAU)...\n");
        }
        uint8_t rgb[CIY_PLANAR_RGB_SIZE];
        ciy_build_wasd_and_arrows_profile(rgb, true);
        if (ciy_apply_custom_profile(&dev, rgb, sizeof(rgb))) {
            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"wasd-arrows\",\n  \"name\": \"WASD + Pfeiltasten (Rot & Gelb / Basis Blau)\",\n  \"brightness\": 5\n}\n");
            } else {
                printf("Kombiniertes Profil erfolgreich angewendet!\n");
            }
        } else {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Anwenden des Profils\"\n}\n");
            } else {
                fprintf(stderr, "Fehler beim Anwenden des Profils.\n");
            }
            rc = 1;
        }
    } else if (strcmp(cmd, "all-red") == 0) {
        if (!json_mode) {
            printf("Aktiviere Profil: Alles Rot (Vollflächig statisches Rot)...\n");
        }
        uint8_t rgb[CIY_PLANAR_RGB_SIZE];
        ciy_build_all_red_profile(rgb);
        if (ciy_apply_custom_profile(&dev, rgb, sizeof(rgb))) {
            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"all-red\",\n  \"name\": \"Alles Rot\",\n  \"brightness\": 5\n}\n");
            } else {
                printf("Profil 'Alles Rot' erfolgreich angewendet!\n");
            }
        } else {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Anwenden des Profils\"\n}\n");
            } else {
                fprintf(stderr, "Fehler beim Anwenden des Profils.\n");
            }
            rc = 1;
        }
    } else if (strcmp(cmd, "set") == 0) {
        const char *set_args[4] = {NULL, NULL, NULL, NULL};
        int num_set_args = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "set") == 0 || strcmp(argv[i], "--json") == 0 ||
                strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
            if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) continue;
            if (num_set_args < 4) {
                set_args[num_set_args++] = argv[i];
            }
        }

        if (num_set_args < 1) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Argumente. Verwendung: ciy_ctl set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\"\n}\n");
            } else {
                fprintf(stderr, "Verwendung: %s set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\n", argv[0]);
            }
            rc = 1;
        } else {
            CIYLightingState current = {19, 0, 1, 5};
            if (num_set_args < 4 && !ciy_get_state(&dev, &current)) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Auslesen des aktuellen Status\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler beim Auslesen des aktuellen Status.\n");
                }
                rc = 1;
            } else {
                CIYLightingState st = current;
                if (!parse_range(set_args[0], 0, CIY_MAX_MODE, &st.mode) ||
                    (num_set_args > 1 && !parse_range(set_args[1], 0, CIY_MAX_COLOR, &st.color)) ||
                    (num_set_args > 2 && !parse_range(set_args[2], 0, CIY_MAX_SPEED, &st.speed)) ||
                    (num_set_args > 3 && !parse_range(set_args[3], 0, CIY_MAX_BRIGHTNESS, &st.brightness))) {
                    if (json_mode) {
                        printf("{\n  \"success\": false,\n  \"error\": \"Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\"\n}\n");
                    } else {
                        fprintf(stderr, "Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\n");
                    }
                    rc = 1;
                } else if (ciy_set_state(&dev, st)) {
                    if (json_mode) {
                        printf("{\n  \"success\": true,\n  \"action\": \"set\",\n  \"mode\": %u,\n  \"color\": %u,\n  \"speed\": %u,\n  \"brightness\": %u\n}\n",
                               st.mode, st.color, st.speed, st.brightness);
                    } else {
                        printf("Modus %u, Farbe %u, Tempo %u, Helligkeit %u gesetzt.\n",
                               st.mode, st.color, st.speed, st.brightness);
                    }
                } else {
                    if (json_mode) {
                        printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Senden des Befehls an die Tastatur\"\n}\n");
                    } else {
                        fprintf(stderr, "Fehler beim Senden des Befehls an die Tastatur.\n");
                    }
                    rc = 1;
                }
            }
        }
    } else {
        if (json_mode) {
            printf("{\n  \"success\": false,\n  \"error\": \"Unbekannter Befehl '");
            print_escaped_json_string(stdout, cmd);
            printf("'\"\n}\n");
        } else {
            fprintf(stderr, "Unbekannter Befehl: %s\n", cmd);
            fprintf(stderr, "Verfügbare Befehle: status, fill, load, wasd, wasd-arrows, all-red, list-modes, set, export-layout, validate-profile\n");
            fprintf(stderr, "Rufen Sie '%s --help' für weitere Informationen auf.\n", argv[0]);
        }
        rc = 1;
    }

    ciy_close(&dev);
    return rc;
}
