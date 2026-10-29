#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Strikt plattformunabhängiger Header-Test:
// Weder IOKit noch CoreFoundation dürfen hier eingebunden werden!
#include "../src/core/ciy_protocol.h"
#include "../src/layouts/ciy_layout.h"

static void test_protocol_constants_and_sizes(void) {
    assert(CIY_VENDOR_ID == 0x258A);
    assert(CIY_PRODUCT_ID == 0x0049);
    assert(CIY_INTERFACE_NUM == 1);

    assert(CIY_LED_COUNT == 126);
    assert(CIY_PLANAR_RGB_SIZE == 378);
    assert(CIY_MAX_MATRIX_SLOT == 124);

    assert(CIY_REPORT_0A_ID == 0x0A);
    assert(CIY_REPORT_0A_SIZE == 42);
    assert(CIY_REPORT_0A_MIN_LEN == 5);

    assert(CIY_REPORT_0B_ID == 0x0B);
    assert(CIY_REPORT_0B_SIZE == 379);

    assert(CIY_CUSTOM_MODE == 19);
    assert(CIY_CUSTOM_DELAY_MS == 50);

    assert(CIY_MAX_MODE == 20);
    assert(CIY_MAX_COLOR == 10);
    assert(CIY_MAX_SPEED == 4);
    assert(CIY_MAX_BRIGHTNESS == 5);
}

static void test_state_validation(void) {
    // Gültige Grenzwerte
    CIYLightingState valid = {0, 0, 0, 0};
    assert(ciy_protocol_validate_state(valid));

    valid = (CIYLightingState){20, 10, 4, 5};
    assert(ciy_protocol_validate_state(valid));

    valid = (CIYLightingState){19, 0, 2, 3};
    assert(ciy_protocol_validate_state(valid));

    // Ungültige Werte
    CIYLightingState bad_mode = {21, 0, 0, 0};
    assert(!ciy_protocol_validate_state(bad_mode));

    CIYLightingState bad_color = {0, 11, 0, 0};
    assert(!ciy_protocol_validate_state(bad_color));

    CIYLightingState bad_speed = {0, 0, 5, 0};
    assert(!ciy_protocol_validate_state(bad_speed));

    CIYLightingState bad_bright = {0, 0, 0, 6};
    assert(!ciy_protocol_validate_state(bad_bright));
}

static void test_encode_report_0a(void) {
    uint8_t buf[CIY_REPORT_0A_SIZE];

    // Null-Pointer-Schutz
    CIYLightingState st = {15, 2, 3, 4};
    assert(!ciy_protocol_encode_0a(st, NULL));

    // Ungültiger Zustand wird abgelehnt
    CIYLightingState bad = {25, 0, 0, 0};
    assert(!ciy_protocol_encode_0a(bad, buf));

    // Erfolgreiche Enkodierung
    memset(buf, 0xFF, sizeof(buf));
    assert(ciy_protocol_encode_0a(st, buf));

    assert(buf[0] == 0x0A);
    assert(buf[1] == 15); // Modus
    assert(buf[2] == 2);  // Farbe
    assert(buf[3] == 1);  // Invertierter Speed: 4 - 3 = 1
    assert(buf[4] == 4);  // Helligkeit

    // Padding-Bytes müssen alle 0 sein
    for (size_t i = 5; i < CIY_REPORT_0A_SIZE; i++) {
        assert(buf[i] == 0x00);
    }

    // Speed-Invertierungs-Grenzwerte
    st.speed = 0;
    assert(ciy_protocol_encode_0a(st, buf));
    assert(buf[3] == 4); // 4 - 0 = 4

    st.speed = 4;
    assert(ciy_protocol_encode_0a(st, buf));
    assert(buf[3] == 0); // 4 - 4 = 0
}

static void test_decode_report_0a(void) {
    uint8_t raw[42] = {0};
    raw[0] = 0x0A;
    raw[1] = 17; // Modus 17
    raw[2] = 5;  // Farbe 5
    raw[3] = 1;  // Invertierter Drahtwert: Speed = 4 - 1 = 3
    raw[4] = 4;  // Helligkeit 4

    CIYLightingState decoded = {0};
    assert(ciy_protocol_decode_0a(raw, sizeof(raw), &decoded));
    assert(decoded.mode == 17);
    assert(decoded.color == 5);
    assert(decoded.speed == 3);
    assert(decoded.brightness == 4);

    // out_st == NULL validiert Format
    assert(ciy_protocol_decode_0a(raw, sizeof(raw), NULL));

    // Fehler: Falsche Report-ID
    raw[0] = 0x0B;
    assert(!ciy_protocol_decode_0a(raw, sizeof(raw), &decoded));
    raw[0] = 0x0A;

    // Fehler: Zu kurz
    assert(!ciy_protocol_decode_0a(raw, 4, &decoded));

    // Fehler: NULL-Puffer
    assert(!ciy_protocol_decode_0a(NULL, 42, &decoded));

    // Defensives Clamping bei Firmware-Ausreißern
    raw[3] = 99; // Ungültiges Hardware-Tempo
    raw[4] = 88; // Ungültige Hardware-Helligkeit
    assert(ciy_protocol_decode_0a(raw, sizeof(raw), &decoded));
    assert(decoded.speed == 0);                 // Sicher auf 0 geklemmt
    assert(decoded.brightness == CIY_MAX_BRIGHTNESS); // Sicher auf 5 geklemmt
}

static void test_encode_report_0b(void) {
    uint8_t planar_rgb[CIY_PLANAR_RGB_SIZE];
    for (size_t i = 0; i < CIY_PLANAR_RGB_SIZE; i++) {
        planar_rgb[i] = (uint8_t)(i & 0xFF);
    }

    uint8_t wire[CIY_REPORT_0B_SIZE];
    memset(wire, 0, sizeof(wire));

    // Fehlerprüfungen
    assert(!ciy_protocol_encode_0b(NULL, CIY_PLANAR_RGB_SIZE, wire));
    assert(!ciy_protocol_encode_0b(planar_rgb, CIY_PLANAR_RGB_SIZE, NULL));
    assert(!ciy_protocol_encode_0b(planar_rgb, CIY_PLANAR_RGB_SIZE - 1, wire));

    // Erfolgreiche Enkodierung
    assert(ciy_protocol_encode_0b(planar_rgb, sizeof(planar_rgb), wire));
    assert(wire[0] == 0x0B);
    assert(memcmp(&wire[1], planar_rgb, CIY_PLANAR_RGB_SIZE) == 0);
}

static void test_report_0b_offsets(void) {
    // Grenzwerte und Fehler
    assert(ciy_protocol_report_0b_r_offset(-1) == -1);
    assert(ciy_protocol_report_0b_r_offset(126) == -1);
    assert(ciy_protocol_report_0b_g_offset(-5) == -1);
    assert(ciy_protocol_report_0b_g_offset(126) == -1);
    assert(ciy_protocol_report_0b_b_offset(-1) == -1);
    assert(ciy_protocol_report_0b_b_offset(200) == -1);

    // Slot 0 (LED-Index 0: theoretischer Anfang)
    assert(ciy_protocol_report_0b_r_offset(0) == 1);
    assert(ciy_protocol_report_0b_g_offset(0) == 127);
    assert(ciy_protocol_report_0b_b_offset(0) == 253);

    // Letzter Slot 125
    assert(ciy_protocol_report_0b_r_offset(125) == 126);
    assert(ciy_protocol_report_0b_g_offset(125) == 252);
    assert(ciy_protocol_report_0b_b_offset(125) == 378);

    // Layout-Abgleich: Esc (Slot 1, led_index 2)
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    assert(layout != NULL);
    const CIYLogicalKey *esc = ciy_layout_find_key_by_name(layout, "Esc");
    assert(esc != NULL);
    assert(esc->matrix_slot == 1);
    assert(esc->led_index == 2);
    assert(ciy_protocol_report_0b_r_offset(esc->led_index) == 3);
    assert(ciy_protocol_report_0b_g_offset(esc->led_index) == 129);
    assert(ciy_protocol_report_0b_b_offset(esc->led_index) == 255);

    // Leertaste (Slot 35, led_index 36)
    const CIYLogicalKey *space = ciy_layout_find_key_by_name(layout, "Leertaste (Space)");
    assert(space != NULL);
    assert(space->matrix_slot == 35);
    assert(space->led_index == 36);
    assert(ciy_protocol_report_0b_r_offset(space->led_index) == 37);
    assert(ciy_protocol_report_0b_g_offset(space->led_index) == 163);
    assert(ciy_protocol_report_0b_b_offset(space->led_index) == 289);
}

static void test_transfer_plans(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    memset(rgb, 0xAA, sizeof(rgb));

    CIYTransferPlan plan;
    // Ungültige Parameter
    assert(!ciy_build_transfer_plan(NULL, rgb, sizeof(rgb), 5, "Test", 69));
    assert(!ciy_build_transfer_plan(&plan, NULL, sizeof(rgb), 5, "Test", 69));
    assert(!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb) - 1, 5, "Test", 69));

    // Gültiger Custom-Plan
    assert(ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), 4, "Cyberpunk", 69));
    assert(plan.step_count == 3);
    assert(strcmp(plan.profile_name, "Cyberpunk") == 0);
    assert(plan.keys_count == 69);
    assert(plan.brightness == 4);

    // Schritt 1: Report 0x0B
    assert(plan.steps[0].type == CIY_PLAN_STEP_REPORT);
    assert(plan.steps[0].report_id == CIY_REPORT_0B_ID);
    assert(plan.steps[0].length == CIY_REPORT_0B_SIZE);
    assert(plan.steps[0].data[0] == 0x0B);
    assert(memcmp(&plan.steps[0].data[1], rgb, CIY_PLANAR_RGB_SIZE) == 0);

    // Schritt 2: Delay 50ms
    assert(plan.steps[1].type == CIY_PLAN_STEP_DELAY);
    assert(plan.steps[1].delay_ms == CIY_CUSTOM_DELAY_MS);

    // Schritt 3: Report 0x0A Modus 19
    assert(plan.steps[2].type == CIY_PLAN_STEP_REPORT);
    assert(plan.steps[2].report_id == CIY_REPORT_0A_ID);
    assert(plan.steps[2].length == CIY_REPORT_0A_SIZE);
    assert(plan.steps[2].mode == CIY_CUSTOM_MODE);
    assert(plan.steps[2].brightness == 4);
    assert(plan.steps[2].data[0] == 0x0A);
    assert(plan.steps[2].data[1] == 19);
    assert(plan.steps[2].data[4] == 4);

    // State-Plan (1 Schritt)
    CIYLightingState st = {15, 2, 3, 5};
    CIYTransferPlan state_plan;
    assert(!ciy_build_state_transfer_plan(NULL, st));
    CIYLightingState bad_st = {25, 0, 0, 0};
    assert(!ciy_build_state_transfer_plan(&state_plan, bad_st));

    assert(ciy_build_state_transfer_plan(&state_plan, st));
    assert(state_plan.step_count == 1);
    assert(state_plan.steps[0].type == CIY_PLAN_STEP_REPORT);
    assert(state_plan.steps[0].report_id == 0x0A);
    assert(state_plan.steps[0].length == 42);
    assert(state_plan.steps[0].mode == 15);
    assert(state_plan.steps[0].color == 2);
    assert(state_plan.steps[0].speed == 3);
    assert(state_plan.steps[0].brightness == 5);
    assert(state_plan.steps[0].data[0] == 0x0A);
    assert(state_plan.steps[0].data[1] == 15);
    assert(state_plan.steps[0].data[3] == 1); // 4 - 3 = 1
}

int main(void) {
    printf("=== Starte Standalone Protocol Tests (Zero OS-Lock-in) ===\n");
    test_protocol_constants_and_sizes();
    test_state_validation();
    test_encode_report_0a();
    test_decode_report_0a();
    test_encode_report_0b();
    test_report_0b_offsets();
    test_transfer_plans();
    printf("Alle Protocol-Tests erfolgreich bestanden!\n");
    return 0;
}
