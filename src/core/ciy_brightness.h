#ifndef CIY_BRIGHTNESS_H
#define CIY_BRIGHTNESS_H

// CIY X79 Individuelle Tasten-Helligkeit Engine (Pure C11 Header-Only)
// Erlaubt das Skalieren einzelner Farbkanäle sowie planarer RGB-Puffer (378 Bytes)
// über Per-LED- (0..125) oder Per-Slot-Masken (0..124).
// Absolut frei von IOKit, Cocoa oder OS-Frameworks (Zero OS-Lock-in).

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ciy_protocol.h"
#include "../layouts/ciy_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CIY_BRIGHTNESS_PERCENT_MAX  100
#define CIY_BRIGHTNESS_SLOT_COUNT   (CIY_MAX_MATRIX_SLOT + 1)

/// Skaliert einen einzelnen Farbkanal (0..255) mit einem Prozentwert (0..100 %).
/// Werte > 100 % werden auf 100 % geklemmt.
/// Verwendet kaufmännische Rundung: ((uint32_t)channel * percent + 50) / 100.
CIY_INLINE uint8_t ciy_brightness_scale_channel(uint8_t channel, uint8_t percent) {
    uint32_t p = (percent > CIY_BRIGHTNESS_PERCENT_MAX) ? (uint32_t)CIY_BRIGHTNESS_PERCENT_MAX : (uint32_t)percent;
    return (uint8_t)(((uint32_t)channel * p + 50u) / 100u);
}

/// Skaliert alle drei Farbebenen (R, G, B) im planaren 378-Byte-RGB-Puffer anhand einer Per-LED-Maske.
/// planar_rgb: Puffer der Größe CIY_PLANAR_RGB_SIZE (3 * CIY_LED_COUNT = 378 Bytes).
/// per_led_percent: Prozentwerte für jeden LED-Index (0..CIY_LED_COUNT-1).
/// Gibt false zurück, falls planar_rgb oder per_led_percent NULL ist.
CIY_INLINE bool ciy_brightness_apply_planar(uint8_t planar_rgb[CIY_PLANAR_RGB_SIZE],
                                           const uint8_t per_led_percent[CIY_LED_COUNT]) {
    if (!planar_rgb || !per_led_percent) {
        return false;
    }

    for (size_t i = 0; i < CIY_LED_COUNT; i++) {
        uint8_t pct = per_led_percent[i];
        if (pct >= CIY_BRIGHTNESS_PERCENT_MAX) {
            continue; // 100 % oder mehr -> unverändert
        }
        planar_rgb[i] = ciy_brightness_scale_channel(planar_rgb[i], pct);
        planar_rgb[CIY_LED_COUNT + i] = ciy_brightness_scale_channel(planar_rgb[CIY_LED_COUNT + i], pct);
        planar_rgb[(2 * CIY_LED_COUNT) + i] = ciy_brightness_scale_channel(planar_rgb[(2 * CIY_LED_COUNT) + i], pct);
    }

    return true;
}

/// Skaliert den planaren 378-Byte-RGB-Puffer anhand einer Slot-basierten Maske (Slots 0..124).
/// Löst über das Hardware-verifizierte ISO-DE-Layout die LED-Indizes der Tasten auf.
/// Slots ohne physische Taste im Layout werden defensiv ignoriert.
/// Bei NULL-Pointern oder ungültigen LED-Indizes im Layout wird false zurückgegeben
/// und der Puffer bleibt unberührt.
CIY_INLINE bool ciy_brightness_apply_slots(uint8_t planar_rgb[CIY_PLANAR_RGB_SIZE],
                                          const uint8_t per_slot_percent[CIY_BRIGHTNESS_SLOT_COUNT]) {
    if (!planar_rgb || !per_slot_percent) {
        return false;
    }

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    if (!layout) {
        return false;
    }

    // 1. Phase: Per-LED-Maske vorbereiten (Standard 100 %)
    uint8_t led_mask[CIY_LED_COUNT];
    for (size_t i = 0; i < CIY_LED_COUNT; i++) {
        led_mask[i] = CIY_BRIGHTNESS_PERCENT_MAX;
    }

    for (int slot = 0; slot <= CIY_MAX_MATRIX_SLOT; slot++) {
        uint8_t pct = per_slot_percent[slot];
        if (pct >= CIY_BRIGHTNESS_PERCENT_MAX) {
            continue;
        }

        const CIYLogicalKey *key = ciy_layout_find_key_by_slot(layout, slot);
        if (!key) {
            // Dünn besetztes Layout: Slot hat keine Taste -> ignorieren
            continue;
        }

        if (key->led_index < 0 || key->led_index >= CIY_LED_COUNT) {
            // Defekter Layout-Index: defensiver Abbruch ohne Mutation
            return false;
        }

        led_mask[key->led_index] = pct;
    }

    // 2. Phase: Maske atomar auf den Planar-Puffer anwenden
    return ciy_brightness_apply_planar(planar_rgb, led_mask);
}

#ifdef __cplusplus
}
#endif

#endif // CIY_BRIGHTNESS_H
