#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/core/ciy_gradient.h"

static void test_gradient_channel_lerp(void) {
    assert(ciy_gradient_lerp_channel(0, 200, 0.0f) == 0);
    assert(ciy_gradient_lerp_channel(0, 200, 1.0f) == 200);
    assert(ciy_gradient_lerp_channel(0, 200, 0.5f) == 100);
    assert(ciy_gradient_lerp_channel(100, 0, 0.5f) == 50);

    // Clamping
    assert(ciy_gradient_lerp_channel(50, 150, -0.5f) == 50);
    assert(ciy_gradient_lerp_channel(50, 150, 1.5f) == 150);
    printf("[Test 1] Channel Lerp & Boundary Clamping... PASSED\n");
}

static void test_gradient_slot_coords(void) {
    float u = 0.0f, row = 0.0f;

    // Esc (Slot 1)
    assert(ciy_gradient_get_slot_coords(1, &u, &row));
    assert(fabs(row - 0.0f) < 0.001f);
    assert(fabs(u - 0.5f) < 0.001f); // 1.0U Breite -> Center = 0.5U

    // Entf (Slot 86, ganz rechts in Row 0: 15.0..16.0U)
    assert(ciy_gradient_get_slot_coords(86, &u, &row));
    assert(fabs(row - 0.0f) < 0.001f);
    assert(fabs(u - 15.5f) < 0.001f); // Center = 15.5U

    // Tab (Slot 2, Row 1, Breite 1.5U)
    assert(ciy_gradient_get_slot_coords(2, &u, &row));
    assert(fabs(row - 1.0f) < 0.001f);
    assert(fabs(u - 0.75f) < 0.001f);

    // ISO Enter (Slot 81, mehrflächig in Row 1 und Row 2)
    assert(ciy_gradient_get_slot_coords(81, &u, &row));
    assert(row >= 1.0f && row <= 2.0f);
    assert(u > 13.0f && u < 16.0f);

    // Ungültiger Slot
    assert(!ciy_gradient_get_slot_coords(999, &u, &row));
    assert(!ciy_gradient_get_slot_coords(-1, &u, &row));
    printf("[Test 2] Physische Koordinaten-Auflösung (U-Coords & Rows)... PASSED\n");
}

static void test_gradient_horizontal(void) {
    // Verlauf von Rot (255, 0, 0) nach Blau (0, 0, 255)
    uint8_t r = 0, g = 0, b = 0;

    // Esc (links außen)
    assert(ciy_gradient_calculate_slot_color(1, 255, 0, 0, 0, 0, 255, CIY_GRADIENT_HORIZONTAL, &r, &g, &b));
    assert(r >= 250 && g == 0 && b <= 5);

    // Entf (rechts außen)
    assert(ciy_gradient_calculate_slot_color(86, 255, 0, 0, 0, 0, 255, CIY_GRADIENT_HORIZONTAL, &r, &g, &b));
    assert(r <= 5 && g == 0 && b >= 250);

    // G (Mitte: Slot 33)
    assert(ciy_gradient_calculate_slot_color(33, 255, 0, 0, 0, 0, 255, CIY_GRADIENT_HORIZONTAL, &r, &g, &b));
    assert(r > 80 && r < 180);
    assert(b > 80 && b < 180);
    assert(g == 0);
    printf("[Test 3] Horizontaler Verlauf (Links -> Rechts)... PASSED\n");
}

static void test_gradient_vertical(void) {
    // Verlauf von Gelb (255, 255, 0) nach Cyan (0, 255, 255)
    uint8_t r = 0, g = 0, b = 0;

    // Reihe 0: Esc (Slot 1)
    assert(ciy_gradient_calculate_slot_color(1, 255, 255, 0, 0, 255, 255, CIY_GRADIENT_VERTICAL, &r, &g, &b));
    assert(r == 255 && g == 255 && b == 0);

    // Reihe 4: Leertaste (Slot 35)
    assert(ciy_gradient_calculate_slot_color(35, 255, 255, 0, 0, 255, 255, CIY_GRADIENT_VERTICAL, &r, &g, &b));
    assert(r == 0 && g == 255 && b == 255);

    // Reihe 2: A (Slot 9, Reihe 2 = 2/4 = 0.5)
    assert(ciy_gradient_calculate_slot_color(9, 255, 255, 0, 0, 255, 255, CIY_GRADIENT_VERTICAL, &r, &g, &b));
    assert(r >= 120 && r <= 135);
    assert(g == 255);
    assert(b >= 120 && b <= 135);
    printf("[Test 4] Vertikaler Verlauf (Oben -> Unten)... PASSED\n");
}

static void test_gradient_planar_generation(void) {
    uint8_t planar[CIY_PLANAR_RGB_SIZE];
    assert(ciy_gradient_generate_planar(planar, 255, 0, 0, 0, 255, 0, CIY_GRADIENT_HORIZONTAL));

    // LED für Esc (Slot 1 -> LED 2)
    assert(planar[2] >= 250); // Rot
    // LED für Entf (Slot 86 -> LED 87)
    assert(planar[CIY_LED_COUNT + 87] >= 250); // Grün

    // Unbelegte Slots (z. B. Slot 0 -> LED 1) müssen 0 bleiben
    assert(planar[1] == 0 && planar[CIY_LED_COUNT + 1] == 0 && planar[2 * CIY_LED_COUNT + 1] == 0);
    printf("[Test 5] Planar RGB Puffer-Generierung... PASSED\n");
}

int main(void) {
    printf("=== Starte CIY Gradient Unit-Tests (ASan/UBSan) ===\n");
    test_gradient_channel_lerp();
    test_gradient_slot_coords();
    test_gradient_horizontal();
    test_gradient_vertical();
    test_gradient_planar_generation();
    printf("=== Alle CIY Gradient Unit-Tests erfolgreich bestanden! ===\n");
    return 0;
}
