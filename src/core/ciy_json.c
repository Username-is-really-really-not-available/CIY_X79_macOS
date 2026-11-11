#include "ciy_json.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ciy_ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return (int)ca - (int)cb;
        a++;
        b++;
    }
    unsigned char ca = (unsigned char)*a;
    unsigned char cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
    return (int)ca - (int)cb;
}

static int hex_digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ciy_parse_hex_color(const char *str, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (r) *r = 0;
    if (g) *g = 0;
    if (b) *b = 0;
    if (!str || !r || !g || !b) return false;
    while (*str && (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')) str++;
    if (*str == '#') str++;
    size_t len = 0;
    while (str[len] && str[len] != ' ' && str[len] != '\t' && str[len] != '\r' && str[len] != '\n') {
        len++;
    }
    for (size_t i = len; str[i]; i++) {
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\r' && str[i] != '\n') return false;
    }
    if (len == 6) {
        int r1 = hex_digit_val(str[0]), r2 = hex_digit_val(str[1]);
        int g1 = hex_digit_val(str[2]), g2 = hex_digit_val(str[3]);
        int b1 = hex_digit_val(str[4]), b2 = hex_digit_val(str[5]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
        *r = (uint8_t)((r1 << 4) | r2);
        *g = (uint8_t)((g1 << 4) | g2);
        *b = (uint8_t)((b1 << 4) | b2);
        return true;
    } else if (len == 3) {
        int r1 = hex_digit_val(str[0]);
        int g1 = hex_digit_val(str[1]);
        int b1 = hex_digit_val(str[2]);
        if (r1 < 0 || g1 < 0 || b1 < 0) return false;
        *r = (uint8_t)((r1 << 4) | r1);
        *g = (uint8_t)((g1 << 4) | g1);
        *b = (uint8_t)((b1 << 4) | b1);
        return true;
    }
    return false;
}

static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const char *parse_json_str(const char *p, char *out, size_t max_len) {
    if (!p || *p != '"') return NULL;
    if (out && max_len == 0) return NULL;
    p++;
    size_t limit = (out != NULL) ? max_len : ((max_len > 0) ? max_len : CIY_JSON_MAX_STRLEN);
    size_t written = 0;

    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) return NULL;
            if (*p == 'u') {
                if (!p[1] || !p[2] || !p[3] || !p[4]) return NULL;
                int h0 = hex_digit(p[1]);
                int h1 = hex_digit(p[2]);
                int h2 = hex_digit(p[3]);
                int h3 = hex_digit(p[4]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return NULL;
                uint32_t cp = (uint32_t)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                if (cp == 0) return NULL;

                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // High surrogate: must be followed by \u and a valid low surrogate
                    if (p[5] != '\\' || p[6] != 'u') return NULL;
                    if (!p[7] || !p[8] || !p[9] || !p[10]) return NULL;
                    int l0 = hex_digit(p[7]);
                    int l1 = hex_digit(p[8]);
                    int l2 = hex_digit(p[9]);
                    int l3 = hex_digit(p[10]);
                    if (l0 < 0 || l1 < 0 || l2 < 0 || l3 < 0) return NULL;
                    uint32_t low = (uint32_t)((l0 << 12) | (l1 << 8) | (l2 << 4) | l3);
                    if (low < 0xDC00 || low > 0xDFFF) return NULL;

                    uint32_t full_cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    if (out) {
                        if (written + 4 >= limit) return NULL;
                        out[written++] = (char)(0xF0 | (full_cp >> 18));
                        out[written++] = (char)(0x80 | ((full_cp >> 12) & 0x3F));
                        out[written++] = (char)(0x80 | ((full_cp >> 6) & 0x3F));
                        out[written++] = (char)(0x80 | (full_cp & 0x3F));
                    } else {
                        if (written + 4 < limit) written += 4;
                    }
                    p += 11;
                    continue;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    // Isolated low surrogate: invalid
                    return NULL;
                } else {
                    // Valid BMP codepoint
                    if (cp < 0x80) {
                        if (out) {
                            if (written + 1 >= limit) return NULL;
                            out[written++] = (char)cp;
                        } else {
                            if (written + 1 < limit) written++;
                        }
                    } else if (cp < 0x800) {
                        if (out) {
                            if (written + 2 >= limit) return NULL;
                            out[written++] = (char)(0xC0 | (cp >> 6));
                            out[written++] = (char)(0x80 | (cp & 0x3F));
                        } else {
                            if (written + 2 < limit) written += 2;
                        }
                    } else {
                        if (out) {
                            if (written + 3 >= limit) return NULL;
                            out[written++] = (char)(0xE0 | (cp >> 12));
                            out[written++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            out[written++] = (char)(0x80 | (cp & 0x3F));
                        } else {
                            if (written + 3 < limit) written += 3;
                        }
                    }
                    p += 5;
                    continue;
                }
            } else {
                /* Einfache Einzeichen-Escapes: \n \r \t \b \f \" \\ \/ */
                static const struct { char esc; char rep; } escape_tab[] = {
                    {'n', '\n'}, {'r', '\r'}, {'t', '\t'}, {'b', '\b'},
                    {'f', '\f'}, {'"', '"'}, {'\\', '\\'}, {'/', '/'},
                };
                char rep = 0;
                for (size_t e = 0; e < sizeof(escape_tab) / sizeof(escape_tab[0]); e++) {
                    if (escape_tab[e].esc == *p) { rep = escape_tab[e].rep; break; }
                }
                if (rep) {
                    if (out) {
                        if (written + 1 >= limit) return NULL;
                        out[written++] = rep;
                    } else {
                        if (written + 1 < limit) written++;
                    }
                    p++;
                    continue;
                }
                return NULL; // Unbekanntes Escape
            }
        }
        unsigned char c0 = (unsigned char)*p;
        if (c0 < 0x20) {
            // RFC 8259 §7: Rohe unescapete Control-Characters (0x00..0x1F) unzulässig
            return NULL;
        }
        if (c0 < 0x80) {
            if (out) {
                if (written + 1 >= limit) return NULL;
                out[written++] = (char)c0;
            } else {
                if (written + 1 < limit) written++;
            }
            p++;
            continue;
        }

        // Strikte RFC 3629 UTF-8-Validierung (lehnt Overlong-NUL, CESU-8-Surrogates, >U+10FFFF & Fragmente ab)
        size_t utf8_len = 0;
        if (c0 >= 0xC2 && c0 <= 0xDF) {
            unsigned char c1 = (unsigned char)p[1];
            if (c1 < 0x80 || c1 > 0xBF) return NULL;
            utf8_len = 2;
        } else if (c0 >= 0xE0 && c0 <= 0xEF) {
            unsigned char c1 = (unsigned char)p[1];
            if (c1 < 0x80 || c1 > 0xBF) return NULL;
            if (c0 == 0xE0 && c1 < 0xA0) return NULL;  // 3-Byte Overlong
            if (c0 == 0xED && c1 >= 0xA0) return NULL; // CESU-8 / UTF-16 Surrogates U+D800..U+DFFF
            unsigned char c2 = (unsigned char)p[2];
            if (c2 < 0x80 || c2 > 0xBF) return NULL;
            utf8_len = 3;
        } else if (c0 >= 0xF0 && c0 <= 0xF4) {
            unsigned char c1 = (unsigned char)p[1];
            if (c1 < 0x80 || c1 > 0xBF) return NULL;
            if (c0 == 0xF0 && c1 < 0x90) return NULL; // 4-Byte Overlong
            if (c0 == 0xF4 && c1 > 0x8F) return NULL; // > U+10FFFF
            unsigned char c2 = (unsigned char)p[2];
            if (c2 < 0x80 || c2 > 0xBF) return NULL;
            unsigned char c3 = (unsigned char)p[3];
            if (c3 < 0x80 || c3 > 0xBF) return NULL;
            utf8_len = 4;
        } else {
            return NULL;
        }

        if (out) {
            if (written + utf8_len >= limit) return NULL;
            memcpy(&out[written], p, utf8_len);
            written += utf8_len;
        } else {
            if (written + utf8_len < limit) written += utf8_len;
        }
        p += utf8_len;
    }
    if (*p != '"') return NULL;
    if (out) {
        out[written] = '\0';
    }
    return p + 1;
}

static bool is_json_delim(char c) {
    return (c == '\0' || c == ',' || c == '}' || c == ']' ||
            c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static const char *skip_json_number(const char *p) {
    if (!p || !*p) return NULL;
    if (*p == '-') p++;
    if (*p == '0') {
        p++;
        if (*p >= '0' && *p <= '9') return NULL;
    } else if (*p >= '1' && *p <= '9') {
        while (*p >= '0' && *p <= '9') p++;
    } else {
        return NULL;
    }
    if (*p == '.') {
        p++;
        if (!(*p >= '0' && *p <= '9')) return NULL;
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!(*p >= '0' && *p <= '9')) return NULL;
        while (*p >= '0' && *p <= '9') p++;
    }
    if (!is_json_delim(*p)) return NULL;
    return p;
}

static const char *skip_json_val_depth(const char *p, int depth) {
    if (depth > CIY_JSON_MAX_DEPTH) return NULL;
    p = skip_ws(p);
    if (!p || !*p) return NULL;
    if (*p == '"') {
        return parse_json_str(p, NULL, CIY_JSON_MAX_STRLEN);
    }
    if (*p == '{') {
        if (depth + 1 > CIY_JSON_MAX_DEPTH) return NULL;
        p = skip_ws(p + 1);
        if (*p == '}') return p + 1;
        while (*p) {
            if (*p != '"') return NULL;
            p = parse_json_str(p, NULL, CIY_JSON_MAX_STRLEN);
            if (!p) return NULL;
            p = skip_ws(p);
            if (*p != ':') return NULL;
            p = skip_json_val_depth(p + 1, depth + 1);
            if (!p) return NULL;
            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                if (*p == '}' || !*p) return NULL;
            } else if (*p == '}') {
                return p + 1;
            } else {
                return NULL;
            }
        }
        return NULL;
    }
    if (*p == '[') {
        if (depth + 1 > CIY_JSON_MAX_DEPTH) return NULL;
        p = skip_ws(p + 1);
        if (*p == ']') return p + 1;
        while (*p) {
            p = skip_json_val_depth(p, depth + 1);
            if (!p) return NULL;
            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                if (*p == ']' || !*p) return NULL;
            } else if (*p == ']') {
                return p + 1;
            } else {
                return NULL;
            }
        }
        return NULL;
    }
    if (strncmp(p, "true", 4) == 0 && is_json_delim(p[4])) return p + 4;
    if (strncmp(p, "false", 5) == 0 && is_json_delim(p[5])) return p + 5;
    if (strncmp(p, "null", 4) == 0 && is_json_delim(p[4])) return p + 4;
    return skip_json_number(p);
}

static const char *skip_json_val(const char *p) {
    return skip_json_val_depth(p, 0);
}

bool ciy_validate_profile_json(const char *json, CIYValidationResult *out_res) {
    if (out_res) {
        memset(out_res, 0, sizeof(*out_res));
    }
    if (!json) {
        if (out_res) {
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Profil-JSON ist NULL oder leer");
        }
        return false;
    }

    // UTF-8 BOM (\xEF\xBB\xBF) überspringen
    if ((unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB && (unsigned char)json[2] == 0xBF) {
        json += 3;
    }

    const char *p = skip_ws(json);
    if (!p || !*p) {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Leere Profildatei");
        }
        return false;
    }

    bool in_array = (*p == '[');
    if (in_array) {
        p = skip_ws(p + 1);
        if (*p == ']') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "profiles");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "JSON-Array enthält keine Profile");
            }
            return false;
        }
    } else if (*p != '{') {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "JSON-Wurzel muss ein Objekt oder Array sein");
        }
        return false;
    }

    int profile_count = 0;
    int total_keys = 0;

    while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p != '{') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwartetes Profil-Objekt '{'");
            }
            return false;
        }
        p = skip_ws(p + 1);
        profile_count++;

        int profile_keys_count = 0;
        bool has_slots = false;
        bool seen[CIY_LED_COUNT] = {false};

        while (*p && *p != '}') {
            p = skip_ws(p);
            if (*p == '}') break;

            char key[64];
            const char *next = parse_json_str(p, key, sizeof(key));
            if (!next) {
                if (out_res) {
                    snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                    snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger oder nicht terminierter String im JSON");
                }
                return false;
            }
            p = skip_ws(next);
            if (*p != ':') {
                if (out_res) {
                    snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                    snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwarteter Doppelpunkt nach Schlüssel '%s'", key);
                }
                return false;
            }
            p = skip_ws(p + 1);

            if (strcmp(key, "name") == 0) {
                if (*p != '"') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "name");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Feld 'name' muss ein String sein");
                    }
                    return false;
                }
                char val[64];
                p = parse_json_str(p, val, sizeof(val));
                if (!p) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "name");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Profilname (String-Syntaxfehler)");
                    }
                    return false;
                }
            } else if (strcmp(key, "brightness") == 0) {
                char *endptr = NULL;
                long b = strtol(p, &endptr, 10);
                if (endptr == p ||
                    !(*endptr == ',' || *endptr == '}' || *endptr == ' ' || *endptr == '\t' || *endptr == '\r' || *endptr == '\n' || *endptr == '\0') ||
                    b < 0 || b > CIY_MAX_BRIGHTNESS) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "brightness");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültige Helligkeit (erlaubt: 0..%d)", CIY_MAX_BRIGHTNESS);
                    }
                    return false;
                }
                p = skip_json_val(p);
                if (!p) return false;
            } else if (strcmp(key, "slots") == 0) {
                has_slots = true;
                if (*p != '{') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Feld 'slots' muss ein Objekt sein");
                    }
                    return false;
                }
                p = skip_ws(p + 1);
                while (*p && *p != '}') {
                    p = skip_ws(p);
                    if (*p == '}') break;

                    char slot_key[32];
                    next = parse_json_str(p, slot_key, sizeof(slot_key));
                    if (!next) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Slot-Schlüssel (String-Syntaxfehler)");
                        }
                        return false;
                    }
                    p = skip_ws(next);
                    if (*p != ':') {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwarteter Doppelpunkt nach Slot '%s'", slot_key);
                        }
                        return false;
                    }
                    p = skip_ws(p + 1);

                    char *endptr = NULL;
                    long slot = strtol(slot_key, &endptr, 10);
                    if (endptr == slot_key || *endptr != '\0' || slot < 0 || slot >= (CIY_LED_COUNT - 1)) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Matrix-Slot '%s' (erlaubt: 0..%d)", slot_key, CIY_LED_COUNT - 2);
                        }
                        return false;
                    }

                    if (*p != '"') {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Farbwert für Slot '%s' muss ein Hex-Farbstring sein", slot_key);
                        }
                        return false;
                    }
                    char hex_val[32];
                    p = parse_json_str(p, hex_val, sizeof(hex_val));
                    if (!p) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Syntaxfehler im Farbstring für Slot '%s'", slot_key);
                        }
                        return false;
                    }

                    uint8_t r = 0, g = 0, b = 0;
                    if (!ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiges Hex-Farbformat '%s' für Slot '%s' (erwartet: #RRGGBB oder #RGB)", hex_val, slot_key);
                        }
                        return false;
                    }

                    int idx = (int)(slot + 1);
                    if (!seen[idx]) {
                        seen[idx] = true;
                        profile_keys_count++;
                    }

                    p = skip_ws(p);
                    if (*p == ',') {
                        p = skip_ws(p + 1);
                        if (*p == '}' || !*p) {
                            if (out_res) {
                                snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                                snprintf(out_res->error_message, sizeof(out_res->error_message), "Trailing comma im 'slots'-Objekt unzulässig");
                            }
                            return false;
                        }
                    } else if (*p != '}') {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwartetes ',' oder '}' im 'slots'-Objekt");
                        }
                        return false;
                    }
                }
                if (*p != '}') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Objekt 'slots' nicht mit '}' geschlossen");
                    }
                    return false;
                }
                p = skip_ws(p + 1);
            } else if (key[0] >= '0' && key[0] <= '9') {
                has_slots = true;
                char *endptr = NULL;
                long slot = strtol(key, &endptr, 10);
                if (endptr == key || *endptr != '\0' || slot < 0 || slot >= (CIY_LED_COUNT - 1)) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Matrix-Slot '%s' (erlaubt: 0..%d)", key, CIY_LED_COUNT - 2);
                    }
                    return false;
                }
                if (*p != '"') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Farbwert für Slot '%s' muss ein Hex-Farbstring sein", key);
                    }
                    return false;
                }
                char hex_val[32];
                p = parse_json_str(p, hex_val, sizeof(hex_val));
                if (!p) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Syntaxfehler im Farbstring für Slot '%s'", key);
                    }
                    return false;
                }
                uint8_t r = 0, g = 0, b = 0;
                if (!ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiges Hex-Farbformat '%s' für Slot '%s' (erwartet: #RRGGBB oder #RGB)", hex_val, key);
                    }
                    return false;
                }
                int idx = (int)(slot + 1);
                if (!seen[idx]) {
                    seen[idx] = true;
                    profile_keys_count++;
                }
            } else {
                p = skip_json_val(p);
                if (!p) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Syntaxfehler im Wert von Feld '%s'", key);
                    }
                    return false;
                }
            }

            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                if (*p == '}' || !*p) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Trailing comma im Profil-Objekt unzulässig");
                    }
                    return false;
                }
            } else if (*p != '}') {
                if (out_res) {
                    snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                    snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwartetes ',' oder '}' im Profil-Objekt");
                }
                return false;
            }
        }

        if (*p != '}') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "Profil-Objekt nicht mit '}' geschlossen");
            }
            return false;
        }
        p = skip_ws(p + 1);

        if (!has_slots || profile_keys_count == 0) {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "Profil enthält keine gültigen Tasten-Slots");
            }
            return false;
        }

        total_keys += profile_keys_count;

        if (!in_array) break;

        p = skip_ws(p);
        if (*p == ',') {
            p = skip_ws(p + 1);
            if (*p == ']' || !*p) {
                if (out_res) {
                    snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                    snprintf(out_res->error_message, sizeof(out_res->error_message), "Trailing comma im Profil-Array unzulässig");
                }
                return false;
            }
        } else if (*p != ']') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "JSON-Array nicht mit ']' geschlossen");
            }
            return false;
        }
    }

    p = skip_ws(p);
    if (in_array) {
        if (*p != ']') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "JSON-Array nicht mit ']' geschlossen");
            }
            return false;
        }
        p = skip_ws(p + 1);
    }

    p = skip_ws(p);
    if (*p != '\0') {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Unerwartete Zeichen nach dem JSON-Wurzelelement");
        }
        return false;
    }

    if (profile_count == 0) {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "profiles");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Keine Profile in JSON gefunden");
        }
        return false;
    }

    if (out_res) {
        out_res->is_valid = true;
        out_res->profile_count = profile_count;
        out_res->total_keys_count = total_keys;
    }
    return true;
}

bool ciy_parse_profile_json_with_brightness(const char *json, const char *target_profile,
                                            uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                                            char *out_name, size_t max_name_len,
                                            int *out_slots_count,
                                            uint8_t *out_brightness) {
    if (!json || !rgb) return false;
    memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    if (out_name && max_name_len > 0) out_name[0] = '\0';
    if (out_slots_count) *out_slots_count = 0;
    if (out_brightness) *out_brightness = CIY_MAX_BRIGHTNESS;

    // UTF-8 BOM (\xEF\xBB\xBF) überspringen
    if ((unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB && (unsigned char)json[2] == 0xBF) {
        json += 3;
    }

    const char *p = skip_ws(json);
    if (!p || !*p) return false;
    bool in_array = (*p == '[');
    if (in_array) {
        p = skip_ws(p + 1);
        if (*p == ']') return false;
    } else if (*p != '{') {
        return false;
    }

    int profile_index = 0;
    bool found_target = false;
    uint8_t matched_rgb[CIY_PLANAR_RGB_SIZE] = {0};
    char matched_name[64] = {0};
    int matched_count = 0;
    uint8_t matched_brightness = CIY_MAX_BRIGHTNESS;

    while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p != '{') return false;
        p = skip_ws(p + 1);
        profile_index++;

        char prof_name[64] = {0};
        uint8_t prof_brightness = CIY_MAX_BRIGHTNESS;
        int valid_count = 0;
        char key[64];
        char slot_key[32];
        char hex_val[32];
        uint8_t temp_rgb[CIY_PLANAR_RGB_SIZE] = {0};
        bool seen[CIY_LED_COUNT] = {false};

        while (*p && *p != '}') {
            p = skip_ws(p);
            if (*p == '}') break;

            p = parse_json_str(p, key, sizeof(key));
            if (!p) return false;
            p = skip_ws(p);
            if (*p != ':') return false;
            p = skip_ws(p + 1);

            if (strcmp(key, "name") == 0) {
                if (*p == '"') {
                    p = parse_json_str(p, prof_name, sizeof(prof_name));
                    if (!p) return false;
                } else {
                    p = skip_json_val(p);
                    if (!p) return false;
                }
            } else if (strcmp(key, "brightness") == 0) {
                char *endptr = NULL;
                errno = 0;
                long b = strtol(p, &endptr, 10);
                if (endptr != p && errno != ERANGE && is_json_delim(*endptr) && b >= 0 && b <= CIY_MAX_BRIGHTNESS) {
                    prof_brightness = (uint8_t)b;
                }
                p = skip_json_val(p);
                if (!p) return false;
            } else if (strcmp(key, "slots") == 0) {
                if (*p != '{') {
                    p = skip_json_val(p);
                    if (!p) return false;
                } else {
                    p = skip_ws(p + 1);
                    while (*p && *p != '}') {
                        p = skip_ws(p);
                        if (*p == '}') break;

                        p = parse_json_str(p, slot_key, sizeof(slot_key));
                        if (!p) return false;
                        p = skip_ws(p);
                        if (*p != ':') return false;
                        p = skip_ws(p + 1);

                        if (*p == '"') {
                            p = parse_json_str(p, hex_val, sizeof(hex_val));
                            if (!p) return false;

                            uint8_t r = 0, g = 0, b = 0;
                            if (ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                                char *endptr = NULL;
                                errno = 0;
                                long slot = strtol(slot_key, &endptr, 10);
                                if (endptr != slot_key && *endptr == '\0' && errno != ERANGE && slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
                                    int idx = (int)(slot + 1);
                                    temp_rgb[idx] = r;
                                    temp_rgb[CIY_LED_COUNT + idx] = g;
                                    temp_rgb[2 * CIY_LED_COUNT + idx] = b;
                                    if (!seen[idx]) {
                                        seen[idx] = true;
                                        valid_count++;
                                    }
                                }
                            }
                        } else {
                            p = skip_json_val(p);
                            if (!p) return false;
                        }

                        p = skip_ws(p);
                        if (*p == ',') {
                            p = skip_ws(p + 1);
                            if (*p == '}' || !*p) return false;
                        } else if (*p != '}') {
                            return false;
                        }
                    }
                    if (*p != '}') return false;
                    p = skip_ws(p + 1);
                }
            } else if (key[0] >= '0' && key[0] <= '9') {
                if (*p == '"') {
                    p = parse_json_str(p, hex_val, sizeof(hex_val));
                    if (!p) return false;

                    uint8_t r = 0, g = 0, b = 0;
                    if (ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                        char *endptr = NULL;
                        errno = 0;
                        long slot = strtol(key, &endptr, 10);
                        if (endptr != key && *endptr == '\0' && errno != ERANGE && slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
                            int idx = (int)(slot + 1);
                            temp_rgb[idx] = r;
                            temp_rgb[CIY_LED_COUNT + idx] = g;
                            temp_rgb[2 * CIY_LED_COUNT + idx] = b;
                            if (!seen[idx]) {
                                seen[idx] = true;
                                valid_count++;
                            }
                        }
                    }
                } else {
                    p = skip_json_val(p);
                    if (!p) return false;
                }
            } else {
                p = skip_json_val(p);
                if (!p) return false;
            }

            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                if (*p == '}' || !*p) return false;
            } else if (*p != '}') {
                return false;
            }
        }

        if (*p != '}') return false;
        p = skip_ws(p + 1);

        if (!found_target) {
            bool match = false;
            if (!target_profile || target_profile[0] == '\0') {
                match = true;
            } else {
                if (prof_name[0] && ciy_ascii_strcasecmp(prof_name, target_profile) == 0) {
                    match = true;
                } else {
                    char *endptr = NULL;
                    errno = 0;
                    long req_idx = strtol(target_profile, &endptr, 10);
                    if (endptr != target_profile && *endptr == '\0' && errno != ERANGE && req_idx == profile_index) {
                        match = true;
                    }
                }
            }

            if (match && valid_count > 0) {
                memcpy(matched_rgb, temp_rgb, CIY_PLANAR_RGB_SIZE);
                strncpy(matched_name, prof_name, sizeof(matched_name) - 1);
                matched_name[sizeof(matched_name) - 1] = '\0';
                matched_count = valid_count;
                matched_brightness = prof_brightness;
                found_target = true;
            }
        }

        if (!in_array) break;

        p = skip_ws(p);
        if (*p == ',') {
            p = skip_ws(p + 1);
            if (*p == ']' || !*p) return false;
        } else if (*p != ']') {
            return false;
        }
    }

    p = skip_ws(p);
    if (in_array) {
        if (*p != ']') return false;
        p = skip_ws(p + 1);
    }
    p = skip_ws(p);
    if (*p != '\0') return false;

    if (!found_target) return false;

    memcpy(rgb, matched_rgb, CIY_PLANAR_RGB_SIZE);
    if (out_name && max_name_len > 0) {
        strncpy(out_name, matched_name, max_name_len - 1);
        out_name[max_name_len - 1] = '\0';
    }
    if (out_slots_count) *out_slots_count = matched_count;
    if (out_brightness) *out_brightness = matched_brightness;
    return true;
}

bool ciy_parse_profile_json(const char *json, const char *target_profile,
                            uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                            char *out_name, size_t max_name_len,
                            int *out_slots_count) {
    return ciy_parse_profile_json_with_brightness(json, target_profile, rgb, out_name, max_name_len, out_slots_count, NULL);
}
