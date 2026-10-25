// Hardware-freie Tests für ciy_hid (Pufferaufbau, Validierung).
// Ausführen: ./scripts/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/ciy_hid.h"

static void test_build_profile_wasd(void) {
    const int idx[] = {15, 10, 16, 22};
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    ciy_build_profile(rgb, idx, 4, true, false, true);
    const uint8_t *r = rgb, *g = rgb + CIY_LED_COUNT, *b = rgb + 2 * CIY_LED_COUNT;

    for (int i = 0; i < CIY_LED_COUNT; i++) {
        bool hit = (i == 15 || i == 10 || i == 16 || i == 22);
        assert(r[i] == (hit ? 0xFF : 0x00));
        assert(g[i] == 0x00);
        assert(b[i] == (hit ? 0x00 : 0xFF));
    }
}

static void test_build_profile_yellow_no_base(void) {
    const int idx[] = {89};
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    ciy_build_profile(rgb, idx, 1, true, true, false);
    assert(rgb[89] == 0xFF && rgb[CIY_LED_COUNT + 89] == 0xFF && rgb[2 * CIY_LED_COUNT + 89] == 0x00);
    assert(rgb[0] == 0 && rgb[CIY_LED_COUNT] == 0 && rgb[2 * CIY_LED_COUNT] == 0);
}

static void test_build_profile_ignores_out_of_range(void) {
    const int idx[] = {-1, CIY_LED_COUNT, 1000};
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    ciy_build_profile(rgb, idx, 3, true, true, false);
    for (size_t i = 0; i < sizeof(rgb); i++) assert(rgb[i] == 0);
}

static void test_validation_without_device(void) {
    CIYDevice d = { NULL };
    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
    assert(!ciy_send_planar_rgb(&d, rgb, CIY_PLANAR_RGB_SIZE - 1)); // falsche Länge
    assert(!ciy_send_planar_rgb(&d, NULL, CIY_PLANAR_RGB_SIZE));
    assert(!ciy_send_planar_rgb(&d, rgb, CIY_PLANAR_RGB_SIZE));      // kein Gerät
    assert(!ciy_apply_custom_profile_with_brightness(&d, rgb, sizeof(rgb), 5));
    assert(!ciy_apply_custom_profile_with_brightness(&d, NULL, sizeof(rgb), 5));
    assert(!ciy_apply_custom_profile_with_brightness(&d, rgb, 10, 5));
    CIYLightingState bad = {21, 0, 0, 0};
    assert(!ciy_set_state(&d, bad));
    CIYLightingState ok = {19, 0, 0, 5};
    assert(!ciy_set_state(&d, ok));                                   // gültig, aber kein Gerät
}

static void test_build_all_red(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    ciy_build_all_red_profile(rgb);
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        assert(rgb[i] == 0xFF);                     // Rot = 0xFF
        assert(rgb[CIY_LED_COUNT + i] == 0x00);     // Grün = 0x00
        assert(rgb[2 * CIY_LED_COUNT + i] == 0x00); // Blau = 0x00
    }
}

static void test_build_wasd_and_arrows(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    ciy_build_wasd_and_arrows_profile(rgb, true);
    const uint8_t *r = rgb, *g = rgb + CIY_LED_COUNT, *b = rgb + 2 * CIY_LED_COUNT;

    // WASD: 15, 10, 16, 22 -> Rot=0xFF, Grün=0x00, Blau=0x00
    int wasd[] = {15, 10, 16, 22};
    for (int k = 0; k < 4; k++) {
        int idx = wasd[k];
        assert(r[idx] == 0xFF && g[idx] == 0x00 && b[idx] == 0x00);
    }

    // Pfeile: 89, 84, 90, 96 -> Rot=0xFF, Grün=0xFF, Blau=0x00 (Gelb)
    int arrows[] = {89, 84, 90, 96};
    for (int k = 0; k < 4; k++) {
        int idx = arrows[k];
        assert(r[idx] == 0xFF && g[idx] == 0xFF && b[idx] == 0x00);
    }

    // Andere Taste (z.B. Esc -> Index 2) -> Basis Blau: Rot=0, Grün=0, Blau=0xFF
    assert(r[2] == 0x00 && g[2] == 0x00 && b[2] == 0xFF);

    // Test mit baseBlue = false: Nicht hervorgehobene Tasten müssen 0 sein
    uint8_t rgb_nobase[CIY_PLANAR_RGB_SIZE];
    ciy_build_wasd_and_arrows_profile(rgb_nobase, false);
    assert(rgb_nobase[2] == 0x00 && rgb_nobase[CIY_LED_COUNT + 2] == 0x00 && rgb_nobase[2 * CIY_LED_COUNT + 2] == 0x00);
}

static void test_hex_parsing(void) {
    uint8_t r = 0, g = 0, b = 0;
    assert(ciy_parse_hex_color("#FF5500", &r, &g, &b));
    assert(r == 0xFF && g == 0x55 && b == 0x00);

    assert(ciy_parse_hex_color("  #00FF88  ", &r, &g, &b));
    assert(r == 0x00 && g == 0xFF && b == 0x88);

    assert(ciy_parse_hex_color("#F0A\n", &r, &g, &b));
    assert(r == 0xFF && g == 0x00 && b == 0xAA);

    assert(!ciy_parse_hex_color("invalid", &r, &g, &b));
    assert(!ciy_parse_hex_color("#12345", &r, &g, &b));
    assert(!ciy_parse_hex_color("#1234567", &r, &g, &b));
    assert(!ciy_parse_hex_color(NULL, &r, &g, &b));
}

static void test_solid_color_profile(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    ciy_build_solid_color_profile(rgb, 0x12, 0x34, 0x56);
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        assert(rgb[i] == 0x12);
        assert(rgb[CIY_LED_COUNT + i] == 0x34);
        assert(rgb[2 * CIY_LED_COUNT + i] == 0x56);
    }
}

static void test_parse_profile_json(void) {
    const char *array_json =
        "[\n"
        "  {\n"
        "    \"id\": \"TEST-UUID-1\",\n"
        "    \"name\": \"Erstes Profil\",\n"
        "    \"slots\": {\n"
        "      \"0\": \"#FF0000\",\n"
        "      \"author\": \"Ungültiger Key\",\n"
        "      \"14\": \"#00FF00\"\n"
        "    }\n"
        "  },\n"
        "  {\n"
        "    \"id\": \"TEST-UUID-2\",\n"
        "    \"name\": \"Zweites Profil\",\n"
        "    \"slots\": {\n"
        "      \"21\": \"#0000FF\"\n"
        "    }\n"
        "  }\n"
        "]";

    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64] = {0};
    int count = 0;

    // 1. Erstes Profil ohne target_name
    assert(ciy_parse_profile_json(array_json, NULL, rgb, name, sizeof(name), &count));
    assert(count == 2); // Nur 0 und 14, "author" ignoriert
    assert(strcmp(name, "Erstes Profil") == 0);
    assert(rgb[1] == 0xFF && rgb[CIY_LED_COUNT + 1] == 0x00 && rgb[2 * CIY_LED_COUNT + 1] == 0x00);
    assert(rgb[15] == 0x00 && rgb[CIY_LED_COUNT + 15] == 0xFF && rgb[2 * CIY_LED_COUNT + 15] == 0x00);

    // 2. Zweites Profil gezielt nach Name wählen
    memset(name, 0, sizeof(name));
    assert(ciy_parse_profile_json(array_json, "Zweites Profil", rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "Zweites Profil") == 0);
    assert(rgb[22] == 0x00 && rgb[CIY_LED_COUNT + 22] == 0x00 && rgb[2 * CIY_LED_COUNT + 22] == 0xFF);

    // 3. Zweites Profil nach 1-basiertem Index wählen ("2")
    memset(name, 0, sizeof(name));
    assert(ciy_parse_profile_json(array_json, "2", rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "Zweites Profil") == 0);

    // 4. Nicht vorhandenes Profil suchen
    assert(!ciy_parse_profile_json(array_json, "Unbekannt", rgb, name, sizeof(name), &count));

    // 5. Single Object mit UTF-8 BOM
    const char *bom_json =
        "\xEF\xBB\xBF{\n"
        "  \"name\": \"BOM Profil\",\n"
        "  \"slots\": {\n"
        "    \"9\": \"#FFAA00\"\n"
        "  }\n"
        "}";
    memset(name, 0, sizeof(name));
    assert(ciy_parse_profile_json(bom_json, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "BOM Profil") == 0);
    assert(rgb[10] == 0xFF && rgb[CIY_LED_COUNT + 10] == 0xAA && rgb[2 * CIY_LED_COUNT + 10] == 0x00);

    // 6. Integer-Overflow und Out-of-Bounds Slots (INT_MAX, LONG_MAX, Slot 125, negative Werte)
    const char *overflow_json =
        "{\n"
        "  \"name\": \"Overflow Test\",\n"
        "  \"slots\": {\n"
        "    \"2147483647\": \"#FF0000\",\n"
        "    \"9223372036854775807\": \"#00FF00\",\n"
        "    \"125\": \"#0000FF\",\n"
        "    \"124\": \"#FFFFFF\",\n"
        "    \"-1\": \"#123456\"\n"
        "  }\n"
        "}";
    memset(name, 0, sizeof(name));
    assert(ciy_parse_profile_json(overflow_json, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1); // Nur Slot 124 ist gültig (Index 125)
    assert(rgb[125] == 0xFF && rgb[CIY_LED_COUNT + 125] == 0xFF && rgb[2 * CIY_LED_COUNT + 125] == 0xFF);

    // 7. Unicode Escapes (\u00df = ß, \u00e4 = ä, \u00fc = ü)
    const char *unicode_json =
        "{\n"
        "  \"name\": \"Profil-\\u00df-\\u00e4\",\n"
        "  \"slots\": {\n"
        "    \"10\": \"#123456\"\n"
        "  }\n"
        "}";
    memset(name, 0, sizeof(name));
    assert(ciy_parse_profile_json(unicode_json, "Profil-ß-ä", rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "Profil-ß-ä") == 0);

    // 7. Ungültiges JSON
    assert(!ciy_parse_profile_json("{}", NULL, rgb, name, sizeof(name), &count));
    assert(!ciy_parse_profile_json("kein json", NULL, rgb, name, sizeof(name), &count));
    assert(!ciy_parse_profile_json(NULL, NULL, rgb, name, sizeof(name), &count));
}

static void test_load_profile_file(void) {
    const char *tmp_path = "/tmp/ciy_test_profile.json";
    FILE *f = fopen(tmp_path, "w");
    assert(f != NULL);
    fputs("{\"name\": \"File Profile\", \"slots\": {\"0\": \"#112233\"}}", f);
    fclose(f);

    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64] = {0};
    int count = 0;
    assert(ciy_load_profile_file(tmp_path, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "File Profile") == 0);
    assert(rgb[1] == 0x11 && rgb[CIY_LED_COUNT + 1] == 0x22 && rgb[2 * CIY_LED_COUNT + 1] == 0x33);

    // Nicht existierende Datei
    assert(!ciy_load_profile_file("/tmp/nonexistent_ciy_file_xyz.json", NULL, rgb, name, sizeof(name), &count));

    // Verzeichnis abweisen
    assert(!ciy_load_profile_file("/tmp", NULL, rgb, name, sizeof(name), &count));

    remove(tmp_path);
}

#include <sys/stat.h>
#include <unistd.h>

static void test_bus_lock_reentrancy(void) {
    // 1. Erstes Locken
    assert(ciy_bus_lock());
    // 2. Reentrantes Locken auf demselben Thread
    assert(ciy_bus_lock());
    // 3. Entsperren der beiden Stufen
    ciy_bus_unlock();
    ciy_bus_unlock();
}

static void test_lockfile_security(void) {
    assert(ciy_bus_lock());
    char user_path[128];
    snprintf(user_path, sizeof(user_path), "/tmp/ciy_x79_bus_%u.lock", (unsigned)getuid());
    struct stat st;
    assert(stat(user_path, &st) == 0);
    // Prüfen, dass das Lockfile existiert und Modus 0600 hat (nur Besitzer darf zugreifen)
    assert((st.st_mode & 0777) == 0600);
    ciy_bus_unlock();
}

static void test_truncated_unicode_escapes(void) {
    const char *cases[] = {
        // Abgeschnittene Unicode-Escapes am Pufferende (ohne schließendes Anführungszeichen)
        "{\"name\": \"\\u",
        "{\"name\": \"\\u1",
        "{\"name\": \"\\u12",
        "{\"name\": \"\\u123",
        "\"\\u",
        "\"\\u1",
        "\"\\u12",
        "\"\\u123",
        "{\"slots\": {\"0\": \"#\\u",
        "{\"slots\": {\"0\": \"#\\u1",
        "{\"slots\": {\"0\": \"#\\u12",
        "{\"slots\": {\"0\": \"#\\u123",
        "{\"slots\": {\"\\u",
        "{\"slots\": {\"\\u1",
        "{\"slots\": {\"\\u12",
        "{\"slots\": {\"\\u123",

        // Abgeschnittene Unicode-Escapes unmittelbar vor dem schließenden Anführungszeichen
        "{\"name\": \"\\u\"}",
        "{\"name\": \"\\u1\"}",
        "{\"name\": \"\\u12\"}",
        "{\"name\": \"\\u123\"}",
        "\"\\u\"",
        "\"\\u1\"",
        "\"\\u12\"",
        "\"\\u123\"",
        "{\"slots\": {\"0\": \"#\\u\"}}",
        "{\"slots\": {\"0\": \"#\\u1\"}}",
        "{\"slots\": {\"0\": \"#\\u12\"}}",
        "{\"slots\": {\"0\": \"#\\u123\"}}",
        "{\"slots\": {\"\\u\": \"#FFFFFF\"}}",
        "{\"slots\": {\"\\u1\": \"#FFFFFF\"}}",
        "{\"slots\": {\"\\u12\": \"#FFFFFF\"}}",
        "{\"slots\": {\"\\u123\": \"#FFFFFF\"}}"
    };

    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64];
    int count = 0;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t len = strlen(cases[i]);
        // Exakte Heap-Allokation: ASan detektiert hier jeden 1-Byte-Überleser jenseits des \0
        char *heap_buf = (char *)malloc(len + 1);
        assert(heap_buf != NULL);
        memcpy(heap_buf, cases[i], len + 1);

        // Darf unter ASan weder crashen noch Out-of-Bounds lesen
        ciy_parse_profile_json(heap_buf, NULL, rgb, name, sizeof(name), &count);

        free(heap_buf);
    }

    // Zusätzlicher Test für Dateiende mit abgeschnittenen Escapes
    const char *tmp_path = "/tmp/ciy_test_truncated_escape.json";
    const char *file_cases[] = {
        "{\"name\": \"\\u",
        "{\"name\": \"\\u1",
        "{\"name\": \"\\u12",
        "{\"name\": \"\\u123"
    };
    for (size_t i = 0; i < sizeof(file_cases) / sizeof(file_cases[0]); i++) {
        FILE *f = fopen(tmp_path, "w");
        assert(f != NULL);
        fputs(file_cases[i], f);
        fclose(f);

        ciy_load_profile_file(tmp_path, NULL, rgb, name, sizeof(name), &count);
        remove(tmp_path);
    }
}

int main(void) {
    test_build_profile_wasd();
    test_build_profile_yellow_no_base();
    test_build_profile_ignores_out_of_range();
    test_build_all_red();
    test_build_wasd_and_arrows();
    test_validation_without_device();
    test_hex_parsing();
    test_solid_color_profile();
    test_parse_profile_json();
    test_truncated_unicode_escapes();
    test_load_profile_file();
    test_bus_lock_reentrancy();
    test_lockfile_security();
    puts("Alle Tests bestanden.");
    return 0;
}
