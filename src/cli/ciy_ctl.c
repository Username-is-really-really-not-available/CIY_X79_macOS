#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include "../core/ciy_hid.h"
#include "../layouts/ciy_layout.h"
#include "../core/ciy_battery.h"
#include "../core/ciy_gradient.h"

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

static bool parse_int_range(const char *str, long min, long max, int *out) {
    if (!str) return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(str, &end, 10);
    if (errno != 0 || end == str || *end != '\0' || v < min || v > max) return false;
    *out = (int)v;
    return true;
}

static volatile sig_atomic_t s_sweep_interrupted = 0;
static struct termios s_orig_termios;
static volatile sig_atomic_t s_termios_saved = 0;

static void sweep_sigint_handler(int sig) {
    (void)sig;
    s_sweep_interrupted = 1;
    if (s_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_termios);
    }
}

static volatile sig_atomic_t s_watch_interrupted = 0;

static void watch_sigint_handler(int sig) {
    (void)sig;
    s_watch_interrupted = 1;
}

static CIYBatterySourceType parse_battery_source_type(const char *source_str) {
    if (!source_str) return CIY_BATTERY_SOURCE_INTERNAL;
    if (strcasecmp(source_str, "internal") == 0 || strcasecmp(source_str, "laptop") == 0 || strcasecmp(source_str, "macbook") == 0) {
        return CIY_BATTERY_SOURCE_INTERNAL;
    }
    if (strcasecmp(source_str, "peripheral") == 0 || strcasecmp(source_str, "mouse") == 0 || strcasecmp(source_str, "trackpad") == 0) {
        return CIY_BATTERY_SOURCE_PERIPHERAL;
    }
    if (strcasecmp(source_str, "ups") == 0) {
        return CIY_BATTERY_SOURCE_UPS;
    }
    if (strcasecmp(source_str, "all") == 0) {
        return CIY_BATTERY_SOURCE_ALL;
    }
    return CIY_BATTERY_SOURCE_INTERNAL;
}

static bool parse_battery_options(int argc, char **argv,
                                  bool *out_apply,
                                  bool *out_watch,
                                  bool *out_list_sources,
                                  bool *out_segmented,
                                  const char **out_target_str,
                                  const char **out_source_str,
                                  char *err_msg, size_t err_size) {
    *out_apply = false;
    *out_watch = false;
    *out_list_sources = false;
    *out_segmented = false;
    *out_target_str = "1-0";
    *out_source_str = "internal";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "battery") == 0 || strcmp(argv[i], "--json") == 0 ||
            strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
        if (strcmp(argv[i], "--apply") == 0) {
            *out_apply = true;
        } else if (strcmp(argv[i], "--watch") == 0) {
            *out_watch = true;
        } else if (strcmp(argv[i], "--list-sources") == 0) {
            *out_list_sources = true;
        } else if (strcmp(argv[i], "--segmented") == 0) {
            *out_segmented = true;
        } else if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                snprintf(err_msg, err_size, "Fehlendes Argument für --target (z. B. 1-0, space, nav)");
                return false;
            }
            *out_target_str = argv[++i];
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            *out_target_str = argv[i] + 9;
        } else if (strcmp(argv[i], "--source") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                snprintf(err_msg, err_size, "Fehlendes Argument für --source (z. B. internal, peripheral, all)");
                return false;
            }
            *out_source_str = argv[++i];
        } else if (strncmp(argv[i], "--source=", 9) == 0) {
            *out_source_str = argv[i] + 9;
        }
    }
    return true;
}

static bool parse_gradient_options(int argc, char **argv,
                                   const char **out_col1,
                                   const char **out_col2,
                                   CIYGradientDirection *out_dir,
                                   char *err_msg, size_t err_size) {
    *out_col1 = NULL;
    *out_col2 = NULL;
    *out_dir = CIY_GRADIENT_HORIZONTAL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "gradient") == 0 || strcmp(argv[i], "--json") == 0 ||
            strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
        if (strcmp(argv[i], "--dir") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                snprintf(err_msg, err_size, "Fehlendes Argument für --dir (horizontal oder vertical)");
                return false;
            }
            const char *d = argv[++i];
            if (strcasecmp(d, "vertical") == 0 || strcasecmp(d, "vert") == 0 || strcasecmp(d, "v") == 0) {
                *out_dir = CIY_GRADIENT_VERTICAL;
            } else if (strcasecmp(d, "horizontal") == 0 || strcasecmp(d, "horiz") == 0 || strcasecmp(d, "h") == 0) {
                *out_dir = CIY_GRADIENT_HORIZONTAL;
            } else {
                snprintf(err_msg, err_size, "Ungültige Richtung '%s' (erlaubt: horizontal, vertical)", d);
                return false;
            }
        } else if (strncmp(argv[i], "--dir=", 6) == 0) {
            const char *d = argv[i] + 6;
            if (strcasecmp(d, "vertical") == 0 || strcasecmp(d, "vert") == 0 || strcasecmp(d, "v") == 0) {
                *out_dir = CIY_GRADIENT_VERTICAL;
            } else if (strcasecmp(d, "horizontal") == 0 || strcasecmp(d, "horiz") == 0 || strcasecmp(d, "h") == 0) {
                *out_dir = CIY_GRADIENT_HORIZONTAL;
            } else {
                snprintf(err_msg, err_size, "Ungültige Richtung '%s' (erlaubt: horizontal, vertical)", d);
                return false;
            }
        } else if (argv[i][0] != '-') {
            if (!*out_col1) *out_col1 = argv[i];
            else if (!*out_col2) *out_col2 = argv[i];
        }
    }

    if (!*out_col1 || !*out_col2) {
        snprintf(err_msg, err_size, "Fehlende Hex-Farben. Verwendung: ciy_ctl gradient <#Start> <#End> [--dir horizontal|vertical] [-b 0..5]");
        return false;
    }
    return true;
}

static void print_battery_sources_json(const CIYBatteryInfo *sources, size_t count) {
    printf("{\n  \"success\": true,\n  \"sources\": [\n");
    for (size_t i = 0; i < count; i++) {
        const char *t_str = "internal";
        if (sources[i].type == CIY_BATTERY_SOURCE_PERIPHERAL) t_str = "peripheral";
        else if (sources[i].type == CIY_BATTERY_SOURCE_UPS) t_str = "ups";

        printf("    {\n      \"name\": \"");
        print_escaped_json_string(stdout, sources[i].name);
        printf("\",\n      \"type\": \"%s\",\n      \"percent\": %d,\n      \"charging\": %s,\n      \"present\": %s\n    }%s\n",
               t_str, sources[i].percent,
               sources[i].is_charging ? "true" : "false",
               sources[i].is_present ? "true" : "false",
               (i + 1 < count) ? "," : "");
    }
    printf("  ]\n}\n");
}

static void print_battery_sources_text(const CIYBatteryInfo *sources, size_t count) {
    printf("Verfügbare Stromquellen (%zu):\n", count);
    for (size_t i = 0; i < count; i++) {
        const char *t_str = "intern";
        if (sources[i].type == CIY_BATTERY_SOURCE_PERIPHERAL) t_str = "Peripherie / Bluetooth";
        else if (sources[i].type == CIY_BATTERY_SOURCE_UPS) t_str = "USV (Notstrom)";

        printf("  [%zu] Name: %s (%s)\n", i + 1, sources[i].name, t_str);
        if (sources[i].percent >= 0) {
            printf("      Ladestand: %d%% (%s)\n", sources[i].percent,
                   sources[i].is_charging ? "Laden / Netzteil" : "Akku-Betrieb");
        } else {
            printf("      Ladestand: Unbekannt\n");
        }
    }
}

static void print_battery_status_json(const CIYBatteryInfo *info, const char *target_str,
                                      const int *slots, size_t count, size_t active_count) {
    const char *t_str = "internal";
    if (info->type == CIY_BATTERY_SOURCE_PERIPHERAL) t_str = "peripheral";
    else if (info->type == CIY_BATTERY_SOURCE_UPS) t_str = "ups";

    uint8_t r = 0, g = 0, b = 0;
    ciy_battery_get_color(info->percent, info->is_charging, &r, &g, &b);

    printf("{\n  \"success\": true,\n  \"source\": \"");
    print_escaped_json_string(stdout, info->name);
    printf("\",\n  \"type\": \"%s\",\n  \"percent\": %d,\n  \"charging\": %s,\n  \"color\": \"#%02X%02X%02X\",\n  \"target\": \"",
           t_str, info->percent, info->is_charging ? "true" : "false", r, g, b);
    print_escaped_json_string(stdout, target_str);
    printf("\",\n  \"active_keys\": %zu,\n  \"target_slots\": [", active_count);
    for (size_t i = 0; i < count; i++) {
        printf("%d%s", slots[i], (i + 1 < count) ? ", " : "");
    }
    printf("]\n}\n");
}

static void print_battery_status_text(const CIYBatteryInfo *info, const char *target_str,
                                      const int *slots, size_t count, size_t active_count) {
    const char *t_str = "intern";
    if (info->type == CIY_BATTERY_SOURCE_PERIPHERAL) t_str = "Peripherie";
    else if (info->type == CIY_BATTERY_SOURCE_UPS) t_str = "USV";

    uint8_t r = 0, g = 0, b = 0;
    ciy_battery_get_color(info->percent, info->is_charging, &r, &g, &b);

    printf("=== CIY X79 Akku-Status ===\n");
    printf("Stromquelle:  %s (%s)\n", info->name, t_str);
    printf("Ladestand:    %d%%\n", info->percent);
    printf("Ladezustand:  %s\n", info->is_charging ? "Am Netzteil (Laden)" : "Akku-Betrieb (Entladen)");
    printf("Farbe:        #%02X%02X%02X (%s)\n", r, g, b,
           info->is_charging ? "Electric Cyan" : (info->percent > 50 ? "Grün" : (info->percent >= 21 ? "Gelb" : "Warn-Rot")));
    printf("Ziel-Zone:    %s (%zu Tasten: [", target_str, count);
    for (size_t i = 0; i < count; i++) {
        printf("%d%s", slots[i], (i + 1 < count) ? ", " : "");
    }
    printf("], %zu aktiv)\n", active_count);
}

static bool parse_probe_args(int argc, char **argv, int *out_led, int *out_slot,
                             uint8_t *out_r, uint8_t *out_g, uint8_t *out_b,
                             char *err_msg, size_t err_size) {
    bool has_slot = false;
    int slot_val = -1;
    int led_val = -1;
    const char *color_args[4] = {NULL, NULL, NULL, NULL};
    int num_color_args = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "probe-led") == 0 || strcmp(argv[i], "--json") == 0 ||
            strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
        if (strcmp(argv[i], "--slot") == 0) {
            if (i + 1 >= argc) {
                snprintf(err_msg, err_size, "Fehlender Wert für --slot (erlaubt: 0..124)");
                return false;
            }
            i++;
            if (!parse_int_range(argv[i], 0, 124, &slot_val)) {
                snprintf(err_msg, err_size, "Ungültiger Matrix-Slot '%s'. Erlaubter Bereich: 0..124.", argv[i]);
                return false;
            }
            has_slot = true;
            continue;
        }
        if (strncmp(argv[i], "--slot=", 7) == 0) {
            const char *val = argv[i] + 7;
            if (!parse_int_range(val, 0, 124, &slot_val)) {
                snprintf(err_msg, err_size, "Ungültiger Matrix-Slot '%s'. Erlaubter Bereich: 0..124.", val);
                return false;
            }
            has_slot = true;
            continue;
        }
        if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) {
            continue;
        }
        if (!has_slot && led_val == -1 && num_color_args == 0) {
            if (!parse_int_range(argv[i], 0, 125, &led_val)) {
                snprintf(err_msg, err_size, "Ungültiger LED-Index '%s'. Erlaubter Bereich: 0..125.", argv[i]);
                return false;
            }
            continue;
        }
        if (num_color_args < 3) {
            color_args[num_color_args++] = argv[i];
        } else {
            snprintf(err_msg, err_size, "Zu viele Argumente für probe-led: '%s'", argv[i]);
            return false;
        }
    }

    if (!has_slot && led_val == -1) {
        snprintf(err_msg, err_size, "Fehlendes Argument: LED-Index (0..125) oder --slot (0..124)");
        return false;
    }

    if (has_slot) {
        *out_slot = slot_val;
        *out_led = slot_val + 1;
    } else {
        *out_led = led_val;
        *out_slot = (led_val > 0) ? (led_val - 1) : -1;
    }

    uint8_t r = 255, g = 0, b = 0; // Standard: Rot
    if (num_color_args == 1) {
        if (!ciy_parse_hex_color(color_args[0], &r, &g, &b)) {
            snprintf(err_msg, err_size, "Ungültiges Farbformat '%s' (erwartet: #RRGGBB, #RGB oder r g b)", color_args[0]);
            return false;
        }
    } else if (num_color_args == 3) {
        if (!parse_range(color_args[0], 0, 255, &r) ||
            !parse_range(color_args[1], 0, 255, &g) ||
            !parse_range(color_args[2], 0, 255, &b)) {
            snprintf(err_msg, err_size, "Ungültige RGB-Farbwerte (jeweils 0..255)");
            return false;
        }
    } else if (num_color_args > 0) {
        snprintf(err_msg, err_size, "Ungültige Farbangabe: Bitte Hex (#RRGGBB) oder 3 Dezimalwerte (r g b) angeben");
        return false;
    }

    *out_r = r;
    *out_g = g;
    *out_b = b;
    return true;
}

static bool parse_sweep_args(int argc, char **argv, bool *out_auto, uint32_t *out_delay_ms,
                             char *err_msg, size_t err_size) {
    bool auto_mode = false;
    uint32_t delay_ms = 500;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "sweep") == 0 || strcmp(argv[i], "--json") == 0 ||
            strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            auto_mode = false;
            continue;
        }
        if (strcmp(argv[i], "--auto") == 0) {
            auto_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--delay-ms") == 0) {
            if (i + 1 >= argc) {
                snprintf(err_msg, err_size, "Fehlender Wert für --delay-ms");
                return false;
            }
            i++;
            int d = 0;
            if (!parse_int_range(argv[i], 100, 100000, &d)) {
                snprintf(err_msg, err_size, "Ungültige Verzögerung '%s' für --delay-ms (Mindestwert: 100 ms)", argv[i]);
                return false;
            }
            delay_ms = (uint32_t)d;
            continue;
        }
        if (strncmp(argv[i], "--delay-ms=", 11) == 0) {
            const char *val = argv[i] + 11;
            int d = 0;
            if (!parse_int_range(val, 100, 100000, &d)) {
                snprintf(err_msg, err_size, "Ungültige Verzögerung '%s' für --delay-ms (Mindestwert: 100 ms)", val);
                return false;
            }
            delay_ms = (uint32_t)d;
            continue;
        }
        if (argv[i][0] != '-') {
            snprintf(err_msg, err_size, "Unerwartetes Argument '%s'. 'sweep' akzeptiert keine Positionsargumente.", argv[i]);
            return false;
        }
    }

    *out_auto = auto_mode;
    *out_delay_ms = delay_ms;
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
    printf("  probe-led <index> [r g b | #HEX] [-b 0..5] Aktiviert eine einzelne LED (0..125 oder --slot 0..124)\n");
    printf("  sweep [--auto] [--delay-ms <ms>]    Schrittweiser Matrix-Durchlauf (Standard: interaktiv)\n");
    printf("  battery [--apply] [Optionen]        Zeigt Akkustand an oder spiegelt ihn auf Tastatur-Slots\n");
    printf("  gradient <#HEX1> <#HEX2> [Optionen] Erzeugt linearen sRGB-Farbverlauf (U-Coords / Zeilen)\n");
    printf("  help, --help, -h                    Zeigt diese Hilfe an\n\n");
    printf("Optionen:\n");
    printf("  -b, --brightness <0..5>             Helligkeit für Schreibbefehle (fill, load, set, wasd, battery, gradient etc.; Standard: 5)\n");
    printf("  --dir <horizontal|vertical>         Richtung für gradient (Standard: horizontal)\n");
    printf("  --apply                             Wendet den Akku-Indikator direkt auf die Tastatur an\n");
    printf("  --target <1-0|space|nav|<slots>>    Ziel-Tasten für den Akku-Indikator (Standard: 1-0)\n");
    printf("  --source <internal|peripheral|all>  Bevorzugte Stromquelle (Standard: internal)\n");
    printf("  --list-sources                      Listet alle erkannten Stromquellen auf\n");
    printf("  --watch                             Überwacht Akkustandsänderungen kontinuierlich\n");
    printf("  --segmented                         Segmentierte Ampelfarben pro Taste anstelle von Einzelfarbe\n");
    printf("  --auto                              Automatischer Durchlauf für sweep (Standard: interaktiv)\n");
    printf("  --delay-ms <ms>                     Verzögerung für --auto bei sweep (Standard: 500 ms, Min: 100 ms)\n");
    printf("  --slot <0..124>                     Matrix-Slot anstelle von LED-Index für probe-led\n");
    printf("  -i, --interactive                   Interaktiver Modus für sweep (Standardverhalten)\n");
    printf("  --dry-run                           Simuliert Übertragung ohne Öffnen von USB-Geräten (für alle Schreibbefehle)\n");
    printf("  --dump-bytes                        Gibt Roh-Wire-Bytes (Hex) der generierten USB-Reports aus\n");
    printf("  --json                              Ausgabe im maschinenlesbaren JSON-Format\n\n");
}

int main(int argc, char **argv) {
    bool json_mode = false;
    bool dry_run = false;
    bool dump_bytes = false;
    const char *cmd = NULL;

    bool batt_apply_mode = false;
    bool batt_watch_mode = false;
    bool batt_segmented = false;
    const char *batt_target_str = "1-0";
    CIYBatterySourceType batt_stype = CIY_BATTERY_SOURCE_INTERNAL;
    int batt_target_slots[64] = {0};
    size_t batt_target_count = 0;

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
            if (strcmp(argv[i], "--json") != 0 && strcmp(argv[i], "--dry-run") != 0 &&
                strcmp(argv[i], "--dump-bytes") != 0 && strcmp(argv[i], "--auto") != 0 &&
                strcmp(argv[i], "-i") != 0 && strcmp(argv[i], "--interactive") != 0 &&
                strcmp(argv[i], "--apply") != 0 && strcmp(argv[i], "--watch") != 0 &&
                strcmp(argv[i], "--list-sources") != 0 && strcmp(argv[i], "--segmented") != 0) {
                if (strcmp(argv[i], "--slot") == 0 || strcmp(argv[i], "--delay-ms") == 0 ||
                    strcmp(argv[i], "--target") == 0 || strcmp(argv[i], "--source") == 0 ||
                    strcmp(argv[i], "--dir") == 0) {
                    if (i + 1 < argc && argv[i + 1][0] != '-') {
                        i++;
                    }
                    continue;
                }
                if (strncmp(argv[i], "--slot=", 7) == 0 || strncmp(argv[i], "--delay-ms=", 11) == 0 ||
                    strncmp(argv[i], "--target=", 9) == 0 || strncmp(argv[i], "--source=", 9) == 0 ||
                    strncmp(argv[i], "--dir=", 6) == 0) {
                    continue;
                }
                // Wenn es eine Zahl ist (z. B. negatives Argument bei set -1 oder probe-led -1), übernimmt der Befehl die Prüfung
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
        strcmp(cmd, "all-red") != 0 && strcmp(cmd, "set") != 0 &&
        strcmp(cmd, "probe-led") != 0 && strcmp(cmd, "sweep") != 0 &&
        strcmp(cmd, "battery") != 0 && strcmp(cmd, "gradient") != 0) {
        if (json_mode) {
            printf("{\n  \"success\": false,\n  \"error\": \"--dry-run wird derzeit nur für Schreibbefehle (load, fill, set, wasd, wasd-arrows, all-red, probe-led, sweep, battery, gradient) unterstützt\"\n}\n");
        } else {
            fprintf(stderr, "Fehler: --dry-run wird derzeit nur für Schreibbefehle (load, fill, set, wasd, wasd-arrows, all-red, probe-led, sweep, battery, gradient) unterstützt.\n");
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

    if (strcmp(cmd, "battery") == 0) {
        bool list_sources = false;
        const char *source_str = "internal";
        char err_msg[256] = {0};

        if (!parse_battery_options(argc, argv, &batt_apply_mode, &batt_watch_mode, &list_sources, &batt_segmented,
                                  &batt_target_str, &source_str, err_msg, sizeof(err_msg))) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"");
                print_escaped_json_string(stdout, err_msg);
                printf("\"\n}\n");
            } else {
                fprintf(stderr, "Fehler: %s\n", err_msg);
            }
            return 1;
        }

        if (!ciy_battery_parse_target(batt_target_str, batt_target_slots, 64, &batt_target_count)) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Ungültiges Akku-Ziel '");
                print_escaped_json_string(stdout, batt_target_str);
                printf("' (erlaubt: 1-0, space, nav oder Slot-Liste)\"\n}\n");
            } else {
                fprintf(stderr, "Fehler: Ungültiges Akku-Ziel '%s'. Erlaubt: 1-0, space, nav oder Komma-getrennte Slot-Liste (z. B. 14,9,15,21).\n", batt_target_str);
            }
            return 1;
        }

        if (list_sources) {
            CIYBatteryInfo sources[8];
            size_t count = ciy_battery_list_sources(sources, 8);
            if (json_mode) {
                print_battery_sources_json(sources, count);
            } else {
                print_battery_sources_text(sources, count);
            }
            return 0;
        }

        batt_stype = parse_battery_source_type(source_str);
        CIYBatteryInfo info;
        ciy_battery_get_info(batt_stype, &info);

        size_t active_count = 0;
        if (batt_target_count == 1) {
            active_count = 1;
        } else if (info.percent > 0) {
            active_count = (size_t)((info.percent * batt_target_count + 99) / 100);
            if (active_count > batt_target_count) active_count = batt_target_count;
        }

        if (dry_run) {
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_battery_calculate_overlay(NULL, info.percent, info.is_charging,
                                         batt_target_slots, batt_target_count, batt_segmented, rgb);
            uint8_t r = 0, g = 0, b = 0;
            ciy_battery_get_color(info.percent, info.is_charging, &r, &g, &b);
            char prof_name[64];
            snprintf(prof_name, sizeof(prof_name), "Akku-Status (%d%%, #%02X%02X%02X)",
                     info.percent, r, g, b);
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), br, prof_name, (int)active_count)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        }

        if (!batt_apply_mode && !batt_watch_mode) {
            // Reiner Status-/Info-Modus (ohne Hardwarezugriff)
            if (json_mode) {
                print_battery_status_json(&info, batt_target_str, batt_target_slots, batt_target_count, active_count);
            } else {
                print_battery_status_text(&info, batt_target_str, batt_target_slots, batt_target_count, active_count);
            }
            return 0;
        }
    }

    if (dry_run) {
        if (strcmp(cmd, "gradient") == 0) {
            const char *col1_str = NULL, *col2_str = NULL;
            CIYGradientDirection dir = CIY_GRADIENT_HORIZONTAL;
            char err[256] = {0};
            if (!parse_gradient_options(argc, argv, &col1_str, &col2_str, &dir, err, sizeof(err))) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"%s\"\n}\n", err);
                } else {
                    fprintf(stderr, "Fehler: %s\n", err);
                }
                return 1;
            }
            uint8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
            if (!ciy_parse_hex_color(col1_str, &r1, &g1, &b1) || !ciy_parse_hex_color(col2_str, &r2, &g2, &b2)) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Hex-Farbe (erwartet #RRGGBB)\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: Ungültige Hex-Farbe. Erwartet: #RRGGBB (z. B. #00FFFF #FF00FF).\n");
                }
                return 1;
            }
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_gradient_generate_planar(rgb, r1, g1, b1, r2, g2, b2, dir);
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            char prof_name[64];
            snprintf(prof_name, sizeof(prof_name), "Verlauf (%s -> %s, %s)",
                     col1_str, col2_str, dir == CIY_GRADIENT_VERTICAL ? "vertikal" : "horizontal");
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), br, prof_name, 69)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        }
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

            // Strikte Profilvalidierung ZUERST (Single Read, keine TOCTOU-Gefahr)
            CIYValidationResult val_res;
            char *buf = ciy_read_text_file_with_result(file_arg, NULL, &val_res);
            if (!buf) {
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

            if (!ciy_validate_profile_json(buf, &val_res)) {
                free(buf);
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
            uint8_t profile_brightness = CIY_MAX_BRIGHTNESS;
            if (!ciy_parse_profile_json_with_brightness(buf, target_profile, rgb, profile_name, sizeof(profile_name), &slots_count, &profile_brightness)) {
                free(buf);
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
            free(buf);

            if (!has_flag_brightness && num_extras < 2) {
                brightness = profile_brightness;
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
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), br, "WASD Gaming (Rot / Basis Blau)", 4)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "wasd-arrows") == 0) {
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_build_wasd_and_arrows_profile(rgb, true);
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), br, "WASD + Pfeiltasten (Rot & Gelb / Basis Blau)", 8)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "all-red") == 0) {
            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_build_all_red_profile(rgb);
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), br, "Alles Rot", 69)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "set") == 0) {
            const char *set_args[4] = {NULL, NULL, NULL, NULL};
            int num_set_args = 0;
            int total_set_args = 0;
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "set") == 0 || strcmp(argv[i], "--json") == 0 ||
                    strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
                if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) continue;
                if (num_set_args < 4) {
                    set_args[num_set_args++] = argv[i];
                }
                total_set_args++;
            }
            if (total_set_args < 1 || total_set_args > 4) {
                if (json_mode) {
                    if (total_set_args > 4) {
                        printf("{\n  \"success\": false,\n  \"error\": \"Zu viele Argumente. Maximal 4 erlaubt. Verwendung: ciy_ctl set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\"\n}\n");
                    } else {
                        printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Argumente. Verwendung: ciy_ctl set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\"\n}\n");
                    }
                } else {
                    if (total_set_args > 4) {
                        fprintf(stderr, "Fehler: Zu viele Argumente (maximal 4 erlaubt).\nVerwendung: %s set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\n", argv[0]);
                    } else {
                        fprintf(stderr, "Verwendung: %s set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\n", argv[0]);
                    }
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
                (num_set_args > 3 && !has_flag_brightness && !parse_range(set_args[3], 0, CIY_MAX_BRIGHTNESS, &st.brightness))) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\"\n}\n");
                } else {
                    fprintf(stderr, "Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\n");
                }
                return 1;
            }
            if (has_flag_brightness) {
                st.brightness = flag_brightness;
            }
            CIYTransferPlan plan;
            if (!ciy_build_state_transfer_plan(&plan, st)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "probe-led") == 0) {
            int led = 0, slot = 0;
            uint8_t r = 0, g = 0, b = 0;
            char err[256];
            if (!parse_probe_args(argc, argv, &led, &slot, &r, &g, &b, err, sizeof(err))) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"");
                    print_escaped_json_string(stdout, err);
                    printf("\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: %s\n", err);
                }
                return 1;
            }
            uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
            if (led >= 0 && led <= 125) {
                rgb[led] = r;
                rgb[CIY_LED_COUNT + led] = g;
                rgb[(2 * CIY_LED_COUNT) + led] = b;
            }
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
            const CIYLogicalKey *k = (slot >= 0) ? ciy_layout_find_key_by_slot(layout, slot) : NULL;
            char prof_name[64];
            snprintf(prof_name, sizeof(prof_name), "Probe LED %d (%s, #%02X%02X%02X)",
                     led, k ? k->name : "Unbelegt", r, g, b);
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), br, prof_name, 1)) {
                return 1;
            }
            print_dry_run_plan(&plan, json_mode, dump_bytes);
            return 0;
        } else if (strcmp(cmd, "sweep") == 0) {
            bool auto_mode = false;
            uint32_t delay_ms = 500;
            char err[256];
            if (!parse_sweep_args(argc, argv, &auto_mode, &delay_ms, err, sizeof(err))) {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"");
                    print_escaped_json_string(stdout, err);
                    printf("\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler: %s\n", err);
                }
                return 1;
            }
            uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
            rgb[1] = 255; // Sample: LED 1
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            CIYTransferPlan plan;
            if (!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), br, "Sweep Simulation", 1)) {
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
            if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) continue;
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
            char *buf = ciy_read_text_file_with_result(file_arg, NULL, &val_res);
            if (!buf) {
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
            } else if (!ciy_validate_profile_json(buf, &val_res)) {
                free(buf);
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
                uint8_t profile_brightness = CIY_MAX_BRIGHTNESS;
                if (!ciy_parse_profile_json_with_brightness(buf, target_profile, rgb, profile_name, sizeof(profile_name), &slots_count, &profile_brightness)) {
                    free(buf);
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
                    free(buf);
                    if (!has_flag_brightness && num_extras < 2) {
                        brightness = profile_brightness;
                    }
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
        const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;

        if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br)) {
            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"wasd\",\n  \"name\": \"WASD Gaming (Rot / Basis Blau)\",\n  \"brightness\": %u\n}\n", br);
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
        const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
        if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br)) {
            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"wasd-arrows\",\n  \"name\": \"WASD + Pfeiltasten (Rot & Gelb / Basis Blau)\",\n  \"brightness\": %u\n}\n", br);
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
        const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
        if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br)) {
            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"all-red\",\n  \"name\": \"Alles Rot\",\n  \"brightness\": %u\n}\n", br);
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
        int total_set_args = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "set") == 0 || strcmp(argv[i], "--json") == 0 ||
                strcmp(argv[i], "--dry-run") == 0 || strcmp(argv[i], "--dump-bytes") == 0) continue;
            if (argv[i][0] == '-' && (argv[i][1] < '0' || argv[i][1] > '9')) continue;
            if (num_set_args < 4) {
                set_args[num_set_args++] = argv[i];
            }
            total_set_args++;
        }

        if (total_set_args < 1 || total_set_args > 4) {
            if (json_mode) {
                if (total_set_args > 4) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Zu viele Argumente. Maximal 4 erlaubt. Verwendung: ciy_ctl set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\"\n}\n");
                } else {
                    printf("{\n  \"success\": false,\n  \"error\": \"Fehlende Argumente. Verwendung: ciy_ctl set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\"\n}\n");
                }
            } else {
                if (total_set_args > 4) {
                    fprintf(stderr, "Fehler: Zu viele Argumente (maximal 4 erlaubt).\nVerwendung: %s set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\n", argv[0]);
                } else {
                    fprintf(stderr, "Verwendung: %s set <Modus (0..20)> [Farbe (0..10)] [Tempo (0..4)] [Helligkeit (0..5)]\n", argv[0]);
                }
            }
            rc = 1;
        } else {
            CIYLightingState current = {19, 0, 1, 5};
            const bool needs_state = (num_set_args < 3) || (!has_flag_brightness && num_set_args < 4);
            if (needs_state && !ciy_get_state(&dev, &current)) {
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
                    (num_set_args > 3 && !has_flag_brightness && !parse_range(set_args[3], 0, CIY_MAX_BRIGHTNESS, &st.brightness))) {
                    if (json_mode) {
                        printf("{\n  \"success\": false,\n  \"error\": \"Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\"\n}\n");
                    } else {
                        fprintf(stderr, "Ungültiger Wert. Bereiche: Modus 0..20, Farbe 0..10, Tempo 0..4, Helligkeit 0..5\n");
                    }
                    rc = 1;
                } else {
                    if (has_flag_brightness) {
                        st.brightness = flag_brightness;
                    }
                    if (ciy_set_state(&dev, st)) {
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
        }
    } else if (strcmp(cmd, "probe-led") == 0) {
        int led = 0, slot = 0;
        uint8_t r = 0, g = 0, b = 0;
        char err[256];
        if (!parse_probe_args(argc, argv, &led, &slot, &r, &g, &b, err, sizeof(err))) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"");
                print_escaped_json_string(stdout, err);
                printf("\"\n}\n");
            } else {
                fprintf(stderr, "Fehler: %s\n", err);
            }
            rc = 1;
        } else {
            uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
            if (led >= 0 && led <= 125) {
                rgb[led] = r;
                rgb[CIY_LED_COUNT + led] = g;
                rgb[(2 * CIY_LED_COUNT) + led] = b;
            }
            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
            const CIYLogicalKey *k = (slot >= 0) ? ciy_layout_find_key_by_slot(layout, slot) : NULL;
            const char *key_name = k ? k->name : "Unbelegt";

            if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br)) {
                if (json_mode) {
                    printf("{\n  \"success\": true,\n  \"action\": \"probe-led\",\n  \"led_index\": %d,\n  \"matrix_slot\": %d,\n  \"key_name\": \"", led, slot);
                    print_escaped_json_string(stdout, key_name);
                    printf("\",\n  \"color\": \"#%02X%02X%02X\",\n  \"brightness\": %u\n}\n", r, g, b, br);
                } else {
                    printf("LED-Index %d (Slot %d, Taste '%s') erfolgreich angesteuert mit Farbe #%02X%02X%02X (Helligkeit %u).\n",
                           led, slot, key_name, r, g, b, br);
                }
            } else {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Senden des Profils an die Tastatur\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler beim Senden des Profils an die Tastatur.\n");
                }
                rc = 1;
            }
        }
    } else if (strcmp(cmd, "sweep") == 0) {
        bool auto_mode = false;
        uint32_t delay_ms = 500;
        char err[256];
        if (!parse_sweep_args(argc, argv, &auto_mode, &delay_ms, err, sizeof(err))) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"");
                print_escaped_json_string(stdout, err);
                printf("\"\n}\n");
            } else {
                fprintf(stderr, "Fehler: %s\n", err);
            }
            rc = 1;
        } else if (!auto_mode && !isatty(STDIN_FILENO)) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"'sweep' erfordert ein interaktives Terminal (TTY) oder das Flag '--auto'\"\n}\n");
            } else {
                fprintf(stderr, "Fehler: 'sweep' erfordert ein interaktives Terminal (TTY) oder das Flag '--auto'.\n");
            }
            rc = 1;
        } else {
            CIYLightingState orig_state = {15, 0, 1, 5};
            bool had_orig_state = ciy_get_state(&dev, &orig_state);

            s_sweep_interrupted = 0;
            struct sigaction sa, old_sa_int, old_sa_term;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = sweep_sigint_handler;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGINT, &sa, &old_sa_int);
            sigaction(SIGTERM, &sa, &old_sa_term);

            bool is_tty = isatty(STDIN_FILENO);
            if (is_tty && !auto_mode) {
                tcgetattr(STDIN_FILENO, &s_orig_termios);
                s_termios_saved = 1;
                struct termios raw = s_orig_termios;
                raw.c_lflag &= ~(ICANON | ECHO);
                raw.c_cc[VMIN] = 1;
                raw.c_cc[VTIME] = 0;
                tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            }

            const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
            const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();

            if (!json_mode) {
                if (auto_mode) {
                    printf("=== CIY X79 Matrix Sweep (Automatisch, Verzögerung: %u ms) ===\n", delay_ms);
                    printf("Drücken Sie Ctrl+C zum Abbrechen.\n");
                } else {
                    printf("=== CIY X79 Matrix Sweep (Interaktiv) ===\n");
                    printf("Befehle: [Enter]/[n] = Nächste Taste, [p] = Vorherige Taste, [q] = Beenden (Ctrl+C = Abbruch)\n");
                }
            }

            if (auto_mode) {
                for (int cur_slot = 0; cur_slot <= 124 && !s_sweep_interrupted; cur_slot++) {
                    int led = cur_slot + 1;
                    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
                    rgb[led] = 255;
                    ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br);
                    const CIYLogicalKey *k = ciy_layout_find_key_by_slot(layout, cur_slot);
                    if (!json_mode) {
                        printf("\r\033[K[Slot %3d / LED %3d] Taste: %-16s", cur_slot, led, k ? k->name : "Unbelegt");
                        fflush(stdout);
                    }
                    for (uint32_t ms = 0; ms < delay_ms && !s_sweep_interrupted; ms += 10) {
                        struct timespec ts = {0, 10 * 1000 * 1000};
                        nanosleep(&ts, NULL);
                    }
                }
            } else {
                int cur_slot = 0;
                while (!s_sweep_interrupted && cur_slot >= 0 && cur_slot <= 124) {
                    int led = cur_slot + 1;
                    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
                    rgb[led] = 255;
                    ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br);
                    const CIYLogicalKey *k = ciy_layout_find_key_by_slot(layout, cur_slot);
                    if (!json_mode) {
                        printf("\r\033[K[Slot %3d / LED %3d] Taste: %-16s -> ", cur_slot, led, k ? k->name : "Unbelegt");
                        fflush(stdout);
                    }
                    int ch = getchar();
                    if (ch == EOF || s_sweep_interrupted || ch == 'q' || ch == 'Q' ||
                        ch == 27 /* ESC */ || ch == 3 /* ETX / Ctrl+C */ || ch == 4 /* EOT / Ctrl+D */) {
                        break;
                    } else if (ch == 'p' || ch == 'P') {
                        if (cur_slot > 0) cur_slot--;
                    } else if (ch == '\n' || ch == '\r' || ch == 'n' || ch == 'N' || ch == ' ') {
                        cur_slot++;
                    }
                }
            }

            if (s_termios_saved) {
                tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_termios);
                s_termios_saved = 0;
            }
            sigaction(SIGINT, &old_sa_int, NULL);
            sigaction(SIGTERM, &old_sa_term, NULL);

            if (had_orig_state) {
                ciy_set_state(&dev, orig_state);
            }

            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"sweep\",\n  \"interrupted\": %s\n}\n",
                       s_sweep_interrupted ? "true" : "false");
            } else {
                if (s_sweep_interrupted) {
                    printf("\nSweep abgebrochen. Ursprünglicher Beleuchtungsstatus wiederhergestellt.\n");
                } else {
                    printf("\nSweep abgeschlossen. Ursprünglicher Beleuchtungsstatus wiederhergestellt.\n");
                }
            }
        }
    } else if (strcmp(cmd, "battery") == 0) {
        const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;

        if (batt_watch_mode) {
            struct sigaction sa, old_sa_int, old_sa_term;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = watch_sigint_handler;
            sigaction(SIGINT, &sa, &old_sa_int);
            sigaction(SIGTERM, &sa, &old_sa_term);

            int last_percent = -999;
            bool last_charging = false;

            if (!json_mode) {
                printf("=== CIY X79 Akku-Überwachung aktiv (Ziel: '%s') ===\n", batt_target_str);
                printf("Drücken Sie Ctrl+C zum Beenden.\n");
            }

            while (!s_watch_interrupted) {
                CIYBatteryInfo info;
                ciy_battery_get_info(batt_stype, &info);

                if (info.percent != last_percent || info.is_charging != last_charging) {
                    last_percent = info.percent;
                    last_charging = info.is_charging;

                    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
                    ciy_battery_calculate_overlay(NULL, info.percent, info.is_charging,
                                                 batt_target_slots, batt_target_count, batt_segmented, rgb);
                    ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br);

                    if (!json_mode) {
                        uint8_t r = 0, g = 0, b = 0;
                        ciy_battery_get_color(info.percent, info.is_charging, &r, &g, &b);
                        printf("\r\033[KAkku aktualisiert: %d%% (%s, #%02X%02X%02X) auf '%s'",
                               info.percent, info.is_charging ? "Laden" : "Akku", r, g, b, batt_target_str);
                        fflush(stdout);
                    }
                }

                for (int ms = 0; ms < 1000 && !s_watch_interrupted; ms += 100) {
                    struct timespec ts = {0, 100 * 1000 * 1000};
                    nanosleep(&ts, NULL);
                }
            }

            sigaction(SIGINT, &old_sa_int, NULL);
            sigaction(SIGTERM, &old_sa_term, NULL);

            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"battery_watch\",\n  \"interrupted\": true\n}\n");
            } else {
                printf("\nAkku-Überwachung beendet.\n");
            }
        } else {
            // apply_mode
            CIYBatteryInfo info;
            ciy_battery_get_info(batt_stype, &info);

            size_t active_count = 0;
            if (batt_target_count == 1) {
                active_count = 1;
            } else if (info.percent > 0) {
                active_count = (size_t)((info.percent * batt_target_count + 99) / 100);
                if (active_count > batt_target_count) active_count = batt_target_count;
            }

            uint8_t rgb[CIY_PLANAR_RGB_SIZE];
            ciy_battery_calculate_overlay(NULL, info.percent, info.is_charging,
                                         batt_target_slots, batt_target_count, batt_segmented, rgb);

            uint8_t r = 0, g = 0, b = 0;
            ciy_battery_get_color(info.percent, info.is_charging, &r, &g, &b);

            if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br)) {
                if (json_mode) {
                    printf("{\n  \"success\": true,\n  \"action\": \"battery_apply\",\n  \"percent\": %d,\n  \"charging\": %s,\n  \"color\": \"#%02X%02X%02X\",\n  \"target\": \"",
                           info.percent, info.is_charging ? "true" : "false", r, g, b);
                    print_escaped_json_string(stdout, batt_target_str);
                    printf("\",\n  \"active_keys\": %zu,\n  \"brightness\": %u\n}\n",
                           active_count, br);
                } else {
                    printf("Akku-Status (%d%%, #%02X%02X%02X) erfolgreich auf Ziel '%s' angewendet (%zu/%zu Tasten aktiv, Helligkeit %u/5).\n",
                           info.percent, r, g, b, batt_target_str, active_count, batt_target_count, br);
                }
            } else {
                if (json_mode) {
                    printf("{\n  \"success\": false,\n  \"error\": \"Fehler beim Übertragen des Akku-Overlays an die Tastatur\"\n}\n");
                } else {
                    fprintf(stderr, "Fehler beim Übertragen des Akku-Overlays an die Tastatur.\n");
                }
                rc = 1;
            }
        }
    } else if (strcmp(cmd, "gradient") == 0) {
        const char *col1_str = NULL, *col2_str = NULL;
        CIYGradientDirection dir = CIY_GRADIENT_HORIZONTAL;
        char err[256] = {0};
        if (!parse_gradient_options(argc, argv, &col1_str, &col2_str, &dir, err, sizeof(err))) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"%s\"\n}\n", err);
            } else {
                fprintf(stderr, "Fehler: %s\n", err);
            }
            ciy_close(&dev);
            return 1;
        }
        uint8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
        if (!ciy_parse_hex_color(col1_str, &r1, &g1, &b1) || !ciy_parse_hex_color(col2_str, &r2, &g2, &b2)) {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Ungültige Hex-Farbe (erwartet #RRGGBB)\"\n}\n");
            } else {
                fprintf(stderr, "Fehler: Ungültige Hex-Farbe. Erwartet: #RRGGBB (z. B. #00FFFF #FF00FF).\n");
            }
            ciy_close(&dev);
            return 1;
        }
        uint8_t rgb[CIY_PLANAR_RGB_SIZE];
        ciy_gradient_generate_planar(rgb, r1, g1, b1, r2, g2, b2, dir);
        const uint8_t br = has_flag_brightness ? flag_brightness : CIY_MAX_BRIGHTNESS;
        if (ciy_apply_custom_profile_with_brightness(&dev, rgb, sizeof(rgb), br)) {
            if (json_mode) {
                printf("{\n  \"success\": true,\n  \"action\": \"gradient\",\n  \"color_start\": \"%s\",\n  \"color_end\": \"%s\",\n  \"direction\": \"%s\",\n  \"brightness\": %u\n}\n",
                       col1_str, col2_str, dir == CIY_GRADIENT_VERTICAL ? "vertical" : "horizontal", br);
            } else {
                printf("Farbverlauf (%s -> %s, %s, Helligkeit %u) erfolgreich auf Tastatur übertragen.\n",
                       col1_str, col2_str, dir == CIY_GRADIENT_VERTICAL ? "vertikal" : "horizontal", br);
            }
            rc = 0;
        } else {
            if (json_mode) {
                printf("{\n  \"success\": false,\n  \"error\": \"Übertragung des Farbverlaufs an USB fehlgeschlagen\"\n}\n");
            } else {
                fprintf(stderr, "Fehler: Übertragung des Farbverlaufs an USB fehlgeschlagen.\n");
            }
            rc = 1;
        }
    } else {
        if (json_mode) {
            printf("{\n  \"success\": false,\n  \"error\": \"Unbekannter Befehl '");
            print_escaped_json_string(stdout, cmd);
            printf("'\"\n}\n");
        } else {
            fprintf(stderr, "Unbekannter Befehl: %s\n", cmd);
            fprintf(stderr, "Verfügbare Befehle: status, fill, load, wasd, wasd-arrows, all-red, list-modes, set, export-layout, validate-profile, probe-led, sweep, battery, gradient\n");
            fprintf(stderr, "Rufen Sie '%s --help' für weitere Informationen auf.\n", argv[0]);
        }
        rc = 1;
    }

    ciy_close(&dev);
    return rc;
}
