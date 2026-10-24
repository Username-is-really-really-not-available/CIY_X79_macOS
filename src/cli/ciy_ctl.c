#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include "../core/ciy_hid.h"

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

static void print_status(CIYDevice *dev) {
    CIYLightingState st;
    if (!ciy_get_state(dev, &st)) {
        fprintf(stderr, "Fehler beim Auslesen des Tastaturstatus.\n");
        return;
    }

    const char *m_name = (st.mode <= CIY_MAX_MODE) ? EFFECT_NAMES[st.mode] : "Unbekannt";
    const char *c_name = (st.color <= CIY_MAX_COLOR) ? COLOR_NAMES[st.color] : "Unbekannt";

    printf("\n=== CIY X79 Tastatur-Status ===\n");
    printf("  Modus:       [%2u] %s\n", st.mode, m_name);
    printf("  Farbe:       [%2u] %s\n", st.color, c_name);
    printf("  Helligkeit:  %u / 5\n", st.brightness);
    printf("  Tempo:       %u / 4\n", st.speed);
    printf("================================\n\n");
}

static void print_status_json(CIYDevice *dev) {
    CIYLightingState st;
    if (!dev || !dev->dev || !ciy_get_state(dev, &st)) {
        printf("{\n  \"connected\": false,\n  \"error\": \"Fehler beim Auslesen des Tastaturstatus\"\n}\n");
        return;
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
    printf("  help, --help, -h                    Zeigt diese Hilfe an\n\n");
    printf("Optionen:\n");
    printf("  -b, --brightness <0..5>             Helligkeit für Befehle wie fill und load (Standard: 5)\n");
    printf("  --json                              Ausgabe im maschinenlesbaren JSON-Format\n\n");
}

int main(int argc, char **argv) {
    bool json_mode = false;
    const char *cmd = NULL;

    // 1. Zuerst prüfen, ob Hilfe angefordert wurde oder --json gesetzt ist
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 || (i == 1 && strcmp(argv[i], "help") == 0)) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--json") == 0) {
            json_mode = true;
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
            if (strcmp(argv[i], "--json") != 0) {
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

    if (strcmp(cmd, "list-modes") == 0) {
        if (json_mode) {
            print_modes_json();
        } else {
            print_modes_text();
        }
        return 0;
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
            print_status_json(&dev);
        } else {
            print_status(&dev);
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
            if (strcmp(argv[i], "set") == 0 || strcmp(argv[i], "--json") == 0) continue;
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
            CIYLightingState cur = {19, 0, 1, 5};
            ciy_get_state(&dev, &cur);

            CIYLightingState st = cur;
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
                    usleep(100000);
                    print_status_json(&dev);
                } else {
                    printf("Einstellung erfolgreich angewendet!\n");
                    usleep(100000);
                    print_status(&dev);
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
    } else {
        if (json_mode) {
            printf("{\n  \"success\": false,\n  \"error\": \"Unbekannter Befehl '");
            print_escaped_json_string(stdout, cmd);
            printf("'\"\n}\n");
        } else {
            fprintf(stderr, "Unbekannter Befehl: %s\n", cmd);
            fprintf(stderr, "Verfügbare Befehle: status, fill, load, wasd, wasd-arrows, all-red, list-modes, set\n");
            fprintf(stderr, "Rufen Sie '%s --help' für weitere Informationen auf.\n", argv[0]);
        }
        rc = 1;
    }

    ciy_close(&dev);
    return rc;
}
