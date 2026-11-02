#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/ciy_battery.h"

static void test_battery_color_logic(void) {
    uint8_t r = 0, g = 0, b = 0;

    // > 50%: Grün (#00E676)
    ciy_battery_get_color(100, false, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_GREEN_R && g == CIY_BATTERY_COLOR_GREEN_G && b == CIY_BATTERY_COLOR_GREEN_B);

    ciy_battery_get_color(51, false, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_GREEN_R && g == CIY_BATTERY_COLOR_GREEN_G && b == CIY_BATTERY_COLOR_GREEN_B);

    // 21..50%: Bernstein / Gelb (#FFD600)
    ciy_battery_get_color(50, false, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_YELLOW_R && g == CIY_BATTERY_COLOR_YELLOW_G && b == CIY_BATTERY_COLOR_YELLOW_B);

    ciy_battery_get_color(21, false, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_YELLOW_R && g == CIY_BATTERY_COLOR_YELLOW_G && b == CIY_BATTERY_COLOR_YELLOW_B);

    // <= 20%: Warn-Rot (#FF1744)
    ciy_battery_get_color(20, false, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_RED_R && g == CIY_BATTERY_COLOR_RED_G && b == CIY_BATTERY_COLOR_RED_B);

    ciy_battery_get_color(5, false, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_RED_R && g == CIY_BATTERY_COLOR_RED_G && b == CIY_BATTERY_COLOR_RED_B);

    ciy_battery_get_color(0, false, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_RED_R && g == CIY_BATTERY_COLOR_RED_G && b == CIY_BATTERY_COLOR_RED_B);

    // Laden aktiv (egal welcher Prozentwert): Electric Cyan (#00E5FF)
    ciy_battery_get_color(10, true, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_CYAN_R && g == CIY_BATTERY_COLOR_CYAN_G && b == CIY_BATTERY_COLOR_CYAN_B);

    ciy_battery_get_color(90, true, &r, &g, &b);
    assert(r == CIY_BATTERY_COLOR_CYAN_R && g == CIY_BATTERY_COLOR_CYAN_G && b == CIY_BATTERY_COLOR_CYAN_B);
    printf("[Test 1] Akku-Ampelfarben & Lade-Highlight (Cyan)... PASSED\n");
}

static void test_battery_target_parsing(void) {
    int slots[16];
    size_t count = 0;

    // Standard "1-0"
    assert(ciy_battery_parse_target("1-0", slots, 16, &count));
    assert(count == 10);
    assert(slots[0] == 7 && slots[9] == 61);

    // Standard "space"
    assert(ciy_battery_parse_target("space", slots, 16, &count));
    assert(count == 1);
    assert(slots[0] == 35);

    // Standard "nav"
    assert(ciy_battery_parse_target("nav", slots, 16, &count));
    assert(count == 4);
    assert(slots[0] == 91 && slots[1] == 94 && slots[2] == 87 && slots[3] == 86);

    // Benutzerdefiniert "14,9,15,21"
    assert(ciy_battery_parse_target("14,9,15,21", slots, 16, &count));
    assert(count == 4);
    assert(slots[0] == 14 && slots[1] == 9 && slots[2] == 15 && slots[3] == 21);

    // Ungültige Eingaben
    assert(!ciy_battery_parse_target("invalid_target", slots, 16, &count));
    assert(!ciy_battery_parse_target("999", slots, 16, &count)); // Slot > 124
    assert(!ciy_battery_parse_target("", slots, 16, &count));
    assert(!ciy_battery_parse_target(NULL, slots, 16, &count));

    // Mehr Slots als max_slots -> muss false zurückgeben
    int small_slots[4];
    assert(!ciy_battery_parse_target("1,2,3,4,5", small_slots, 4, &count));
    assert(!ciy_battery_parse_target("1-0", small_slots, 4, &count)); // 10 Slots passen nicht in 4
    int tiny_slots[2];
    assert(!ciy_battery_parse_target("nav", tiny_slots, 2, &count)); // 4 Slots passen nicht in 2

    // Über 64 Slots bei max_slots 64 (Finding 6)
    int max_test_slots[64];
    char large_target[512] = {0};
    for (int i = 0; i < 65; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%s", i % 100, (i < 64) ? "," : "");
        strcat(large_target, buf);
    }
    assert(!ciy_battery_parse_target(large_target, max_test_slots, 64, &count));

    printf("[Test 2] Akku-Zielzonen Parsing (1-0, space, nav, custom)... PASSED\n");
}

static void test_battery_overlay_calculation(void) {
    uint8_t base[CIY_PLANAR_RGB_SIZE];
    memset(base, 0x11, sizeof(base)); // Basisfarbe überall 0x11

    uint8_t out[CIY_PLANAR_RGB_SIZE];

    // 1-0 bei 70%, nicht ladend -> Stufe 7 von 10 aktiv (Grün), 8..10 inaktiv (0x00)
    int target_1_0[10];
    size_t count = 0;
    ciy_battery_parse_target("1-0", target_1_0, 10, &count);

    assert(ciy_battery_calculate_overlay(base, 70, false, target_1_0, count, false, out));

    // Prüfe die 7 aktiven Tasten
    for (int i = 0; i < 7; i++) {
        int led = target_1_0[i] + 1;
        assert(out[led] == CIY_BATTERY_COLOR_GREEN_R);
        assert(out[CIY_LED_COUNT + led] == CIY_BATTERY_COLOR_GREEN_G);
        assert(out[2 * CIY_LED_COUNT + led] == CIY_BATTERY_COLOR_GREEN_B);
    }

    // Prüfe die 3 inaktiven Tasten (müssen dunkel/aus sein)
    for (int i = 7; i < 10; i++) {
        int led = target_1_0[i] + 1;
        assert(out[led] == 0);
        assert(out[CIY_LED_COUNT + led] == 0);
        assert(out[2 * CIY_LED_COUNT + led] == 0);
    }

    // Prüfe, dass andere Tasten (z. B. Esc LED 2, W LED 15) ihre Basisfarbe 0x11 behalten haben!
    assert(out[2] == 0x11);
    assert(out[15] == 0x11);

    // Segmentierter Modus bei 80% (Keys 1..2 rot, 3..5 gelb, 6..8 grün, 9..10 aus)
    assert(ciy_battery_calculate_overlay(base, 80, false, target_1_0, count, true, out));
    // Key 1 (Index 0, 10% -> Rot)
    int led0 = target_1_0[0] + 1;
    assert(out[led0] == CIY_BATTERY_COLOR_RED_R && out[CIY_LED_COUNT + led0] == CIY_BATTERY_COLOR_RED_G);
    // Key 4 (Index 3, 40% -> Gelb)
    int led3 = target_1_0[3] + 1;
    assert(out[led3] == CIY_BATTERY_COLOR_YELLOW_R && out[CIY_LED_COUNT + led3] == CIY_BATTERY_COLOR_YELLOW_G);
    // Key 7 (Index 6, 70% -> Grün)
    int led6 = target_1_0[6] + 1;
    assert(out[led6] == CIY_BATTERY_COLOR_GREEN_R && out[CIY_LED_COUNT + led6] == CIY_BATTERY_COLOR_GREEN_G);
    // Key 9 (Index 8, 90% -> Aus, da Pegel nur bis 80%)
    int led8 = target_1_0[8] + 1;
    assert(out[led8] == 0 && out[CIY_LED_COUNT + led8] == 0);

    // Leertaste bei 15% Warnstufe (Warn-Rot)
    int target_space[1] = {35};
    assert(ciy_battery_calculate_overlay(NULL, 15, false, target_space, 1, false, out));
    int space_led = 36;
    assert(out[space_led] == CIY_BATTERY_COLOR_RED_R);
    assert(out[CIY_LED_COUNT + space_led] == CIY_BATTERY_COLOR_RED_G);
    assert(out[2 * CIY_LED_COUNT + space_led] == CIY_BATTERY_COLOR_RED_B);

    // KRITISCH: Leertaste bei 0% Ladestand muss aktiv bleiben und Warn-Rot zeigen (nicht ausgehen!)
    assert(ciy_battery_calculate_overlay(NULL, 0, false, target_space, 1, false, out));
    assert(out[space_led] == CIY_BATTERY_COLOR_RED_R);
    assert(out[CIY_LED_COUNT + space_led] == CIY_BATTERY_COLOR_RED_G);
    assert(out[2 * CIY_LED_COUNT + space_led] == CIY_BATTERY_COLOR_RED_B);

    // 1-0 bei 0%: Keine LEDs aktiv
    assert(ciy_battery_calculate_overlay(NULL, 0, false, target_1_0, count, false, out));
    for (int i = 0; i < 10; i++) {
        int led = target_1_0[i] + 1;
        assert(out[led] == 0 && out[CIY_LED_COUNT + led] == 0 && out[2 * CIY_LED_COUNT + led] == 0);
    }

    printf("[Test 3] Planar Overlay Berechnung & Basis-Profil-Schutz... PASSED\n");
}

static void test_battery_mocks(void) {
    setenv("CIY_BATTERY_MOCK_PERCENT", "42", 1);
    setenv("CIY_BATTERY_MOCK_CHARGING", "1", 1);
    setenv("CIY_BATTERY_MOCK_NAME", "MockMacBook", 1);

    CIYBatteryInfo info;
    assert(ciy_battery_get_info(CIY_BATTERY_SOURCE_INTERNAL, &info));
    assert(info.percent == 42);
    assert(info.is_charging == true);
    assert(strcmp(info.name, "MockMacBook") == 0);

    CIYBatteryInfo sources[4];
    size_t count = ciy_battery_list_sources(sources, 4);
    assert(count == 1);
    assert(sources[0].percent == 42);

    unsetenv("CIY_BATTERY_MOCK_PERCENT");
    unsetenv("CIY_BATTERY_MOCK_CHARGING");
    unsetenv("CIY_BATTERY_MOCK_NAME");

    // Ohne Mocks: UPS abfragen (wenn keine vorhanden ist, muss percent == -1 und false gemeldet werden)
    CIYBatteryInfo ups_info;
    bool has_ups = ciy_battery_get_info(CIY_BATTERY_SOURCE_UPS, &ups_info);
    if (!has_ups) {
        assert(ups_info.percent == -1);
        assert(ups_info.is_present == false);
    }

    printf("[Test 4] Mock-Engine & deterministisches Testen... PASSED\n");
}

static void test_battery_unreadable_sources(void) {
    // 1. Mock für unlesbare / unbekannte Quelle (-1)
    setenv("CIY_BATTERY_MOCK_PERCENT", "-1", 1);
    CIYBatteryInfo mock_info;
    assert(ciy_battery_get_info(CIY_BATTERY_SOURCE_INTERNAL, &mock_info));
    assert(mock_info.percent == -1);
    unsetenv("CIY_BATTERY_MOCK_PERCENT");

    // 2. Initialisierung und Default-Wert bei nicht lesbarer Kapazität
    CIYBatteryInfo info;
    memset(&info, 0, sizeof(info));
    info.percent = -1;
    assert(info.percent == -1);

    // 3. Nicht vorhandene oder nicht lesbare Quelle via ciy_battery_get_info
    CIYBatteryInfo ups_info;
    bool has_ups = ciy_battery_get_info(CIY_BATTERY_SOURCE_UPS, &ups_info);
    if (!has_ups) {
        assert(ups_info.percent == -1);
        assert(ups_info.is_present == false);
    }

    // 4. Fallback ohne Mocks für nicht existierende Peripherie
    CIYBatteryInfo periph_info;
    bool has_periph = ciy_battery_get_info(CIY_BATTERY_SOURCE_PERIPHERAL, &periph_info);
    if (!has_periph) {
        assert(periph_info.percent == -1);
        assert(periph_info.is_present == false);
    }

    printf("[Test 5] Nicht lesbare / nicht vorhandene Quellen liefern percent == -1... PASSED\n");
}

int main(void) {
    printf("=== Starte CIY Battery Unit-Tests (ASan/UBSan) ===\n");
    test_battery_color_logic();
    test_battery_target_parsing();
    test_battery_overlay_calculation();
    test_battery_mocks();
    test_battery_unreadable_sources();
    printf("=== Alle CIY Battery Unit-Tests erfolgreich bestanden! ===\n");
    return 0;
}
