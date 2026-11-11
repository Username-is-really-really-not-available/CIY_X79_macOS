#ifndef CIY_GRADIENT_H
#define CIY_GRADIENT_H

// CIY X79 Linear-Gradient Farbfeld-Engine (Pure C11 Header-Only)
// Berechnet stufenlose lineare sRGB-Farbverläufe über die physikalischen
// U-Koordinaten (0.0..16.0) und Tastenreihen (0..4) des ISO-DE 65% Layouts.

#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "ciy_protocol.h"
#include "../layouts/ciy_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CIY_GRADIENT_HORIZONTAL = 0, // Links nach Rechts
    CIY_GRADIENT_VERTICAL        // Oben nach Unten
} CIYGradientDirection;

/// Lineare Interpolation eines 8-Bit-Farbkanals mit Rundung auf das nächste Integer.
CIY_INLINE uint8_t ciy_gradient_lerp_channel(uint8_t c1, uint8_t c2, float t) {
    if (isnan(t) || t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    float v = (float)c1 + t * ((float)c2 - (float)c1);
    int rounded = (int)(v + 0.5f);
    if (rounded < 0) rounded = 0;
    if (rounded > 255) rounded = 255;
    return (uint8_t)rounded;
}

/// Ermittelt das Zentrum in Key-Units (U) und die Zeile (0..4) für einen Matrix-Slot.
/// Gibt false zurück, falls der Slot im Layout nicht vorhanden ist.
CIY_INLINE bool ciy_gradient_get_slot_coords(int slot, float *out_u_center, float *out_row) {
    if (out_u_center) *out_u_center = 0.0f;
    if (out_row) *out_row = 0.0f;
    if (slot < 0 || slot > CIY_MAX_MATRIX_SLOT) return false;

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    if (!layout || !layout->surfaces) return false;

    int currentRow = -1;
    float current_u = 0.0f;
    float found_u = -1.0f;
    float found_row = -1.0f;
    int surface_count_for_slot = 0;

    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *surf = &layout->surfaces[i];
        if (surf->row != currentRow) {
            currentRow = surf->row;
            current_u = 0.0f;
        }

        float center_u = current_u + (surf->width_u / 2.0f);
        if (surf->matrix_slot == slot) {
            if (surface_count_for_slot == 0) {
                found_u = center_u;
                found_row = (float)surf->row;
            } else {
                // Bei mehrflächigen Tasten (z. B. ISO Enter Slot 81) Mittelwert bilden
                found_u = (found_u + center_u) / 2.0f;
                found_row = (found_row + (float)surf->row) / 2.0f;
            }
            surface_count_for_slot++;
        }
        current_u += surf->width_u;
    }

    if (surface_count_for_slot > 0) {
        if (out_u_center) *out_u_center = found_u;
        if (out_row) *out_row = found_row;
        return true;
    }

    return false;
}

/// Berechnet die interpolierte Farbe für eine Einzeltaste basierend auf Start- und Endfarbe.
CIY_INLINE bool ciy_gradient_calculate_slot_color(int slot,
                                                  uint8_t r1, uint8_t g1, uint8_t b1,
                                                  uint8_t r2, uint8_t g2, uint8_t b2,
                                                  CIYGradientDirection direction,
                                                  uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    if (out_r) *out_r = 0;
    if (out_g) *out_g = 0;
    if (out_b) *out_b = 0;
    if (!out_r || !out_g || !out_b) return false;

    float u_center = 0.0f;
    float row = 0.0f;
    if (!ciy_gradient_get_slot_coords(slot, &u_center, &row)) {
        return false;
    }

    float t = 0.0f;
    if (direction == CIY_GRADIENT_HORIZONTAL) {
        // Tastenbreite der CIY X79 beträgt ca. 16.0 Key-Units (Esc bis Entf / Pfeil rechts)
        // Normalisierung: Esc (~0.5 U) bis Entf/Pfeil rechts (~15.5 U)
        // Verwende Bereich 0.5 bis 15.5 für vollen Farbumfang von Rand zu Rand
        float min_u = 0.5f;
        float max_u = 15.5f;
        t = (u_center - min_u) / (max_u - min_u);
    } else {
        // Vertikal: Reihe 0 (Esc-Reihe) bis Reihe 4 (Leertasten-Reihe)
        float max_row = 4.0f;
        t = row / max_row;
    }

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    *out_r = ciy_gradient_lerp_channel(r1, r2, t);
    *out_g = ciy_gradient_lerp_channel(g1, g2, t);
    *out_b = ciy_gradient_lerp_channel(b1, b2, t);

    return true;
}

/// Erzeugt einen vollständigen 378-Byte Planar-RGB-Puffer für die CIY X79.
/// Alle im Layout vorhandenen Tasten werden gemäß dem linearen Verlauf eingefärbt,
/// ungenutzte Matrix-Slots bleiben 0 (aus).
CIY_INLINE bool ciy_gradient_generate_planar(uint8_t out_planar_rgb[CIY_PLANAR_RGB_SIZE],
                                            uint8_t r1, uint8_t g1, uint8_t b1,
                                            uint8_t r2, uint8_t g2, uint8_t b2,
                                            CIYGradientDirection direction) {
    if (!out_planar_rgb) return false;
    memset(out_planar_rgb, 0, CIY_PLANAR_RGB_SIZE);

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    if (!layout || !layout->keys) return false;

    for (size_t i = 0; i < layout->key_count; i++) {
        int slot = layout->keys[i].matrix_slot;
        int led = layout->keys[i].led_index;
        if (led < 0 || led >= CIY_LED_COUNT) continue;

        uint8_t r = 0, g = 0, b = 0;
        if (ciy_gradient_calculate_slot_color(slot, r1, g1, b1, r2, g2, b2, direction, &r, &g, &b)) {
            out_planar_rgb[led] = r;
            out_planar_rgb[CIY_LED_COUNT + led] = g;
            out_planar_rgb[(2 * CIY_LED_COUNT) + led] = b;
        }
    }

    return true;
}

#ifdef __cplusplus
}
#endif

#endif // CIY_GRADIENT_H
