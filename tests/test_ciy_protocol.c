#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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

    // Fehler: Zu kurz (SEC-T3-001: Berichte < 42 Bytes werden strikt abgelehnt)
    assert(!ciy_protocol_decode_0a(raw, 4, &decoded));
    assert(!ciy_protocol_decode_0a(raw, 5, &decoded));
    assert(!ciy_protocol_decode_0a(raw, 41, &decoded));
    assert(ciy_protocol_decode_0a(raw, 42, &decoded));

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

static void test_overflow_and_size_max_boundaries(void) {
    uint8_t planar_rgb[CIY_PLANAR_RGB_SIZE] = {0};
    uint8_t wire[CIY_REPORT_0B_SIZE] = {0};
    CIYTransferPlan plan;

    // 1a: encode_0b & build_transfer_plan mit rgb_len = 0, 377, 379 (+1), SIZE_MAX
    assert(!ciy_protocol_encode_0b(planar_rgb, 0, wire));
    assert(!ciy_protocol_encode_0b(planar_rgb, CIY_PLANAR_RGB_SIZE - 1, wire));
    assert(!ciy_protocol_encode_0b(planar_rgb, CIY_REPORT_0B_SIZE, wire)); // 379 != 378
    assert(!ciy_protocol_encode_0b(planar_rgb, SIZE_MAX, wire));

    assert(!ciy_build_transfer_plan(&plan, planar_rgb, 0, 5, "Test", 69));
    assert(!ciy_build_transfer_plan(&plan, planar_rgb, CIY_REPORT_0B_SIZE, 5, "Test", 69)); // 379
    assert(!ciy_build_transfer_plan(&plan, planar_rgb, SIZE_MAX, 5, "Test", 69));

    // Helligkeits-Clamping und Profilnamen-Truncation bei build_transfer_plan
    char overlong_name[96];
    memset(overlong_name, 'P', sizeof(overlong_name) - 1);
    overlong_name[sizeof(overlong_name) - 1] = '\0';
    assert(ciy_build_transfer_plan(&plan, planar_rgb, CIY_PLANAR_RGB_SIZE, 255, overlong_name, INT_MAX));
    assert(plan.brightness == CIY_MAX_BRIGHTNESS);
    assert(plan.keys_count == INT_MAX);
    assert(strlen(plan.profile_name) == sizeof(plan.profile_name) - 1);
    assert(plan.profile_name[sizeof(plan.profile_name) - 1] == '\0');

    assert(ciy_build_transfer_plan(&plan, planar_rgb, CIY_PLANAR_RGB_SIZE, 0, NULL, 0));
    assert(plan.profile_name[0] == '\0');
    assert(plan.brightness == 0);

    // 1a: Offset-Helper mit INT_MIN und INT_MAX
    assert(ciy_protocol_report_0b_r_offset(INT_MIN) == -1);
    assert(ciy_protocol_report_0b_r_offset(INT_MAX) == -1);
    assert(ciy_protocol_report_0b_g_offset(INT_MIN) == -1);
    assert(ciy_protocol_report_0b_g_offset(INT_MAX) == -1);
    assert(ciy_protocol_report_0b_b_offset(INT_MIN) == -1);
    assert(ciy_protocol_report_0b_b_offset(INT_MAX) == -1);
}

static void test_decode_0a_exact_clamping_and_lengths(void) {
    uint8_t raw[43];
    memset(raw, 0, sizeof(raw));
    raw[0] = CIY_REPORT_0A_ID;

    CIYLightingState st = {0};

    // 1b: Exakte Längenkanten 0, 41, 42, 43
    assert(!ciy_protocol_decode_0a(raw, 0, &st));
    assert(!ciy_protocol_decode_0a(raw, 41, &st));
    assert(ciy_protocol_decode_0a(raw, 42, &st));
    assert(ciy_protocol_decode_0a(raw, 43, &st));

    // 1b: Obere Feldgrenzen exakt an Kante vs. +1
    // mode: 20 (gültig) vs. 21 (geklemmt auf 0)
    raw[1] = 20; raw[2] = 10; raw[3] = 4; raw[4] = 5;
    assert(ciy_protocol_decode_0a(raw, 42, &st));
    assert(st.mode == 20);
    assert(st.color == 10);
    assert(st.speed == 0); // 4 - 4 = 0
    assert(st.brightness == 5);

    raw[1] = 21; raw[2] = 11; raw[3] = 5; raw[4] = 6;
    assert(ciy_protocol_decode_0a(raw, 42, &st));
    assert(st.mode == 0);
    assert(st.color == 0);
    assert(st.speed == 0);
    assert(st.brightness == CIY_MAX_BRIGHTNESS);

    // Wire-Speed 0 entspricht maximalem logischem Tempo 4
    raw[1] = 0; raw[2] = 0; raw[3] = 0; raw[4] = 0;
    assert(ciy_protocol_decode_0a(raw, 42, &st));
    assert(st.mode == 0 && st.color == 0 && st.speed == 4 && st.brightness == 0);
}

static void test_non_null_terminated_asan_buffers(void) {
    // 2b: Exakte Heap-Allokation ohne NUL-Terminator für Binär-APIs unter ASan
    const size_t decode_lens[] = {1, 5, 41, 42, 43};
    for (size_t i = 0; i < sizeof(decode_lens) / sizeof(decode_lens[0]); i++) {
        size_t len = decode_lens[i];
        uint8_t *exact = (uint8_t *)malloc(len);
        assert(exact != NULL);
        memset(exact, 0x7F, len); // kein einziges 0x00-Byte im Puffer
        exact[0] = CIY_REPORT_0A_ID;

        CIYLightingState st = {0};
        bool ok = ciy_protocol_decode_0a(exact, len, &st);
        if (len < CIY_REPORT_0A_SIZE) {
            assert(!ok);
        } else {
            assert(ok);
            assert(st.mode == 0 && st.color == 0 && st.speed == 0 && st.brightness == CIY_MAX_BRIGHTNESS);
        }
        free(exact);
    }

    const size_t encode_lens[] = {1, 377, 378, 379};
    for (size_t i = 0; i < sizeof(encode_lens) / sizeof(encode_lens[0]); i++) {
        size_t len = encode_lens[i];
        uint8_t *exact_rgb = (uint8_t *)malloc(len);
        uint8_t *exact_out = (uint8_t *)malloc(CIY_REPORT_0B_SIZE);
        assert(exact_rgb != NULL && exact_out != NULL);
        memset(exact_rgb, 0xCC, len);
        memset(exact_out, 0xEE, CIY_REPORT_0B_SIZE);

        bool ok = ciy_protocol_encode_0b(exact_rgb, len, exact_out);
        if (len == CIY_PLANAR_RGB_SIZE) {
            assert(ok);
            assert(exact_out[0] == CIY_REPORT_0B_ID);
            assert(exact_out[CIY_REPORT_0B_SIZE - 1] == 0xCC);
        } else {
            assert(!ok);
        }
        free(exact_rgb);
        free(exact_out);
    }
}

static void test_layout_null_guards_and_poisoned_protocol_outputs(void) {
    // 1c: Layout-Header NULL-Guards
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    assert(layout != NULL);
    assert(ciy_layout_find_key_by_slot(NULL, 1) == NULL);
    assert(ciy_layout_find_key_by_slot(layout, INT_MIN) == NULL);
    assert(ciy_layout_find_key_by_slot(layout, INT_MAX) == NULL);
    assert(ciy_layout_find_key_by_name(NULL, "Esc") == NULL);
    assert(ciy_layout_find_key_by_name(layout, NULL) == NULL);
    assert(ciy_layout_find_key_by_name(NULL, NULL) == NULL);

    CIYKeyboardLayout empty_layout = {"empty", "Empty", 5, NULL, 5, NULL};
    assert(ciy_layout_find_key_by_slot(&empty_layout, 1) == NULL);
    assert(ciy_layout_find_key_by_name(&empty_layout, "Esc") == NULL);

    // 3b: 0xAA-Poisoning bei Fehlerpfaden der Protokoll-Funktionen
    CIYLightingState poisoned_st;
    memset(&poisoned_st, 0xAA, sizeof(poisoned_st));
    uint8_t bad_0a[42];
    memset(bad_0a, 0xFF, sizeof(bad_0a)); // ungültige Report-ID 0xFF
    assert(!ciy_protocol_decode_0a(bad_0a, sizeof(bad_0a), &poisoned_st));
    assert(poisoned_st.mode == 0 && poisoned_st.color == 0 && poisoned_st.speed == 0 && poisoned_st.brightness == 0);

    uint8_t poisoned_0a[CIY_REPORT_0A_SIZE];
    memset(poisoned_0a, 0xAA, sizeof(poisoned_0a));
    CIYLightingState invalid_st = {99, 99, 99, 99};
    assert(!ciy_protocol_encode_0a(invalid_st, poisoned_0a));
    assert(poisoned_0a[0] == 0);

    CIYTransferPlan poisoned_plan;
    memset(&poisoned_plan, 0xAA, sizeof(poisoned_plan));
    assert(!ciy_build_transfer_plan(&poisoned_plan, NULL, CIY_PLANAR_RGB_SIZE, 5, "X", 1));
    assert(poisoned_plan.step_count == 0 && poisoned_plan.profile_name[0] == '\0');

    memset(&poisoned_plan, 0xAA, sizeof(poisoned_plan));
    assert(!ciy_build_state_transfer_plan(&poisoned_plan, invalid_st));
    assert(poisoned_plan.step_count == 0 && poisoned_plan.profile_name[0] == '\0');
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
    test_overflow_and_size_max_boundaries();
    test_decode_0a_exact_clamping_and_lengths();
    test_non_null_terminated_asan_buffers();
    test_layout_null_guards_and_poisoned_protocol_outputs();
    printf("Alle Protocol-Tests erfolgreich bestanden!\n");
    return 0;
}
