#ifndef CIY_TEMPLATES_H
#define CIY_TEMPLATES_H

// CIY X79 Profil-Vorlagen-Engine (Pure C11 Header-Only)
// Erlaubt das Instanziieren vorkonfigurierter Beleuchtungs-Vorlagen
// für das hardware-verifizierte ISO-DE Layout der CIY X79.
// Absolut frei von IOKit, Cocoa oder OS-Frameworks (Zero OS-Lock-in).

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "ciy_protocol.h"
#include "ciy_brightness.h"
#include "ciy_gradient.h"
#include "../layouts/ciy_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Maximale UTF-8-Bytelänge für Profilnamen ohne Nullterminator (kompatibel mit char[64] in ciy_hid.c).
#define CIY_TEMPLATE_MAX_NAME_LEN 63

/// Repräsentiert die semantische Rolle einer Taste innerhalb einer Vorlage.
typedef enum {
    CIY_TEMPLATE_ROLE_OFF = 0,    // Taste aus (#000000, auch für Matrix-Löcher)
    CIY_TEMPLATE_ROLE_BG,         // Hintergrundfarbe
    CIY_TEMPLATE_ROLE_ACCENT,     // Primäre Akzentfarbe
    CIY_TEMPLATE_ROLE_SECONDARY,  // Sekundärfarbe (z. B. Waffenreihe oder Akzent-Mischung)
    CIY_TEMPLATE_ROLE_DIM         // Gedimmte Alphanumerik / Orientierung
} CIYTemplateRole;

/// Beschreibung einer vorkonfigurierten Profil-Vorlage.
typedef struct {
    const char *id;             // Eindeutige, stabile ID (z. B. "wasd-only")
    const char *name;           // Anzeigename (z. B. "WASD Only")
    const char *description;    // Kurzbeschreibung des Einsatzzwecks
    uint8_t default_accent[3];  // Standard-Akzentfarbe (RGB)
    uint8_t default_bg[3];      // Standard-Hintergrundfarbe (RGB)
} CIYTemplate;

/// Vordefinierte Vorlagen für CIY X79 ISO-DE.
static const CIYTemplate CIY_PREDEFINED_TEMPLATES[] = {
    {
        "wasd-only",
        "WASD Only",
        "Nur W, A, S, D akzentuiert, alle übrigen Tasten aus",
        {0x00, 0xFF, 0xCC}, // Leuchtendes Cyan-Grün (#00FFCC)
        {0x00, 0x00, 0x00}  // Aus (#000000)
    },
    {
        "arrows-only",
        "Arrows Only",
        "Nur die 4 Pfeiltasten akzentuiert, Rest aus",
        {0xFF, 0xCC, 0x00}, // Gold/Gelb (#FFCC00)
        {0x00, 0x00, 0x00}  // Aus (#000000)
    },
    {
        "home-row",
        "Home Row (Grundstellung)",
        "ISO-DE Grundstellung (A S D F + J K L Ö sowie Space) zur blinden Orientierung",
        {0x00, 0xAA, 0xFF}, // Klares Azurblau (#00AAFF)
        {0x00, 0x00, 0x00}  // Aus (#000000)
    },
    {
        "game-fps",
        "Game FPS",
        "WASD Hauptakzent, Waffen-/Aktionsreihe Sekundärfarbe, Rest aus/gedimmt",
        {0xFF, 0x00, 0x33}, // Signalrot (#FF0033)
        {0x00, 0x00, 0x00}  // Aus (#000000)
    },
    {
        "coding",
        "Coding",
        "Alphanumerik dezent gedimmt, Modifikatoren und Navigation klar abgesetzt",
        {0x00, 0xD4, 0xFF}, // Helles Cyan (#00D4FF)
        {0x00, 0x00, 0x00}  // Aus (#000000)
    },
    {
        "night",
        "Night Mode",
        "Augenschonendes, stark gedimmtes Bernstein/Tiefrot ohne Blendwirkung",
        {0x80, 0x28, 0x00}, // Gedimmtes Bernstein (#802800)
        {0x00, 0x00, 0x00}  // Aus (#000000)
    }
};

/// Gibt die Gesamtzahl vorkonfigurierter Vorlagen zurück.
CIY_INLINE size_t ciy_template_count(void) {
    return sizeof(CIY_PREDEFINED_TEMPLATES) / sizeof(CIY_PREDEFINED_TEMPLATES[0]);
}

/// Ruft eine Vorlage anhand ihres 0-basierten Index ab (NULL bei out-of-bounds).
CIY_INLINE const CIYTemplate *ciy_template_get(size_t idx) {
    if (idx >= ciy_template_count()) {
        return NULL;
    }
    return &CIY_PREDEFINED_TEMPLATES[idx];
}

/// Sucht eine Vorlage anhand ihrer stabilen ID (exakter Vergleich, NULL bei Nichtfund).
CIY_INLINE const CIYTemplate *ciy_template_find(const char *id) {
    if (!id || id[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < ciy_template_count(); i++) {
        if (strcmp(CIY_PREDEFINED_TEMPLATES[i].id, id) == 0) {
            return &CIY_PREDEFINED_TEMPLATES[i];
        }
    }
    return NULL;
}

/// Ermittelt die funktionale Rolle eines Matrix-Slots (0..124) für eine Vorlage.
/// Wichtig: Matrix-Löcher (Slots ohne physische Taste im ISO-DE Layout)
/// erhalten zwingend CIY_TEMPLATE_ROLE_OFF.
CIY_INLINE CIYTemplateRole ciy_template_get_slot_role(const CIYTemplate *tpl, int matrix_slot) {
    if (!tpl || matrix_slot < 0 || matrix_slot > CIY_MAX_MATRIX_SLOT) {
        return CIY_TEMPLATE_ROLE_OFF;
    }

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    if (!layout || !ciy_layout_find_key_by_slot(layout, matrix_slot)) {
        return CIY_TEMPLATE_ROLE_OFF;
    }

    if (strcmp(tpl->id, "wasd-only") == 0) {
        // W=14, A=9, S=15, D=21
        if (matrix_slot == 14 || matrix_slot == 9 || matrix_slot == 15 || matrix_slot == 21) {
            return CIY_TEMPLATE_ROLE_ACCENT;
        }
        return CIY_TEMPLATE_ROLE_BG;
    }

    if (strcmp(tpl->id, "arrows-only") == 0) {
        // ▲=88, ▼=89, ◄=83, ►=95
        if (matrix_slot == 88 || matrix_slot == 89 || matrix_slot == 83 || matrix_slot == 95) {
            return CIY_TEMPLATE_ROLE_ACCENT;
        }
        return CIY_TEMPLATE_ROLE_BG;
    }

    if (strcmp(tpl->id, "home-row") == 0) {
        // Grundstellung: A=9, S=15, D=21, F=27 | J=45, K=51, L=57, Ö=63
        if (matrix_slot == 9 || matrix_slot == 15 || matrix_slot == 21 || matrix_slot == 27 ||
            matrix_slot == 45 || matrix_slot == 51 || matrix_slot == 57 || matrix_slot == 63) {
            return CIY_TEMPLATE_ROLE_ACCENT;
        }
        // Space dezent als taktiler Nullpunkt
        if (matrix_slot == 35) {
            return CIY_TEMPLATE_ROLE_SECONDARY;
        }
        return CIY_TEMPLATE_ROLE_BG;
    }

    if (strcmp(tpl->id, "game-fps") == 0) {
        // Hauptakzent: WASD
        if (matrix_slot == 14 || matrix_slot == 9 || matrix_slot == 15 || matrix_slot == 21) {
            return CIY_TEMPLATE_ROLE_ACCENT;
        }
        // Waffen- & Aktionsreihe:
        // 1..5: 7, 13, 19, 25, 31
        // R=26, E=20, F=27
        // Space=35
        // Shift=4, 82 (beidseitig)
        // Ctrl=5, 80 (beidseitig)
        // Esc=1
        if (matrix_slot == 7 || matrix_slot == 13 || matrix_slot == 19 || matrix_slot == 25 || matrix_slot == 31 ||
            matrix_slot == 26 || matrix_slot == 20 || matrix_slot == 27 ||
            matrix_slot == 35 ||
            matrix_slot == 4 || matrix_slot == 82 ||
            matrix_slot == 5 || matrix_slot == 80 ||
            matrix_slot == 1) {
            return CIY_TEMPLATE_ROLE_SECONDARY;
        }
        return CIY_TEMPLATE_ROLE_BG;
    }

    if (strcmp(tpl->id, "coding") == 0) {
        // Modifikatoren + Esc, Tab, Enter, Backspace, Pfeile klar abgesetzt
        // Esc=1, Tab=2, Caps=3, Shift=4,82, Ctrl=5,80, Win=11, Alt=17, AltGr=53, Fn=77
        // Backspace=79, Enter=81
        // Pfeile: ▲=88, ▼=89, ◄=83, ►=95
        if (matrix_slot == 1 || matrix_slot == 2 || matrix_slot == 3 ||
            matrix_slot == 4 || matrix_slot == 82 ||
            matrix_slot == 5 || matrix_slot == 80 ||
            matrix_slot == 11 || matrix_slot == 17 || matrix_slot == 53 || matrix_slot == 77 ||
            matrix_slot == 79 || matrix_slot == 81 ||
            matrix_slot == 88 || matrix_slot == 89 || matrix_slot == 83 || matrix_slot == 95) {
            return CIY_TEMPLATE_ROLE_ACCENT;
        }
        // Alphanumerik dezent gedimmt
        return CIY_TEMPLATE_ROLE_DIM;
    }

    if (strcmp(tpl->id, "night") == 0) {
        // Schreib- und Orientierungstasten:
        // Zahlenreihe: 7, 13, 19, 25, 31, 37, 43, 49, 55, 61, 67, 73
        // Q-Reihe: 8, 14, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74
        // A-Reihe: 9, 15, 21, 27, 33, 39, 45, 51, 57, 63, 69, 75
        // Y-Reihe: 76, 10, 16, 22, 28, 34, 40, 46, 52, 58, 64
        // Space=35, Enter=81, Backspace=79, Pfeile: 88, 89, 83, 95
        if (matrix_slot == 7 || matrix_slot == 13 || matrix_slot == 19 || matrix_slot == 25 ||
            matrix_slot == 31 || matrix_slot == 37 || matrix_slot == 43 || matrix_slot == 49 ||
            matrix_slot == 55 || matrix_slot == 61 || matrix_slot == 67 || matrix_slot == 73 ||
            matrix_slot == 8 || matrix_slot == 14 || matrix_slot == 20 || matrix_slot == 26 ||
            matrix_slot == 32 || matrix_slot == 38 || matrix_slot == 44 || matrix_slot == 50 ||
            matrix_slot == 56 || matrix_slot == 62 || matrix_slot == 68 || matrix_slot == 74 ||
            matrix_slot == 9 || matrix_slot == 15 || matrix_slot == 21 || matrix_slot == 27 ||
            matrix_slot == 33 || matrix_slot == 39 || matrix_slot == 45 || matrix_slot == 51 ||
            matrix_slot == 57 || matrix_slot == 63 || matrix_slot == 69 || matrix_slot == 75 ||
            matrix_slot == 76 || matrix_slot == 10 || matrix_slot == 16 || matrix_slot == 22 ||
            matrix_slot == 28 || matrix_slot == 34 || matrix_slot == 40 || matrix_slot == 46 ||
            matrix_slot == 52 || matrix_slot == 58 || matrix_slot == 64 ||
            matrix_slot == 35 || matrix_slot == 81 || matrix_slot == 79 ||
            matrix_slot == 88 || matrix_slot == 89 || matrix_slot == 83 || matrix_slot == 95) {
            return CIY_TEMPLATE_ROLE_ACCENT;
        }
        return CIY_TEMPLATE_ROLE_BG;
    }

    return CIY_TEMPLATE_ROLE_BG;
}

/// Erzeugt die 125 Slot-Farben (0..124) einer Vorlage.
/// Optionale Parameter custom_accent und custom_bg erlauben das Überschreiben
/// der Standardfarben (jeweils uint8_t[3] oder NULL für Standard).
/// Unbelegte Matrix-Löcher erhalten garantiert {0, 0, 0}.
CIY_INLINE bool ciy_template_generate_slots(const CIYTemplate *tpl,
                                           const uint8_t *custom_accent,
                                           const uint8_t *custom_bg,
                                           uint8_t out_slots[CIY_MAX_MATRIX_SLOT + 1][3]) {
    if (out_slots) {
        memset(out_slots, 0, sizeof(uint8_t) * (CIY_MAX_MATRIX_SLOT + 1) * 3);
    }
    if (!tpl || !out_slots) {
        return false;
    }

    const uint8_t *accent = custom_accent ? custom_accent : tpl->default_accent;
    const uint8_t *bg = custom_bg ? custom_bg : tpl->default_bg;

    // Vordefinierte Sekundärfarbe bzw. konsistente Ableitung
    uint8_t sec[3];
    if (!custom_accent && strcmp(tpl->id, "game-fps") == 0) {
        sec[0] = 0xFF; sec[1] = 0x88; sec[2] = 0x00; // Orange (#FF8800) für Waffenreihe
    } else {
        for (int c = 0; c < 3; c++) {
            sec[c] = ciy_gradient_lerp_channel(bg[c], accent[c], 0.65f);
        }
    }

    // Gedimmte Farbe (für Alphanumerik bei coding)
    uint8_t dim[3];
    for (int c = 0; c < 3; c++) {
        dim[c] = ciy_gradient_lerp_channel(bg[c], accent[c], 0.25f);
    }

    // Für night mode: falls benutzerdefinierter Akzent übergeben wurde,
    // blendungsarm auf maximal 40 % skalieren
    uint8_t night_accent[3];
    if (strcmp(tpl->id, "night") == 0 && custom_accent) {
        for (int c = 0; c < 3; c++) {
            night_accent[c] = ciy_brightness_scale_channel(accent[c], 40);
        }
    } else {
        memcpy(night_accent, accent, 3);
    }

    for (int slot = 0; slot <= CIY_MAX_MATRIX_SLOT; slot++) {
        CIYTemplateRole role = ciy_template_get_slot_role(tpl, slot);
        switch (role) {
            case CIY_TEMPLATE_ROLE_OFF:
                out_slots[slot][0] = 0;
                out_slots[slot][1] = 0;
                out_slots[slot][2] = 0;
                break;
            case CIY_TEMPLATE_ROLE_BG:
                out_slots[slot][0] = bg[0];
                out_slots[slot][1] = bg[1];
                out_slots[slot][2] = bg[2];
                break;
            case CIY_TEMPLATE_ROLE_ACCENT:
                out_slots[slot][0] = night_accent[0];
                out_slots[slot][1] = night_accent[1];
                out_slots[slot][2] = night_accent[2];
                break;
            case CIY_TEMPLATE_ROLE_SECONDARY:
                out_slots[slot][0] = sec[0];
                out_slots[slot][1] = sec[1];
                out_slots[slot][2] = sec[2];
                break;
            case CIY_TEMPLATE_ROLE_DIM:
                out_slots[slot][0] = dim[0];
                out_slots[slot][1] = dim[1];
                out_slots[slot][2] = dim[2];
                break;
        }
    }

    return true;
}

/// Erzeugt den 378-Byte Planar-RGB-Puffer für die Vorlage.
/// Nutzt exakt das Hardware-verifizierte ISO-DE Layout zur Auflösung der
/// Report-0x0B LED-Indizes (led = slot + 1).
CIY_INLINE bool ciy_template_generate_planar(const CIYTemplate *tpl,
                                             const uint8_t *custom_accent,
                                             const uint8_t *custom_bg,
                                             uint8_t out_planar_rgb[CIY_PLANAR_RGB_SIZE]) {
    if (out_planar_rgb) {
        memset(out_planar_rgb, 0, CIY_PLANAR_RGB_SIZE);
    }
    if (!tpl || !out_planar_rgb) {
        return false;
    }

    uint8_t slots[CIY_MAX_MATRIX_SLOT + 1][3];
    if (!ciy_template_generate_slots(tpl, custom_accent, custom_bg, slots)) {
        return false;
    }

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    if (!layout || !layout->keys) {
        return false;
    }

    for (size_t i = 0; i < layout->key_count; i++) {
        int slot = layout->keys[i].matrix_slot;
        int led = layout->keys[i].led_index;
        if (slot < 0 || slot > CIY_MAX_MATRIX_SLOT) continue;
        if (led < 0 || led >= CIY_LED_COUNT) continue;

        out_planar_rgb[led] = slots[slot][0];
        out_planar_rgb[CIY_LED_COUNT + led] = slots[slot][1];
        out_planar_rgb[(2 * CIY_LED_COUNT) + led] = slots[slot][2];
    }

    return true;
}

/// Serialisiert die Vorlage als valides CIY-Profil-JSON.
/// Format ist 100 % kompatibel mit ciy_ctl validate-profile und ciy_ctl load:
/// {
///   "name": "...",
///   "slots": {
///     "0": "#000000",
///     ...
///     "124": "#000000"
///   }
/// }
CIY_INLINE bool ciy_template_to_json(const CIYTemplate *tpl,
                                     const char *custom_name,
                                     const uint8_t *custom_accent,
                                     const uint8_t *custom_bg,
                                     char *out_json,
                                     size_t max_len) {
    if (!tpl || !out_json || max_len < 512) {
        if (out_json && max_len > 0) out_json[0] = '\0';
        return false;
    }

    uint8_t slots[CIY_MAX_MATRIX_SLOT + 1][3];
    if (!ciy_template_generate_slots(tpl, custom_accent, custom_bg, slots)) {
        out_json[0] = '\0';
        return false;
    }

    const char *name = (custom_name && custom_name[0] != '\0') ? custom_name : tpl->name;
    if (strlen(name) > CIY_TEMPLATE_MAX_NAME_LEN) {
        out_json[0] = '\0';
        return false;
    }

    size_t pos = 0;
    int n = snprintf(out_json + pos, max_len - pos, "{\n  \"name\": \"");
    if (n < 0 || (size_t)n >= max_len - pos) {
        out_json[0] = '\0';
        return false;
    }
    pos += (size_t)n;

    for (const char *p = name; *p != '\0'; p++) {
        char esc[8];
        size_t esc_len = 0;
        unsigned char uc = (unsigned char)*p;

        if (uc == '"') {
            esc[0] = '\\'; esc[1] = '"'; esc_len = 2;
        } else if (uc == '\\') {
            esc[0] = '\\'; esc[1] = '\\'; esc_len = 2;
        } else if (uc == '\b') {
            esc[0] = '\\'; esc[1] = 'b'; esc_len = 2;
        } else if (uc == '\f') {
            esc[0] = '\\'; esc[1] = 'f'; esc_len = 2;
        } else if (uc == '\n') {
            esc[0] = '\\'; esc[1] = 'n'; esc_len = 2;
        } else if (uc == '\r') {
            esc[0] = '\\'; esc[1] = 'r'; esc_len = 2;
        } else if (uc == '\t') {
            esc[0] = '\\'; esc[1] = 't'; esc_len = 2;
        } else if (uc < 0x20) {
            int n_esc = snprintf(esc, sizeof(esc), "\\u%04x", (unsigned int)uc);
            if (n_esc < 0 || (size_t)n_esc >= sizeof(esc)) {
                out_json[0] = '\0';
                return false;
            }
            esc_len = (size_t)n_esc;
        } else {
            esc[0] = (char)uc;
            esc_len = 1;
        }

        if (pos + esc_len >= max_len) {
            out_json[0] = '\0';
            return false;
        }
        memcpy(out_json + pos, esc, esc_len);
        pos += esc_len;
    }

    n = snprintf(out_json + pos, max_len - pos, "\",\n  \"slots\": {\n");
    if (n < 0 || (size_t)n >= max_len - pos) {
        out_json[0] = '\0';
        return false;
    }
    pos += (size_t)n;

    for (int slot = 0; slot <= CIY_MAX_MATRIX_SLOT; slot++) {
        const char *comma = (slot == CIY_MAX_MATRIX_SLOT) ? "" : ",";
        n = snprintf(out_json + pos, max_len - pos,
                     "    \"%d\": \"#%02X%02X%02X\"%s\n",
                     slot,
                     slots[slot][0], slots[slot][1], slots[slot][2],
                     comma);
        if (n < 0 || (size_t)n >= max_len - pos) {
            out_json[0] = '\0';
            return false;
        }
        pos += (size_t)n;
    }

    n = snprintf(out_json + pos, max_len - pos, "  }\n}\n");
    if (n < 0 || (size_t)n >= max_len - pos) {
        out_json[0] = '\0';
        return false;
    }
    pos += (size_t)n;

    return true;
}

#ifdef __cplusplus
}
#endif

#endif // CIY_TEMPLATES_H
