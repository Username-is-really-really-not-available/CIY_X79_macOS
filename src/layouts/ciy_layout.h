#ifndef CIY_LAYOUT_H
#define CIY_LAYOUT_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Repräsentiert eine eindeutige logische/physische Taste auf der Matrix.
typedef struct {
    int matrix_slot;        // Matrix-Slot (0..124 bei CIY X79)
    int led_index;          // USB-HID LED-Index im Report 0x0B (matrix_slot + 1 bei CIY X79)
    const char *name;       // Eindeutiger Primärname der Taste (z. B. "Esc", "W", "↵ Enter")
} CIYLogicalKey;

/// Repräsentiert eine grafische Zeichen- und Hit-Test-Fläche im Tastaturlayout.
/// Eine logische Taste kann aus mehreren Flächen bestehen (z. B. 2 Flächen für ISO Enter).
typedef struct {
    int matrix_slot;        // Referenz auf CIYLogicalKey.matrix_slot
    const char *label;      // Visuelle Tastenbeschriftung (z. B. "↵ Enter", "↵", "Q")
    float width_u;          // Tastenbreite in Key Units (1.0 = 1U, 1.5 = 1.5U etc.)
    int row;                // Physische Zeile (0..4)
} CIYKeySurface;

/// Vollständige Layout-Beschreibung.
typedef struct {
    const char *layout_id;          // z. B. "ciy_x79_iso_de"
    const char *display_name;       // z. B. "CIY X79 (ISO-DE)"
    size_t key_count;               // Anzahl eindeutiger logischer Tasten (69)
    const CIYLogicalKey *keys;      // Array aller logischen Tasten
    size_t surface_count;           // Anzahl grafischer Flächen (70)
    const CIYKeySurface *surfaces;  // Array aller grafischen Flächen
} CIYKeyboardLayout;

/// Gibt das offizielle, hardware-verifizierte Layout für die CIY X79 ISO-DE zurück.
const CIYKeyboardLayout *ciy_layout_x79_iso_de(void);

/// Sucht eine logische Taste anhand ihres Matrix-Slots. Gibt NULL zurück, falls nicht gefunden.
const CIYLogicalKey *ciy_layout_find_key_by_slot(const CIYKeyboardLayout *layout, int matrix_slot);

/// Sucht eine logische Taste anhand ihres Primärnamens. Gibt NULL zurück, falls nicht gefunden.
const CIYLogicalKey *ciy_layout_find_key_by_name(const CIYKeyboardLayout *layout, const char *name);

#ifdef __cplusplus
}
#endif

#endif // CIY_LAYOUT_H
