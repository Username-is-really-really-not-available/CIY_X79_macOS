// Hardware-freie Tests für ciy_hid (Pufferaufbau, Validierung).
// Ausführen: ./scripts/run_tests.sh
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include "../src/core/ciy_hid.h"
#include "../src/layouts/ciy_layout.h"

#if !defined(CIY_ENABLE_TEST_HOOKS)
#error "Tests must be compiled with -DCIY_ENABLE_TEST_HOOKS=1 to prevent real hardware I/O"
#endif

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

static void test_build_profile_null_checks(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    const int idx[] = {15, 10};
    // SEC-T2-002: NULL rgb darf nicht dereferenziert werden
    ciy_build_profile(NULL, idx, 2, true, false, true);
    // NULL indices bei count > 0 darf nicht dereferenziert werden
    ciy_build_profile(rgb, NULL, 4, true, false, true);
    // NULL indices bei count == 0 darf nicht dereferenziert werden
    ciy_build_profile(rgb, NULL, 0, true, false, false);
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

static void test_unicode_surrogate_rejection(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64] = {0};
    int count = 0;

    // Surrogate pair (U+D800) - invalid in UTF-8, must be rejected
    const char *surrogate_json = "{\"name\": \"\\uD800\", \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(surrogate_json, NULL, rgb, name, sizeof(name), &count));

    // High surrogate (U+DBFF) - invalid
    const char *high_surrogate_json = "{\"name\": \"\\uDBFF\", \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(high_surrogate_json, NULL, rgb, name, sizeof(name), &count));

    // Low surrogate (U+DFFF) - invalid
    const char *low_surrogate_json = "{\"name\": \"\\uDFFF\", \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(low_surrogate_json, NULL, rgb, name, sizeof(name), &count));

    // Codepoint > U+10FFFF (max valid Unicode) - e.g., U+110000
    // Note: \uXXXX only supports 4 hex digits (max U+FFFF), so we test U+FFFF which is valid
    // but U+110000 would require \U00110000 which our parser doesn't support.
    // Test that U+FFFF (max BMP) is accepted, but U+10FFFF is the actual limit.
    // Since we only support \uXXXX (16-bit), we test boundary at U+FFFF.
    // The parser should accept U+FFFF (valid BMP) but reject if somehow > 0x10FFFF.
    // Our parser only sees 4 hex digits, so max is U+FFFF which is < 0x10FFFF, so it passes.
    // The check is still valuable for correctness if parser is extended.

    // Valid high BMP codepoint (U+FFFF) - should be accepted
    const char *valid_bmp_json = "{\"name\": \"\\uFFFF\", \"slots\": {\"0\": \"#FF0000\"}}";
    memset(name, 0, sizeof(name));
    assert(ciy_parse_profile_json(valid_bmp_json, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);

    // Valides UTF-16 Surrogate-Paar: \uD83C\uDF89 (Party Popper Emoji U+1F389 -> \xF0\x9F\x8E\x89 in UTF-8)
    const char *valid_surrogate_pair = "{\"name\": \"Party \\uD83C\\uDF89\", \"slots\": {\"0\": \"#FF0000\"}}";
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(valid_surrogate_pair, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "Party \xF0\x9F\x8E\x89") == 0);

    const char *emoji_only = "{\"name\": \"\\uD83C\\uDF89\", \"slots\": {\"0\": \"#FF0000\"}}";
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(emoji_only, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "\xF0\x9F\x8E\x89") == 0);
    // Selektion nach UTF-8-Namen
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(emoji_only, "\xF0\x9F\x8E\x89", rgb, name, sizeof(name), &count));
    assert(count == 1);

    // Unvollständige oder fehlerhafte Surrogates abweisen
    // Isoliertes High-Surrogate am Stringende
    assert(!ciy_parse_profile_json("{\"name\": \"\\uD83C\", \"slots\": {\"0\": \"#FF0000\"}}", NULL, rgb, name, sizeof(name), &count));
    // High-Surrogate gefolgt von normalen Zeichen (kein \u)
    assert(!ciy_parse_profile_json("{\"name\": \"\\uD83Cabc\", \"slots\": {\"0\": \"#FF0000\"}}", NULL, rgb, name, sizeof(name), &count));
    // High-Surrogate gefolgt von Nicht-Low-Surrogate (z.B. BMP-Zeichen \u0041)
    assert(!ciy_parse_profile_json("{\"name\": \"\\uD83C\\u0041\", \"slots\": {\"0\": \"#FF0000\"}}", NULL, rgb, name, sizeof(name), &count));
    // High-Surrogate gefolgt von zweitem High-Surrogate
    assert(!ciy_parse_profile_json("{\"name\": \"\\uD83C\\uD800\", \"slots\": {\"0\": \"#FF0000\"}}", NULL, rgb, name, sizeof(name), &count));
    // Isoliertes Low-Surrogate
    assert(!ciy_parse_profile_json("{\"name\": \"\\uDC00\", \"slots\": {\"0\": \"#FF0000\"}}", NULL, rgb, name, sizeof(name), &count));
    assert(!ciy_parse_profile_json("{\"name\": \"\\uDF89\", \"slots\": {\"0\": \"#FF0000\"}}", NULL, rgb, name, sizeof(name), &count));
}

static void test_json_escape_decoding(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[128] = {0};
    int count = 0;

    const char *json =
        "{\"name\": \"A\\bB\\tC\\nD\\fE\\\"F\\\\G\\/H\", "
        "\"slots\": {\"0\": \"#FF0000\"}}";
    assert(ciy_parse_profile_json(json, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, "A\bB\tC\nD\fE\"F\\G/H") == 0);

    // Unbekanntes Escape muss abgelehnt werden
    const char *bad_escape =
        "{\"name\": \"Bad\\q\", \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(bad_escape, NULL, rgb, name, sizeof(name), &count));
}

static void test_duplicate_slots_count_once(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64] = {0};
    int count = 0;

    const char *json =
        "{\"name\": \"Dupe\", \"slots\": {"
        "\"0\": \"#FF0000\","
        "\"0\": \"#00FF00\""
        "}}";
    assert(ciy_parse_profile_json(json, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(rgb[1] == 0x00 && rgb[CIY_LED_COUNT + 1] == 0xFF && rgb[2 * CIY_LED_COUNT + 1] == 0x00);
}

static bool cancel_immediately(void *ctx) {
    (void)ctx;
    return true;
}

static bool cancel_on_third_call(void *ctx) {
    int *calls = (int *)ctx;
    (*calls)++;
    return *calls >= 3;
}

static void test_cancelable_apply(void) {
    CIYDevice d = {0};
    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
    assert(ciy_open(&d));

    // Sofortiger Abbruch: kein Hardware-Schritt, Bus-Lock bleibt sauber
    assert(!ciy_apply_custom_profile_with_brightness_cancelable(
        &d, rgb, sizeof(rgb), 5, cancel_immediately, NULL));
    assert(ciy_bus_lock());
    ciy_bus_unlock();

    // Abbruch zwischen den Schritten (nach Report 0x0B, in der 50-ms-Pause)
    int calls = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(!ciy_apply_custom_profile_with_brightness_cancelable(
        &d, rgb, sizeof(rgb), 5, cancel_on_third_call, &calls));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    assert(calls >= 3);
    assert(elapsed < 0.5); // deutlich unter der vollen 50-ms-Sequenz + ASan-Reserve

    // Ohne Cancel muss die Sequenz vollständig durchlaufen
    calls = 0;
    assert(ciy_apply_custom_profile_with_brightness_cancelable(
        &d, rgb, sizeof(rgb), 5, NULL, NULL));

    ciy_close(&d);
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

static void test_load_profile_file_schema_validation(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64];
    int count = 0;
    uint8_t brightness = 0;
    char path[256];
    snprintf(path, sizeof(path), "/tmp/ciy_test_schema_val_%d.json", (int)getpid());

    // 1. Ungültige Helligkeit (brightness = 9) -> ciy_validate_profile_json muss abweisen
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs("{\"name\": \"BadBright\", \"brightness\": 9, \"slots\": {\"0\": \"#FF0000\"}}", f);
    fclose(f);
    assert(!ciy_load_profile_file_with_brightness(path, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(!ciy_load_profile_file(path, NULL, rgb, name, sizeof(name), &count));
    unlink(path);

    // 2. Ungültiger Slot (slot = 130) -> muss abgewiesen werden
    f = fopen(path, "w");
    assert(f != NULL);
    fputs("{\"name\": \"BadSlot\", \"slots\": {\"130\": \"#FF0000\"}}", f);
    fclose(f);
    assert(!ciy_load_profile_file_with_brightness(path, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(!ciy_load_profile_file(path, NULL, rgb, name, sizeof(name), &count));
    unlink(path);

    // 3. Ungültige Hex-Farbe -> muss abgewiesen werden
    f = fopen(path, "w");
    assert(f != NULL);
    fputs("{\"name\": \"BadColor\", \"slots\": {\"0\": \"#ZZZZZZ\"}}", f);
    fclose(f);
    assert(!ciy_load_profile_file_with_brightness(path, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(!ciy_load_profile_file(path, NULL, rgb, name, sizeof(name), &count));
    unlink(path);

    // 4. Korruptes JSON -> muss abgewiesen werden
    f = fopen(path, "w");
    assert(f != NULL);
    fputs("{\"name\": \"BrokenJSON\", \"slots\": {", f);
    fclose(f);
    assert(!ciy_load_profile_file_with_brightness(path, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(!ciy_load_profile_file(path, NULL, rgb, name, sizeof(name), &count));
    unlink(path);

    // 5. Valides Profil -> muss erfolgreich laden
    f = fopen(path, "w");
    assert(f != NULL);
    fputs("{\"name\": \"ValidProf\", \"brightness\": 3, \"slots\": {\"0\": \"#00FF00\"}}", f);
    fclose(f);
    assert(ciy_load_profile_file_with_brightness(path, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(brightness == 3);
    assert(count == 1);
    assert(strcmp(name, "ValidProf") == 0);
    assert(ciy_load_profile_file(path, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    unlink(path);
}

static void test_parse_profile_json_brightness(void) {
    const char *json_with_bright = "{\"name\": \"Bright3\", \"brightness\": 3, \"slots\": {\"0\": \"#FF0000\"}}";
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64] = {0};
    int count = 0;
    uint8_t brightness = 0;

    assert(ciy_parse_profile_json_with_brightness(json_with_bright, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(count == 1);
    assert(brightness == 3);
    assert(strcmp(name, "Bright3") == 0);

    // Ohne Helligkeitsfeld: Default CIY_MAX_BRIGHTNESS (5)
    const char *json_no_bright = "{\"name\": \"NoBright\", \"slots\": {\"0\": \"#00FF00\"}}";
    brightness = 0;
    assert(ciy_parse_profile_json_with_brightness(json_no_bright, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(count == 1);
    assert(brightness == CIY_MAX_BRIGHTNESS);

    // Multi-Profil mit unterschiedlicher Helligkeit
    const char *multi_bright = "[{\"name\": \"P1\", \"brightness\": 1, \"slots\": {\"1\": \"#FF0000\"}}, {\"name\": \"P2\", \"brightness\": 4, \"slots\": {\"2\": \"#00FF00\"}}]";
    brightness = 0;
    assert(ciy_parse_profile_json_with_brightness(multi_bright, "P2", rgb, name, sizeof(name), &count, &brightness));
    assert(brightness == 4);
    assert(strcmp(name, "P2") == 0);

    // Helligkeit 0 (Grenzfall)
    const char *json_zero_bright = "{\"name\": \"ZeroBright\", \"brightness\": 0, \"slots\": {\"0\": \"#FF0000\"}}";
    brightness = 5;
    assert(ciy_parse_profile_json_with_brightness(json_zero_bright, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(count == 1);
    assert(brightness == 0);
    assert(strcmp(name, "ZeroBright") == 0);

    // Helligkeitsfeld VOR dem Namen (Schlüssel-Reihenfolge Unabhängigkeit)
    const char *json_bright_first = "{\"brightness\": 2, \"name\": \"PreBright\", \"slots\": {\"0\": \"#AABBCC\"}}";
    brightness = 0;
    assert(ciy_parse_profile_json_with_brightness(json_bright_first, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(count == 1);
    assert(brightness == 2);
    assert(strcmp(name, "PreBright") == 0);

    // Multi-Profil mit Helligkeit vor Name im zweiten Profil
    const char *multi_bright_first = "[{\"name\": \"P1\", \"slots\": {\"0\": \"#FF0000\"}}, {\"brightness\": 1, \"name\": \"P2\", \"slots\": {\"1\": \"#00FF00\"}}]";
    brightness = 0;
    assert(ciy_parse_profile_json_with_brightness(multi_bright_first, "P2", rgb, name, sizeof(name), &count, &brightness));
    assert(brightness == 1);
    assert(strcmp(name, "P2") == 0);

    // Ungültige Helligkeit (Out-of-Range 9) muss von ciy_validate_profile_json abgewiesen werden
    const char *json_bad_bright = "{\"name\": \"BadBright\", \"brightness\": 9, \"slots\": {\"0\": \"#FF0000\"}}";
    CIYValidationResult vres;
    assert(!ciy_validate_profile_json(json_bad_bright, &vres));
    assert(strcmp(vres.error_field, "brightness") == 0);

    // Laden von Datei mit Helligkeit
    const char *tmp_path = "/tmp/ciy_test_bright_profile.json";
    FILE *f = fopen(tmp_path, "w");
    assert(f != NULL);
    fputs("{\"name\": \"File Bright\", \"brightness\": 2, \"slots\": {\"0\": \"#112233\"}}", f);
    fclose(f);

    brightness = 0;
    assert(ciy_load_profile_file_with_brightness(tmp_path, NULL, rgb, name, sizeof(name), &count, &brightness));
    assert(brightness == 2);
    assert(strcmp(name, "File Bright") == 0);
    remove(tmp_path);
}

static void test_fifo_profile_rejection(void) {
    char fifo_path[128];
    snprintf(fifo_path, sizeof(fifo_path), "/tmp/ciy_test_fifo_%d.fifo", (int)getpid());
    unlink(fifo_path);

    int rc = mkfifo(fifo_path, 0600);
    assert(rc == 0);

    // 1. ciy_validate_profile_file auf FIFO muss sofort ohne Blockieren fehlschlagen
    CIYValidationResult res;
    bool valid = ciy_validate_profile_file(fifo_path, &res);
    assert(!valid);
    assert(strcmp(res.error_field, "file") == 0);
    assert(strstr(res.error_message, "keine reguläre Datei") != NULL);
    assert(res.read_error == CIYFileReadErrorNotRegularFile);

    // Auch ohne out_res muss es sofort false zurückgeben
    assert(!ciy_validate_profile_file(fifo_path, NULL));

    // 2. ciy_load_profile_file auf FIFO muss sofort ohne Blockieren fehlschlagen
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64] = {0};
    int count = 0;
    bool loaded = ciy_load_profile_file(fifo_path, NULL, rgb, name, sizeof(name), &count);
    assert(!loaded);

    // Aufräumen
    assert(unlink(fifo_path) == 0);
}

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
    char user_path[PATH_MAX];
    assert(ciy_bus_lock_path(user_path, sizeof(user_path)) != NULL);

    // Sicheres Öffnen mit O_NOFOLLOW und atomare Prüfung über Dateideskriptor (vermeidet TOCTOU-Race)
    int fd = open(user_path, O_RDONLY | O_NOFOLLOW);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0);
    // Prüfen, dass das Lockfile existiert, dem aktuellen Benutzer gehört und Modus 0600 hat
    assert((st.st_mode & 0777) == 0600);
    assert(st.st_uid == getuid());
    assert(S_ISREG(st.st_mode));
    close(fd);

    ciy_bus_unlock();
    assert(unlink(user_path) == 0);
}

static void test_bus_lock_path_isolated_per_user(void) {
    char buf[1024];
    char uid_str[32];
    char pid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%u", (unsigned)getuid());
    snprintf(pid_str, sizeof(pid_str), "%d", (int)getpid());

    // 1. Mit CIY_TESTING gesetzt: /tmp/ciy_x79_bus_%u_%d.lock
    const char *p = ciy_bus_lock_path(buf, sizeof(buf));
    assert(p != NULL);
    assert(strstr(p, "/tmp/ciy_x79_bus_") != NULL);
    assert(strstr(p, ".lock") != NULL);
    assert(strstr(p, uid_str) != NULL);
    assert(strstr(p, pid_str) != NULL);

    // Cross-UID-Verhalten bei Fast User Switching ist bewusst akzeptiertes
    // Restrisiko (SEC-T4-002); ein echter UID-Wechsel-Test würde Root-Rechte erfordern.

    // 2. NULL, 0 oder zu kleine Buffer Size abweisen (Rückgabe NULL, keine stille Trunkierung)
    assert(ciy_bus_lock_path(NULL, sizeof(buf)) == NULL);
    assert(ciy_bus_lock_path(buf, 0) == NULL);
    char tiny[10];
    assert(ciy_bus_lock_path(tiny, sizeof(tiny)) == NULL);

    // 3. Mit CIY_TESTING ungesetzt: isolierter Per-User-Temp-Pfad
    const char *orig_testing = getenv("CIY_TESTING");
    char saved_testing[64] = {0};
    if (orig_testing) strncpy(saved_testing, orig_testing, sizeof(saved_testing) - 1);
    unsetenv("CIY_TESTING");

    p = ciy_bus_lock_path(buf, sizeof(buf));
    assert(p != NULL);
    assert(strstr(p, uid_str) != NULL);
    assert(ciy_bus_lock_path(tiny, sizeof(tiny)) == NULL);
#if defined(__APPLE__) && defined(_CS_DARWIN_USER_TEMP_DIR)
    char darwin_temp[1024];
    if (confstr(_CS_DARWIN_USER_TEMP_DIR, darwin_temp, sizeof(darwin_temp)) > 1) {
        assert(strncmp(p, darwin_temp, strlen(darwin_temp)) == 0);
    }
#endif

    // 4. Relatives oder ungültiges TMPDIR darf nicht verwendet werden (muss absolut sein)
    const char *orig_tmp = getenv("TMPDIR");
    char saved_tmp[PATH_MAX] = {0};
    if (orig_tmp) strncpy(saved_tmp, orig_tmp, sizeof(saved_tmp) - 1);

    setenv("TMPDIR", "relative/tmp/dir", 1);
    char fallback_buf[1024];
    p = ciy_bus_lock_path(fallback_buf, sizeof(fallback_buf));
    assert(p != NULL);
    assert(strstr(p, uid_str) != NULL);
    assert(strncmp(p, "relative/", 9) != 0); // Niemals relativen Pfad als Lockfile nutzen

    if (orig_tmp) {
        setenv("TMPDIR", saved_tmp, 1);
    } else {
        unsetenv("TMPDIR");
    }

    // Wiederherstellen
    if (orig_testing) {
        setenv("CIY_TESTING", saved_testing, 1);
    }
}

static void test_lockfile_rejects_fifo(void) {
    char fifo_path[PATH_MAX];
    assert(ciy_bus_lock_path(fifo_path, sizeof(fifo_path)) != NULL);

    unlink(fifo_path);

    // Nicht-reguläre Datei als Lockfile abweisen
    assert(mkfifo(fifo_path, 0600) == 0);
    assert(!ciy_bus_lock());
    assert(unlink(fifo_path) == 0);

    // Verifizieren, dass der Lock-Mechanismus nach Abweisung im sauberen Zustand bleibt
    assert(ciy_bus_lock());
    ciy_bus_unlock();
    assert(unlink(fifo_path) == 0);
}

static void test_lockfile_rejects_symlink(void) {
    char symlink_path[PATH_MAX];
    assert(ciy_bus_lock_path(symlink_path, sizeof(symlink_path)) != NULL);

    unlink(symlink_path);

    // Symlink als Lockfile abweisen (O_NOFOLLOW schützt vor Link-Traversal)
    assert(symlink("/dev/null", symlink_path) == 0);
    assert(!ciy_bus_lock());
    assert(unlink(symlink_path) == 0);

    // Verifizieren, dass der Lock-Mechanismus nach Abweisung im sauberen Zustand bleibt
    assert(ciy_bus_lock());
    ciy_bus_unlock();
    assert(unlink(symlink_path) == 0);
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

static void test_json_string_length_limits(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64];
    int count = 0;

    // 1. Profilname überschreitet 64-Byte-Puffer (64 Zeichen String + \0 > 64)
    // Erwartet: Abweisung als Längen-/Syntaxfehler (keine stille Trunkierung)
    char long_name[70];
    memset(long_name, 'A', 64);
    long_name[64] = '\0';
    char json_buf[512];
    snprintf(json_buf, sizeof(json_buf), "{\"name\": \"%s\", \"slots\": {\"0\": \"#FF0000\"}}", long_name);
    memset(name, 0, sizeof(name));
    count = 0;
    assert(!ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));

    // 2. Profilname mit exakt 63 Zeichen (Grenzfall: passt inklusive '\0' genau in 64 Byte)
    memset(long_name, 'B', 63);
    long_name[63] = '\0';
    snprintf(json_buf, sizeof(json_buf), "{\"name\": \"%s\", \"slots\": {\"0\": \"#FF0000\"}}", long_name);
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, long_name) == 0);

    // 3. JSON-Key überschreitet 64-Byte-Puffer (64 Zeichen Key) -> Abweisung als Überlänge
    char long_key[70];
    memset(long_key, 'k', 64);
    long_key[64] = '\0';
    snprintf(json_buf, sizeof(json_buf), "{\"%s\": 123, \"name\": \"OK\", \"slots\": {\"0\": \"#FF0000\"}}", long_key);
    memset(name, 0, sizeof(name));
    count = 0;
    assert(!ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));

    // 4. Slot-Key überschreitet 32-Byte-Puffer (32 Zeichen) -> Abweisung als Überlänge
    char long_slot[40];
    memset(long_slot, '9', 32);
    long_slot[32] = '\0';
    snprintf(json_buf, sizeof(json_buf), "{\"name\": \"OK\", \"slots\": {\"%s\": \"#FF0000\"}}", long_slot);
    memset(name, 0, sizeof(name));
    count = 0;
    assert(!ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));

    // 5. String in unbekanntem Feld überschreitet CIY_JSON_MAX_STRLEN (4096) -> DoS-Schutz beim Überspringen
    char *huge_json = (char *)malloc(5000);
    assert(huge_json != NULL);
    strcpy(huge_json, "{\"name\": \"DoS\", \"extra\": \"");
    size_t prefix_len = strlen(huge_json);
    memset(huge_json + prefix_len, 'X', 4100);
    strcpy(huge_json + prefix_len + 4100, "\", \"slots\": {\"0\": \"#FF0000\"}}");
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(huge_json, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    free(huge_json);
}

static void test_json_string_bounds_and_unicode_escapes(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64];
    int count = 0;

    // 1. UTF-8 Surrogate Pair (\uD83D\uDE00, 4 Bytes UTF-8):
    // Name mit 60 Bytes 'A' + 4-Byte Emoji = 64 Bytes Inhalt + '\0' (benötigt 65 Bytes Puffer)
    // In 64-Byte-Puffer: muss als Überlauf abgewiesen werden!
    char buf[512];
    char prefix[64];
    memset(prefix, 'A', 60);
    prefix[60] = '\0';
    snprintf(buf, sizeof(buf), "{\"name\": \"%s\\uD83D\\uDE00\", \"slots\": {\"0\": \"#FF0000\"}}", prefix);
    assert(!ciy_parse_profile_json(buf, NULL, rgb, name, sizeof(name), &count));

    // 2. UTF-8 Surrogate Pair passend: 59 Bytes 'A' + 4-Byte Emoji = 63 Bytes + '\0' = 64 Bytes (genau passend)
    prefix[59] = '\0';
    snprintf(buf, sizeof(buf), "{\"name\": \"%s\\uD83D\\uDE00\", \"slots\": {\"0\": \"#FF0000\"}}", prefix);
    assert(ciy_parse_profile_json(buf, NULL, rgb, name, sizeof(name), &count));
    assert(strlen(name) == 63);

    // 3. UTF-8 BMP 3-Byte Codepoint (\u20AC, Euro, 3 Bytes UTF-8):
    // 61 Bytes 'A' + 3 Bytes = 64 Bytes Inhalt (benötigt 65 Bytes Puffer) -> Abweisung
    memset(prefix, 'A', 61);
    prefix[61] = '\0';
    snprintf(buf, sizeof(buf), "{\"name\": \"%s\\u20AC\", \"slots\": {\"0\": \"#FF0000\"}}", prefix);
    assert(!ciy_parse_profile_json(buf, NULL, rgb, name, sizeof(name), &count));

    // 4. UTF-8 BMP 3-Byte passend: 60 Bytes 'A' + 3 Bytes = 63 Bytes + '\0' = 64 Bytes -> Erfolg
    prefix[60] = '\0';
    snprintf(buf, sizeof(buf), "{\"name\": \"%s\\u20AC\", \"slots\": {\"0\": \"#FF0000\"}}", prefix);
    assert(ciy_parse_profile_json(buf, NULL, rgb, name, sizeof(name), &count));
    assert(strlen(name) == 63);

    // 5. UTF-8 BMP 2-Byte Codepoint (\u00A9, Copyright, 2 Bytes UTF-8):
    // 62 Bytes 'A' + 2 Bytes = 64 Bytes Inhalt -> Abweisung
    memset(prefix, 'A', 62);
    prefix[62] = '\0';
    snprintf(buf, sizeof(buf), "{\"name\": \"%s\\u00A9\", \"slots\": {\"0\": \"#FF0000\"}}", prefix);
    assert(!ciy_parse_profile_json(buf, NULL, rgb, name, sizeof(name), &count));

    // 6. UTF-8 BMP 2-Byte passend: 61 Bytes 'A' + 2 Bytes = 63 Bytes + '\0' = 64 Bytes -> Erfolg
    prefix[61] = '\0';
    snprintf(buf, sizeof(buf), "{\"name\": \"%s\\u00A9\", \"slots\": {\"0\": \"#FF0000\"}}", prefix);
    assert(ciy_parse_profile_json(buf, NULL, rgb, name, sizeof(name), &count));
    assert(strlen(name) == 63);
}

static void test_corrupt_strings_in_unknown_fields(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64];
    int count = 0;

    // 1. Nicht-terminierter String in verschachteltem Objekt in unbekanntem Feld
    const char *unterminated_in_obj =
        "{\"name\": \"Profile\", \"unknown\": {\"bad\": \"unterminated string}, \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(unterminated_in_obj, NULL, rgb, name, sizeof(name), &count));

    // 2. Abgeschnittener Unicode-Escape in verschachteltem Objekt
    const char *truncated_unicode_in_obj =
        "{\"name\": \"Profile\", \"unknown\": {\"bad\": \"\\u12\"}, \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(truncated_unicode_in_obj, NULL, rgb, name, sizeof(name), &count));

    // 3. Ungültiges Surrogate in verschachteltem Objekt
    const char *bad_surrogate_in_obj =
        "{\"name\": \"Profile\", \"unknown\": {\"bad\": \"\\uD800\"}, \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(bad_surrogate_in_obj, NULL, rgb, name, sizeof(name), &count));

    // 4. Nicht-terminierter String in Array in unbekanntem Feld
    const char *unterminated_in_arr =
        "{\"name\": \"Profile\", \"unknown\": [\"unterminated array], \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(unterminated_in_arr, NULL, rgb, name, sizeof(name), &count));

    // 5. Abgeschnittener Unicode-Escape in Array
    const char *truncated_unicode_in_arr =
        "{\"name\": \"Profile\", \"unknown\": [\"\\u99\"], \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(truncated_unicode_in_arr, NULL, rgb, name, sizeof(name), &count));

    // 6. Nicht-terminierter skalarer String in unbekanntem Feld
    const char *unterminated_scalar =
        "{\"name\": \"Profile\", \"unknown\": \"unterminated scalar, \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(unterminated_scalar, NULL, rgb, name, sizeof(name), &count));

    // 7. Abgeschnittener Escape in unbekanntem skalarem String
    const char *truncated_scalar =
        "{\"name\": \"Profile\", \"unknown\": \"\\u123\", \"slots\": {\"0\": \"#FF0000\"}}";
    assert(!ciy_parse_profile_json(truncated_scalar, NULL, rgb, name, sizeof(name), &count));

    // 8. Völlig unvollständiges JSON mit korruptem String am Ende
    const char *cut_off_escape =
        "{\"name\": \"Profile\", \"unknown\": {\"bad\": \"\\u";
    assert(!ciy_parse_profile_json(cut_off_escape, NULL, rgb, name, sizeof(name), &count));
}

static void test_layout_c_model(void) {
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    assert(layout != NULL);
    assert(strcmp(layout->layout_id, "ciy_x79_iso_de") == 0);
    assert(layout->key_count == 69);
    assert(layout->surface_count == 70);

    bool seen_slots[128] = {false};
    for (size_t i = 0; i < layout->key_count; i++) {
        const CIYLogicalKey *k = &layout->keys[i];
        assert(k->matrix_slot >= 0 && k->matrix_slot <= 124);
        assert(k->led_index == k->matrix_slot + 1);
        assert(k->name != NULL && strlen(k->name) > 0);
        assert(!seen_slots[k->matrix_slot]);
        seen_slots[k->matrix_slot] = true;
    }

    int enter_surfaces = 0;
    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *s = &layout->surfaces[i];
        assert(s->matrix_slot >= 0 && s->matrix_slot <= 124);
        assert(s->row >= 0 && s->row <= 4);
        assert(s->width_u >= 1.0f);
        assert(s->label != NULL);

        const CIYLogicalKey *k = ciy_layout_find_key_by_slot(layout, s->matrix_slot);
        assert(k != NULL);
        assert(k->matrix_slot == s->matrix_slot);

        if (s->matrix_slot == 81) {
            enter_surfaces++;
            assert(s->row == 1 || s->row == 2);
        }
    }
    assert(enter_surfaces == 2);

    assert(ciy_layout_find_key_by_slot(layout, 1) != NULL);
    assert(ciy_layout_find_key_by_slot(layout, 81) != NULL);
    assert(ciy_layout_find_key_by_slot(layout, -1) == NULL);
    assert(ciy_layout_find_key_by_slot(layout, 126) == NULL);
    assert(ciy_layout_find_key_by_name(layout, "Esc") != NULL);
    assert(ciy_layout_find_key_by_name(layout, "↵ Enter") != NULL);
    assert(ciy_layout_find_key_by_name(layout, "INVALID") == NULL);
}

static void test_profile_validation(void) {
    CIYValidationResult res;

    // 1. Valides Single-Profil
    const char *valid_single = "{\"name\": \"Single\", \"slots\": {\"0\": \"#FF0000\", \"14\": \"#00FF00\"}}";
    assert(ciy_validate_profile_json(valid_single, &res));
    assert(res.is_valid);
    assert(res.profile_count == 1);
    assert(res.total_keys_count == 2);

    // 2. Valides Multi-Profil
    const char *valid_multi = "[{\"name\": \"P1\", \"slots\": {\"1\": \"#FF0000\"}}, {\"name\": \"P2\", \"slots\": {\"2\": \"#00FF00\"}}]";
    assert(ciy_validate_profile_json(valid_multi, &res));
    assert(res.is_valid);
    assert(res.profile_count == 2);
    assert(res.total_keys_count == 2);

    // 3. Valides Profil mit Helligkeit
    const char *valid_bright = "{\"name\": \"Bright\", \"brightness\": 3, \"slots\": {\"1\": \"#123456\"}}";
    assert(ciy_validate_profile_json(valid_bright, &res));
    assert(res.is_valid);

    // 4. Ungültige Helligkeit (> 5)
    const char *bad_bright = "{\"name\": \"BadBright\", \"brightness\": 6, \"slots\": {\"1\": \"#123456\"}}";
    assert(!ciy_validate_profile_json(bad_bright, &res));
    assert(!res.is_valid);
    assert(strcmp(res.error_field, "brightness") == 0);

    // 5. Ungültiger Slot (> 124, z. B. 125 oder 200)
    const char *bad_slot = "{\"name\": \"BadSlot\", \"slots\": {\"125\": \"#FF0000\"}}";
    assert(!ciy_validate_profile_json(bad_slot, &res));
    assert(!res.is_valid);
    assert(strcmp(res.error_field, "125") == 0);

    // 6. Negativer Slot
    const char *neg_slot = "{\"name\": \"NegSlot\", \"slots\": {\"-1\": \"#FF0000\"}}";
    assert(!ciy_validate_profile_json(neg_slot, &res));
    assert(!res.is_valid);

    // 7. Ungültige Hex-Farbe
    const char *bad_color = "{\"name\": \"BadColor\", \"slots\": {\"1\": \"#XYZ123\"}}";
    assert(!ciy_validate_profile_json(bad_color, &res));
    assert(!res.is_valid);
    assert(strcmp(res.error_field, "1") == 0);

    // 8. Leere Slots
    const char *empty_slots = "{\"name\": \"EmptySlots\", \"slots\": {}}";
    assert(!ciy_validate_profile_json(empty_slots, &res));
    assert(!res.is_valid);
    assert(strcmp(res.error_field, "slots") == 0);

    // 9. Ungültige Syntax
    const char *bad_syntax = "{\"name\": \"Broken\", \"slots\": {";
    assert(!ciy_validate_profile_json(bad_syntax, &res));
    assert(!res.is_valid);

    // 10. NULL / Leer
    assert(!ciy_validate_profile_json(NULL, &res));
    assert(!ciy_validate_profile_json("", &res));
    assert(!ciy_validate_profile_json("[]", &res));

    // 11. Fixtures auf Platte testen
    assert(ciy_validate_profile_file("tests/fixtures/valid_profile.json", &res));
    assert(res.is_valid && res.profile_count == 1);

    assert(ciy_validate_profile_file("tests/fixtures/valid_multi_profile.json", &res));
    assert(res.is_valid && res.profile_count == 2);

    assert(!ciy_validate_profile_file("tests/fixtures/invalid_syntax.json", &res));
    assert(!res.is_valid);

    assert(!ciy_validate_profile_file("tests/fixtures/invalid_slot_range.json", &res));
    assert(!res.is_valid && strcmp(res.error_field, "130") == 0);

    assert(!ciy_validate_profile_file("tests/fixtures/invalid_color.json", &res));
    assert(!res.is_valid && strcmp(res.error_field, "14") == 0);

    assert(!ciy_validate_profile_file("tests/fixtures/invalid_brightness.json", &res));
    assert(!res.is_valid && strcmp(res.error_field, "brightness") == 0);

    assert(!ciy_validate_profile_file("tests/fixtures/non_existent_file.json", &res));
    assert(!res.is_valid);
    assert(res.read_error == CIYFileReadErrorOpenFailed);
}

static void test_transfer_plan(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    memset(rgb, 0x55, sizeof(rgb));
    CIYTransferPlan plan;

    // 1. Plan-Generierung prüfen
    assert(ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), 3, "Cyberpunk", 69));
    assert(plan.step_count == 3);
    assert(strcmp(plan.profile_name, "Cyberpunk") == 0);
    assert(plan.keys_count == 69);
    assert(plan.brightness == 3);

    // Schritt 1: Report 0x0B (379 Bytes)
    assert(plan.steps[0].type == CIY_PLAN_STEP_REPORT);
    assert(plan.steps[0].report_id == 0x0B);
    assert(plan.steps[0].length == 379);
    assert(plan.steps[0].data[0] == 0x0B);
    assert(memcmp(&plan.steps[0].data[1], rgb, CIY_PLANAR_RGB_SIZE) == 0);

    // Schritt 2: Delay 50 ms
    assert(plan.steps[1].type == CIY_PLAN_STEP_DELAY);
    assert(plan.steps[1].delay_ms == 50);

    // Schritt 3: Report 0x0A (42 Bytes, Modus 19, Helligkeit 3)
    assert(plan.steps[2].type == CIY_PLAN_STEP_REPORT);
    assert(plan.steps[2].report_id == 0x0A);
    assert(plan.steps[2].length == 42);
    assert(plan.steps[2].data[0] == 0x0A);
    assert(plan.steps[2].data[1] == 19);
    assert(plan.steps[2].data[4] == 3);
    assert(plan.steps[2].mode == 19);
    assert(plan.steps[2].brightness == 3);

    // Ungültige Parameter für Plan-Aufbau
    CIYTransferPlan bad_plan;
    assert(!ciy_build_transfer_plan(NULL, rgb, sizeof(rgb), 3, "X", 1));
    assert(!ciy_build_transfer_plan(&bad_plan, NULL, sizeof(rgb), 3, "X", 1));
    assert(!ciy_build_transfer_plan(&bad_plan, rgb, sizeof(rgb) - 1, 3, "X", 1));

    // 2. Plan-Ausführung prüfen
    CIYDevice dev = {0};
    assert(ciy_open(&dev));
    assert(ciy_execute_transfer_plan(&dev, &plan, NULL, NULL));

    // Ausführung mit Abbruch (cancel)
    assert(!ciy_execute_transfer_plan(&dev, &plan, cancel_immediately, NULL));

    // Ausführung mit unzulässiger Schritt-Länge (Schutz vor Stack-Pufferüberlauf)
    CIYTransferPlan invalid_plan = plan;
    invalid_plan.steps[0].length = (uint16_t)(sizeof(invalid_plan.steps[0].data) + 10);
    assert(!ciy_execute_transfer_plan(&dev, &invalid_plan, NULL, NULL));

    invalid_plan.steps[0].length = 0;
    assert(!ciy_execute_transfer_plan(&dev, &invalid_plan, NULL, NULL));

    // Ausführung mit unzulässiger Schritt-Anzahl (OOB-Schutz SEC-T2-001)
    CIYTransferPlan oob_plan = plan;
    oob_plan.step_count = CIY_MAX_PLAN_STEPS + 1;
    assert(!ciy_execute_transfer_plan(&dev, &oob_plan, NULL, NULL));
}

static void test_read_text_file_validation_result_init(void) {
    CIYValidationResult res;
    char path[128];

    // 1. Nicht-existierende Datei
    snprintf(path, sizeof(path), "/tmp/ciy_nonexist_%d.json", (int)getpid());
    unlink(path);
    memset(&res, 0xAA, sizeof(res));
    size_t out_size = 999;
    char *buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(res.error_message[0] != '\0');
    assert(strcmp(res.error_field, "file") == 0);
    assert(res.read_error == CIYFileReadErrorOpenFailed);

    // 2. NULL-Dateipfad
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(NULL, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(res.error_message[0] != '\0');
    assert(strcmp(res.error_field, "file") == 0);
    assert(res.read_error == CIYFileReadErrorNullPath);

    // 3. Verzeichnis abweisen
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(".", &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(res.error_message[0] != '\0');
    assert(strcmp(res.error_field, "file") == 0);
    assert(res.read_error == CIYFileReadErrorNotRegularFile);

    // 4. Leere Datei abweisen
    snprintf(path, sizeof(path), "/tmp/ciy_empty_%d.txt", (int)getpid());
    unlink(path);
    FILE *ef = fopen(path, "w");
    assert(ef != NULL);
    fclose(ef);
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(res.error_message[0] != '\0');
    assert(strcmp(res.error_field, "file") == 0);
    assert(res.read_error == CIYFileReadErrorEmptyFile);
    unlink(path);

    // 5. FIFO abweisen (nicht-blockierend)
    snprintf(path, sizeof(path), "/tmp/ciy_fifo_%d.fifo", (int)getpid());
    unlink(path);
    assert(mkfifo(path, 0600) == 0);
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(strcmp(res.error_field, "file") == 0);
    assert(strstr(res.error_message, "keine reguläre Datei") != NULL);
    assert(res.read_error == CIYFileReadErrorNotRegularFile);
    unlink(path);

    // 6. Zyklischen Symlink sowie Symlink auf reguläre Datei abweisen (O_NOFOLLOW)
    snprintf(path, sizeof(path), "/tmp/ciy_symloop_%d.json", (int)getpid());
    unlink(path);
    assert(symlink(path, path) == 0);
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(strcmp(res.error_field, "file") == 0);
    assert(res.read_error == CIYFileReadErrorSymlinkRejected);
    unlink(path);

    char target_path[128];
    snprintf(target_path, sizeof(target_path), "/tmp/ciy_symtarget_%d.json", (int)getpid());
    snprintf(path, sizeof(path), "/tmp/ciy_symlink_reg_%d.json", (int)getpid());
    unlink(target_path);
    unlink(path);
    FILE *tf = fopen(target_path, "w");
    assert(tf != NULL);
    fprintf(tf, "{\"name\": \"Target\", \"slots\": {\"1\": \"#FF0000\"}}");
    fclose(tf);
    assert(symlink(target_path, path) == 0);
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(strcmp(res.error_field, "file") == 0);
    assert(res.read_error == CIYFileReadErrorSymlinkRejected);
    assert(!ciy_validate_profile_file(path, &res));
    uint8_t sym_rgb[CIY_PLANAR_RGB_SIZE];
    char sym_name[64];
    int sym_count = 0;
    assert(!ciy_load_profile_file(path, NULL, sym_rgb, sym_name, sizeof(sym_name), &sym_count));
    unlink(path);
    unlink(target_path);

    // 7. Datei mit Null-Bytes abweisen (Binärdatei-Schutz)
    snprintf(path, sizeof(path), "/tmp/ciy_nullbyte_%d.json", (int)getpid());
    unlink(path);
    FILE *bf = fopen(path, "wb");
    assert(bf != NULL);
    char bin_data[] = {'{', '"', 'a', '"', ':', ' ', 0, '}'};
    assert(fwrite(bin_data, 1, sizeof(bin_data), bf) == sizeof(bin_data));
    fclose(bf);
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(strcmp(res.error_field, "syntax") == 0);
    assert(strstr(res.error_message, "Null-Bytes") != NULL);
    assert(res.read_error == CIYFileReadErrorReadFailed);
    unlink(path);

    // 8. Datei > 512 KB abweisen
    snprintf(path, sizeof(path), "/tmp/ciy_oversized_%d.json", (int)getpid());
    unlink(path);
    FILE *of = fopen(path, "wb");
    assert(of != NULL);
    assert(fseek(of, 513 * 1024, SEEK_SET) == 0);
    assert(fputc('}', of) == '}');
    fclose(of);
    memset(&res, 0xAA, sizeof(res));
    out_size = 999;
    buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf == NULL);
    assert(out_size == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(strcmp(res.error_field, "file") == 0);
    assert(strstr(res.error_message, "512 KB") != NULL);
    assert(res.read_error == CIYFileReadErrorTooLarge);
    unlink(path);

    // 9. Erfolgreiches Lesen initialisiert out_res sauber (keine 0xAA Dummy-Werte)
    snprintf(path, sizeof(path), "/tmp/ciy_valid_%d.json", (int)getpid());
    unlink(path);
    FILE *vf = fopen(path, "w");
    assert(vf != NULL);
    const char *content = "{\"slots\":{}}";
    fputs(content, vf);
    fclose(vf);
    memset(&res, 0xAA, sizeof(res));
    out_size = 0;
    buf = ciy_read_text_file_with_result(path, &out_size, &res);
    assert(buf != NULL);
    assert(out_size == strlen(content));
    assert(strcmp(buf, content) == 0);
    assert(res.is_valid == false);
    assert(res.profile_count == 0);
    assert(res.total_keys_count == 0);
    assert(res.error_message[0] == '\0');
    assert(res.error_field[0] == '\0');
    assert(res.read_error == CIYFileReadErrorNone);
    free(buf);

    // 10. Convenience-Funktion ciy_read_text_file testen
    size_t conv_size = 0;
    buf = ciy_read_text_file(path, &conv_size);
    assert(buf != NULL);
    assert(conv_size == strlen(content));
    assert(strcmp(buf, content) == 0);
    free(buf);
    unlink(path);

    // 11. Robuste Behandlung von NULL out_size und NULL out_res
    assert(ciy_read_text_file_with_result(NULL, NULL, NULL) == NULL);
    snprintf(path, sizeof(path), "/tmp/ciy_nonexist_%d.json", (int)getpid());
    unlink(path);
    assert(ciy_read_text_file_with_result(path, NULL, NULL) == NULL);
}

static void test_trailing_comma_rejection(void) {
    CIYValidationResult res;

    // Trailing comma im 'slots'-Objekt (SEC-T2-010)
    assert(!ciy_validate_profile_json("{\"slots\": {\"0\": \"#FF0000\", }}", &res));
    assert(!res.is_valid);
    assert(strcmp(res.error_field, "slots") == 0);

    // Trailing comma im Profil-Objekt
    assert(!ciy_validate_profile_json("{\"name\": \"P\", \"slots\": {\"0\": \"#FF0000\"}, }", &res));
    assert(!res.is_valid);
    assert(strcmp(res.error_field, "syntax") == 0);

    // Trailing comma im Profil-Array
    assert(!ciy_validate_profile_json("[{\"name\": \"P\", \"slots\": {\"0\": \"#FF0000\"}}, ]", &res));
    assert(!res.is_valid);
    assert(strcmp(res.error_field, "syntax") == 0);

    // Auch ciy_parse_profile_json_with_brightness muss Trailing Commas ablehnen
    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
    assert(!ciy_parse_profile_json_with_brightness("{\"slots\": {\"0\": \"#FF0000\", }}", NULL, rgb, NULL, 0, NULL, NULL));
    assert(!ciy_parse_profile_json_with_brightness("{\"name\": \"P\", \"slots\": {\"0\": \"#FF0000\"}, }", NULL, rgb, NULL, 0, NULL, NULL));
    assert(!ciy_parse_profile_json_with_brightness("[{\"name\": \"P\", \"slots\": {\"0\": \"#FF0000\"}}, ]", NULL, rgb, NULL, 0, NULL, NULL));
}

static volatile sig_atomic_t s_sigalrm_fired = 0;
static void handle_sigalrm(int sig) {
    (void)sig;
    s_sigalrm_fired = 1;
}

static void test_transfer_plan_delay_eintr(void) {
    CIYDevice dev = {0};
    assert(ciy_open(&dev));

    s_sigalrm_fired = 0;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigalrm;
    sigaction(SIGALRM, &sa, NULL);

    CIYTransferPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.step_count = 1;
    plan.steps[0].type = CIY_PLAN_STEP_DELAY;
    plan.steps[0].delay_ms = 20;

    struct itimerval itv = {
        .it_interval = {.tv_sec = 0, .tv_usec = 0},
        .it_value = {.tv_sec = 0, .tv_usec = 2000} // Alarm nach 2 ms
    };
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    setitimer(ITIMER_REAL, &itv, NULL);

    assert(ciy_execute_transfer_plan(&dev, &plan, NULL, NULL));

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;

    assert(s_sigalrm_fired == 1);
    assert(elapsed >= 0.018);

    struct itimerval disarm;
    memset(&disarm, 0, sizeof(disarm));
    setitimer(ITIMER_REAL, &disarm, NULL);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGALRM, &sa, NULL);
    ciy_close(&dev);
}

static void test_nanosleep_uninterrupted_eintr(void) {
    s_sigalrm_fired = 0;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigalrm;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval itv = {
        .it_interval = {.tv_sec = 0, .tv_usec = 0},
        .it_value = {.tv_sec = 0, .tv_usec = 5000} // Alarm nach 5 ms
    };
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    setitimer(ITIMER_REAL, &itv, NULL);

    ciy_test_nanosleep_uninterrupted(30000000L); // 30 ms

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;

    assert(s_sigalrm_fired == 1);
    assert(elapsed >= 0.028); // garantiert volle Schlafdauer trotz EINTR

    struct itimerval disarm;
    memset(&disarm, 0, sizeof(disarm));
    setitimer(ITIMER_REAL, &disarm, NULL);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGALRM, &sa, NULL);
}

static void test_bus_lock_fork_safety(void) {
    assert(ciy_bus_lock());
    char parent_lock_path[PATH_MAX];
    assert(ciy_bus_lock_path(parent_lock_path, sizeof(parent_lock_path)) != NULL);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        // Kindprozess ruft ciy_bus_unlock auf: darf den Eltern-flock NICHT freigeben!
        ciy_bus_unlock();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    // Elternprozess muss weiterhin exklusiven Lock halten
    pid_t probe_pid = fork();
    assert(probe_pid >= 0);
    if (probe_pid == 0) {
        int fd = open(parent_lock_path, O_RDWR);
        if (fd >= 0) {
            int r = flock(fd, LOCK_EX | LOCK_NB);
            close(fd);
            if (r == 0) {
                // FEHLER: Eltern-Lock wurde fälschlicherweise freigegeben
                _exit(1);
            }
        }
        _exit(0);
    }
    assert(waitpid(probe_pid, &status, 0) == probe_pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    ciy_bus_unlock();
}

static void test_test_lockfile_atexit_cleanup(void) {
    int fds[2];
    assert(pipe(fds) == 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(fds[0]);
        assert(ciy_bus_lock());
        char path[PATH_MAX];
        assert(ciy_bus_lock_path(path, sizeof(path)) != NULL);
        size_t len = strlen(path) + 1;
        assert(write(fds[1], path, len) == (ssize_t)len);
        close(fds[1]);
        ciy_bus_unlock();
        exit(0); // Triggert atexit(ciy_cleanup_test_lockfile)
    }
    close(fds[1]);
    char child_path[PATH_MAX] = {0};
    ssize_t n = read(fds[0], child_path, sizeof(child_path) - 1);
    close(fds[0]);
    assert(n > 0);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    // Prüfe, dass das Test-Lockfile durch atexit aufgeräumt wurde
    struct stat st;
    assert(stat(child_path, &st) != 0 && errno == ENOENT);
}

// ===========================================================================
// Phase 1 CWE-Top-25 Blind-Spot-Matrix Tests (1a..3b + Seed-Corpus)
// ===========================================================================

static bool cancel_after_first_chunk(void *ctx) {
    int *calls = (int *)ctx;
    (*calls)++;
    // Aufruf 1: Vorab-Check vor der Schleife; Aufruf 2: Vorab-Check Schritt 0;
    // Aufruf 3: Nach dem 1. 5-ms-Delay-Chunk -> sofort abbrechen (kein Endlos-Sleep!)
    return (*calls >= 3);
}

static void test_cwe_1a_integer_overflow_and_delay_wraparound(void) {
    CIYDevice dev = {0};
    assert(ciy_open(&dev));

    // Referenz: Bei 50 ms wird mindestens 1 Chunk geschlafen und bei Aufruf 3 abgebrochen
    CIYTransferPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.step_count = 1;
    plan.steps[0].type = CIY_PLAN_STEP_DELAY;
    plan.steps[0].delay_ms = 50;
    int calls = 0;
    assert(!ciy_execute_transfer_plan(&dev, &plan, cancel_after_first_chunk, &calls));
    assert(calls == 3);

    // 1a: Delay-Wraparound bei delay_ms = UINT32_MAX und UINT32_MAX - 3
    const uint32_t wrap_delays[] = {UINT32_MAX, UINT32_MAX - 3u};
    for (size_t i = 0; i < sizeof(wrap_delays) / sizeof(wrap_delays[0]); i++) {
        plan.steps[0].delay_ms = wrap_delays[i];
        calls = 0;
        bool ok = ciy_execute_transfer_plan(&dev, &plan, cancel_after_first_chunk, &calls);
        assert(!ok && calls == 3);
    }

    // 1a: ciy_send_planar_rgb / ciy_apply_custom_profile_with_brightness mit rgb_len = 379 & SIZE_MAX
    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
    assert(!ciy_send_planar_rgb(&dev, rgb, CIY_REPORT_0B_SIZE)); // 379 != 378
    assert(!ciy_send_planar_rgb(&dev, rgb, SIZE_MAX));
    assert(!ciy_apply_custom_profile_with_brightness(&dev, rgb, CIY_REPORT_0B_SIZE, 5));
    assert(!ciy_apply_custom_profile_with_brightness(&dev, rgb, SIZE_MAX, 5));
    ciy_close(&dev);

    // 1a: ciy_build_profile mit count = -1, INT_MIN sowie Indizes INT_MIN / INT_MAX
    int single_idx[1] = {15};
    memset(rgb, 0xAA, sizeof(rgb));
    ciy_build_profile(rgb, single_idx, -1, true, true, false);
    for (size_t i = 0; i < sizeof(rgb); i++) assert(rgb[i] == 0x00);

    memset(rgb, 0xAA, sizeof(rgb));
    ciy_build_profile(rgb, single_idx, INT_MIN, true, true, true);
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        assert(rgb[i] == 0x00 && rgb[CIY_LED_COUNT + i] == 0x00 && rgb[2 * CIY_LED_COUNT + i] == 0xFF);
    }

    const int extreme_indices[] = {INT_MIN, -1, 0, 125, 126, INT_MAX};
    ciy_build_profile(rgb, extreme_indices, 6, true, false, false);
    assert(rgb[0] == 0xFF && rgb[125] == 0xFF);
    for (int i = 1; i < 125; i++) assert(rgb[i] == 0x00);
}

static void test_cwe_1b_off_by_one_boundaries(void) {
    // 1. Exakt 524288 Bytes (512 * 1024) Erfolg vs. 524289 Bytes Ablehnung
    char path[256];
    snprintf(path, sizeof(path), "/tmp/ciy_exact_512k_%d.json", (int)getpid());
    unlink(path);

    const char *json_head = "{\"name\":\"Exact512K\",\"slots\":{\"0\":\"#FF0000\"}}";
    size_t head_len = strlen(json_head);
    const size_t exact_512k = 512 * 1024; // 524288
    char *file_buf = (char *)malloc(exact_512k + 1);
    assert(file_buf != NULL);
    memset(file_buf, ' ', exact_512k + 1);
    memcpy(file_buf, json_head, head_len);

    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(file_buf, 1, exact_512k, f) == exact_512k);
    fclose(f);

    size_t read_sz = 0;
    CIYValidationResult vres;
    char *loaded_txt = ciy_read_text_file_with_result(path, &read_sz, &vres);
    assert(loaded_txt != NULL);
    assert(read_sz == exact_512k);
    assert(vres.read_error == CIYFileReadErrorNone);
    free(loaded_txt);
    assert(ciy_validate_profile_file(path, &vres) && vres.is_valid);

    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char prof_name[64];
    int slots_cnt = 0;
    uint8_t bright = 0;
    assert(ciy_load_profile_file_with_brightness(path, NULL, rgb, prof_name, sizeof(prof_name), &slots_cnt, &bright));
    assert(slots_cnt == 1 && strcmp(prof_name, "Exact512K") == 0);

    // 524289 Bytes (+1 Byte über 512 KB) -> strikte Ablehnung
    f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(file_buf, 1, exact_512k + 1, f) == exact_512k + 1);
    fclose(f);

    read_sz = 777;
    assert(ciy_read_text_file_with_result(path, &read_sz, &vres) == NULL);
    assert(read_sz == 0 && vres.read_error == CIYFileReadErrorTooLarge);
    assert(!ciy_validate_profile_file(path, &vres));
    assert(!ciy_load_profile_file_with_brightness(path, NULL, rgb, prof_name, sizeof(prof_name), &slots_cnt, &bright));
    unlink(path);
    free(file_buf);

    // 2. ciy_build_profile Canary-Test für Rand-LEDs 0, 125 (CIY_LED_COUNT - 1), -1, 126
    struct {
        uint8_t pre[16];
        uint8_t rgb[CIY_PLANAR_RGB_SIZE];
        uint8_t post[16];
    } canary_prof;
    memset(canary_prof.pre, 0xAA, sizeof(canary_prof.pre));
    memset(canary_prof.post, 0x55, sizeof(canary_prof.post));
    const int edge_leds[] = {-1, 0, CIY_LED_COUNT - 1, CIY_LED_COUNT};
    ciy_build_profile(canary_prof.rgb, edge_leds, 4, true, true, false);
    assert(canary_prof.rgb[0] == 0xFF && canary_prof.rgb[CIY_LED_COUNT] == 0xFF && canary_prof.rgb[2 * CIY_LED_COUNT] == 0x00);
    assert(canary_prof.rgb[125] == 0xFF && canary_prof.rgb[CIY_LED_COUNT + 125] == 0xFF && canary_prof.rgb[2 * CIY_LED_COUNT + 125] == 0x00);
    for (int i = 1; i < 125; i++) {
        assert(canary_prof.rgb[i] == 0 && canary_prof.rgb[CIY_LED_COUNT + i] == 0 && canary_prof.rgb[2 * CIY_LED_COUNT + i] == 0);
    }
    for (size_t i = 0; i < 16; i++) {
        assert(canary_prof.pre[i] == 0xAA && canary_prof.post[i] == 0x55);
    }

    // 3. slot_key & hex_val exakt 31 Bytes (Erfolg) vs. 32 Bytes (Ablehnung in char[32])
    const char *slot_31_ok = "{\"name\":\"S31\",\"slots\":{\"0000000000000000000000000000124\":\"#FF00AA\"}}";
    const char *slot_32_fail = "{\"name\":\"S32\",\"slots\":{\"00000000000000000000000000000124\":\"#FF00AA\"}}";
    assert(ciy_validate_profile_json(slot_31_ok, &vres));
    assert(ciy_parse_profile_json_with_brightness(slot_31_ok, NULL, rgb, prof_name, sizeof(prof_name), &slots_cnt, &bright));
    assert(slots_cnt == 1 && rgb[125] == 0xFF && rgb[2 * CIY_LED_COUNT + 125] == 0xAA);
    assert(!ciy_validate_profile_json(slot_32_fail, &vres));
    assert(!ciy_parse_profile_json_with_brightness(slot_32_fail, NULL, rgb, prof_name, sizeof(prof_name), &slots_cnt, &bright));

    const char *hex_31_ok = "{\"name\":\"H31\",\"slots\":{\"14\":\"   #112233                     \"}}";
    const char *hex_32_fail = "{\"name\":\"H32\",\"slots\":{\"14\":\"   #112233                      \"}}";
    assert(ciy_validate_profile_json(hex_31_ok, &vres));
    assert(ciy_parse_profile_json_with_brightness(hex_31_ok, NULL, rgb, prof_name, sizeof(prof_name), &slots_cnt, &bright));
    assert(slots_cnt == 1 && rgb[15] == 0x11 && rgb[CIY_LED_COUNT + 15] == 0x22 && rgb[2 * CIY_LED_COUNT + 15] == 0x33);
    assert(!ciy_validate_profile_json(hex_32_fail, &vres));
    assert(!ciy_parse_profile_json_with_brightness(hex_32_fail, NULL, rgb, prof_name, sizeof(prof_name), &slots_cnt, &bright));

    // 4. out_name mit max_name_len = 0, 1, 2 (Canary-Prüfung)
    const char *short_json = "{\"name\":\"XYZ\",\"slots\":{\"0\":\"#FF0000\"}}";
    char name_canary[8];
    memset(name_canary, 0xAA, sizeof(name_canary));
    assert(ciy_parse_profile_json_with_brightness(short_json, NULL, rgb, name_canary, 0, &slots_cnt, &bright));
    assert((unsigned char)name_canary[0] == 0xAA); // max_name_len = 0 berührt Puffer nicht

    memset(name_canary, 0xAA, sizeof(name_canary));
    assert(ciy_parse_profile_json_with_brightness(short_json, NULL, rgb, name_canary, 1, &slots_cnt, &bright));
    assert(name_canary[0] == '\0' && (unsigned char)name_canary[1] == 0xAA);

    memset(name_canary, 0xAA, sizeof(name_canary));
    assert(ciy_parse_profile_json_with_brightness(short_json, NULL, rgb, name_canary, 2, &slots_cnt, &bright));
    assert(name_canary[0] == 'X' && name_canary[1] == '\0' && (unsigned char)name_canary[2] == 0xAA);

    // 5. step->length = 379 (Erfolg) vs. 380 (+1 Off-by-One Ablehnung)
    CIYDevice dev = {0};
    assert(ciy_open(&dev));
    CIYTransferPlan plan;
    assert(ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), 5, "StepEdge", 1));
    assert(plan.steps[0].length == 379);
    assert(ciy_execute_transfer_plan(&dev, &plan, NULL, NULL));
    plan.steps[0].length = 380; // Exakt +1 über CIY_REPORT_0B_SIZE (379)
    assert(!ciy_execute_transfer_plan(&dev, &plan, NULL, NULL));
    ciy_close(&dev);
}

static void test_cwe_1c_null_pointer_matrix(void) {
    assert(!ciy_open(NULL));
    ciy_close(NULL);
    CIYDevice closed_dev = {NULL};
    ciy_close(&closed_dev);

    CIYDevice open_dev = {0};
    assert(ciy_open(&open_dev));

    CIYLightingState st = {19, 0, 2, 5};
    assert(!ciy_get_state(NULL, &st));
    assert(!ciy_get_state(NULL, NULL));
    assert(!ciy_get_state(&closed_dev, &st));
    assert(ciy_get_state(&open_dev, NULL)); // Formatprüfung ohne Out-Pointer

    assert(!ciy_set_state(NULL, st));
    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
    assert(!ciy_send_planar_rgb(NULL, rgb, sizeof(rgb)));
    assert(!ciy_apply_custom_profile(NULL, rgb, sizeof(rgb)));
    assert(!ciy_apply_custom_profile(&open_dev, NULL, sizeof(rgb)));
    assert(!ciy_apply_custom_profile_with_brightness(NULL, rgb, sizeof(rgb), 5));
    assert(!ciy_apply_custom_profile_with_brightness_cancelable(NULL, rgb, sizeof(rgb), 5, NULL, NULL));

    CIYTransferPlan plan;
    assert(ciy_build_transfer_plan(&plan, rgb, sizeof(rgb), 5, "N", 1));
    assert(!ciy_execute_transfer_plan(NULL, &plan, NULL, NULL));
    assert(!ciy_execute_transfer_plan(&open_dev, NULL, NULL, NULL));
    assert(!ciy_execute_transfer_plan(&closed_dev, &plan, NULL, NULL));
    ciy_close(&open_dev);

    // Teil-NULLs bei ciy_parse_hex_color
    uint8_t r = 0, g = 0, b = 0;
    assert(!ciy_parse_hex_color("#FF00AA", NULL, &g, &b));
    assert(!ciy_parse_hex_color("#FF00AA", &r, NULL, &b));
    assert(!ciy_parse_hex_color("#FF00AA", &r, &g, NULL));
    assert(!ciy_parse_hex_color(NULL, NULL, NULL, NULL));

    // NULL-Guards für die 3 Void-Builder in src/core/ciy_hid.c:423-439
    ciy_build_all_red_profile(NULL);
    ciy_build_solid_color_profile(NULL, 0x11, 0x22, 0x33);
    ciy_build_wasd_and_arrows_profile(NULL, true);

    // ciy_validate_profile_json(bad, NULL) über sämtliche Fehlerzweige ohne out_res
    const char *bad_branches[] = {
        "",
        "   ",
        "[]",
        "123",
        "[123]",
        "{\"unterminated",
        "{\"name\" \"missing_colon\"}",
        "{\"name\": 123, \"slots\": {\"0\": \"#FF0000\"}}",
        "{\"name\": \"bad\\q\", \"slots\": {\"0\": \"#FF0000\"}}",
        "{\"brightness\": 9, \"slots\": {\"0\": \"#FF0000\"}}",
        "{\"slots\": 123}",
        "{\"slots\": {\"bad\\q\": \"#FF0000\"}}",
        "{\"slots\": {\"0\" \"#FF0000\"}}",
        "{\"slots\": {\"125\": \"#FF0000\"}}",
        "{\"slots\": {\"0\": 123}}",
        "{\"slots\": {\"0\": \"#FF\\q\"}}",
        "{\"slots\": {\"0\": \"#ZZZZZZ\"}}",
        "{\"slots\": {\"0\": \"#FF0000\", }}",
        "{\"slots\": {\"0\": \"#FF0000\"",
        "{\"125\": \"#FF0000\"}",
        "{\"0\": 123}",
        "{\"0\": \"#ZZZZZZ\"}",
        "{\"extra\": \"unterminated, \"slots\": {\"0\": \"#FF0000\"}}",
        "{\"slots\": {\"0\": \"#FF0000\"}, }",
        "[{\"slots\": {\"0\": \"#FF0000\"}}, ]",
        "[{\"slots\": {\"0\": \"#FF0000\"}}",
        "{\"slots\": {\"0\": \"#FF0000\"}} trailing"
    };
    assert(!ciy_validate_profile_json(NULL, NULL));
    for (size_t i = 0; i < sizeof(bad_branches) / sizeof(bad_branches[0]); i++) {
        assert(!ciy_validate_profile_json(bad_branches[i], NULL));
    }
    assert(ciy_validate_profile_json("{\"slots\": {\"0\": \"#FF0000\"}}", NULL));

    // Optionale NULL-Ausgabeparameter bei parse & load
    assert(!ciy_parse_profile_json_with_brightness(NULL, NULL, rgb, NULL, 0, NULL, NULL));
    assert(!ciy_parse_profile_json_with_brightness("{\"0\":\"#FF0000\"}", NULL, NULL, NULL, 0, NULL, NULL));
    assert(ciy_parse_profile_json_with_brightness("{\"0\":\"#FF0000\"}", NULL, rgb, NULL, 0, NULL, NULL));
    assert(!ciy_load_profile_file_with_brightness(NULL, NULL, rgb, NULL, 0, NULL, NULL));
    assert(!ciy_load_profile_file_with_brightness("tests/fixtures/valid_profile.json", NULL, NULL, NULL, 0, NULL, NULL));
    assert(ciy_load_profile_file_with_brightness("tests/fixtures/valid_profile.json", NULL, rgb, NULL, 0, NULL, NULL));
}

static void test_cwe_2a_deep_nesting_and_mixed_brackets(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    CIYValidationResult vres;

    // 1. Moderate Verschachtelung (Tiefe 16) -> zulässig
    const char *depth16 = "{\"name\":\"D16\",\"extra\":[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]],\"slots\":{\"0\":\"#FF0000\"}}";
    assert(ciy_validate_profile_json(depth16, &vres) && vres.is_valid);
    assert(ciy_parse_profile_json_with_brightness(depth16, NULL, rgb, NULL, 0, NULL, NULL));

    // 2. 10.000-fache Verschachtelung (Arrays & Objekte): kein Stack-Overflow, lineare Laufzeit (< 0.5s)
    const size_t nest_depth = 10000;
    const char *arr_head = "{\"name\":\"DeepArr\",\"extra\":";
    const char *tail = ",\"slots\":{\"0\":\"#FF0000\"}}";
    size_t arr_len = strlen(arr_head) + nest_depth + 1 + nest_depth + strlen(tail);
    char *deep_arr = (char *)malloc(arr_len + 1);
    assert(deep_arr != NULL);
    size_t pos = 0;
    memcpy(deep_arr + pos, arr_head, strlen(arr_head)); pos += strlen(arr_head);
    memset(deep_arr + pos, '[', nest_depth); pos += nest_depth;
    deep_arr[pos++] = '1';
    memset(deep_arr + pos, ']', nest_depth); pos += nest_depth;
    memcpy(deep_arr + pos, tail, strlen(tail)); pos += strlen(tail);
    deep_arr[pos] = '\0';

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool deep_arr_valid = ciy_validate_profile_json(deep_arr, &vres);
    bool deep_arr_parsed = ciy_parse_profile_json_with_brightness(deep_arr, NULL, rgb, NULL, 0, NULL, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_arr = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    assert(elapsed_arr < 0.5);
    assert(!deep_arr_valid && !deep_arr_parsed);

    // Ungeschlossene 10.000-fache Verschachtelung wird bereits heute sicher abgelehnt
    deep_arr[strlen(arr_head) + nest_depth] = '\0';
    assert(!ciy_validate_profile_json(deep_arr, &vres));
    assert(!ciy_parse_profile_json_with_brightness(deep_arr, NULL, rgb, NULL, 0, NULL, NULL));
    free(deep_arr);

    // 10.000-fach verschachteltes Objekt {"a":{"a":...1...}}
    size_t obj_len = strlen(arr_head) + nest_depth * 5 + 1 + nest_depth + strlen(tail);
    char *deep_obj = (char *)malloc(obj_len + 1);
    assert(deep_obj != NULL);
    pos = 0;
    memcpy(deep_obj + pos, arr_head, strlen(arr_head)); pos += strlen(arr_head);
    for (size_t i = 0; i < nest_depth; i++) {
        memcpy(deep_obj + pos, "{\"a\":", 5);
        pos += 5;
    }
    deep_obj[pos++] = '1';
    memset(deep_obj + pos, '}', nest_depth); pos += nest_depth;
    memcpy(deep_obj + pos, tail, strlen(tail)); pos += strlen(tail);
    deep_obj[pos] = '\0';

    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool deep_obj_valid = ciy_validate_profile_json(deep_obj, &vres);
    bool deep_obj_parsed = ciy_parse_profile_json_with_brightness(deep_obj, NULL, rgb, NULL, 0, NULL, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_obj = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    assert(elapsed_obj < 0.5);
    assert(!deep_obj_valid && !deep_obj_parsed);
    free(deep_obj);

    // 3. Gemischte / inkonsistente Klammern {[}] und [{]}
    const char *mixed1 = "{\"name\":\"Mixed1\",\"extra\":{[}],\"slots\":{\"0\":\"#FF0000\"}}";
    const char *mixed2 = "{\"name\":\"Mixed2\",\"extra\":[{]},\"slots\":{\"0\":\"#FF0000\"}}";
    bool m1_v = ciy_validate_profile_json(mixed1, &vres);
    bool m1_p = ciy_parse_profile_json_with_brightness(mixed1, NULL, rgb, NULL, 0, NULL, NULL);
    bool m2_v = ciy_validate_profile_json(mixed2, &vres);
    bool m2_p = ciy_parse_profile_json_with_brightness(mixed2, NULL, rgb, NULL, 0, NULL, NULL);
    assert(!m1_v && !m1_p && !m2_v && !m2_p);
}

static void test_cwe_2b_prefix_truncation_asan(void) {
    // Byte-für-Byte Präfix-Truncation (len = 0 .. N-1) in exakten malloc(len + 1) ASan-Heap-Puffern
    // Deckt ab: abgeschnittenes UTF-8-BOM ("\xEF", "\xEF\xBB"), Surrogates, Zahlen, Kolons, Strings, Objekte, Arrays
    const char *samples[] = {
        "\xEF\xBB\xBF{\"name\":\"Party \\uD83C\\uDF89\",\"brightness\":4,\"slots\":{\"0\":\"#FF0000\",\"14\":\"#00FF00\"}}",
        "[{\"name\":\"P1\",\"slots\":{\"1\":\"#123\"}},{\"name\":\"P2\",\"brightness\":5,\"14\":\"#ABCDEF\"}]"
    };

    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char out_name[64];
    int out_cnt = 0;
    uint8_t out_bright = 0;
    CIYValidationResult vres;

    for (size_t s = 0; s < sizeof(samples) / sizeof(samples[0]); s++) {
        size_t full_len = strlen(samples[s]);
        // Vollständiger Payload muss valide sein
        assert(ciy_validate_profile_json(samples[s], &vres));
        assert(ciy_parse_profile_json_with_brightness(samples[s], NULL, rgb, out_name, sizeof(out_name), &out_cnt, &out_bright));

        size_t parse_false_positives = 0;
        for (size_t len = 0; len < full_len; len++) {
            char *exact_prefix = (char *)malloc(len + 1);
            assert(exact_prefix != NULL);
            if (len > 0) {
                memcpy(exact_prefix, samples[s], len);
            }
            exact_prefix[len] = '\0';

            // ciy_validate_profile_json MUSS jedes unvollständige Präfix ablehnen!
            assert(!ciy_validate_profile_json(exact_prefix, &vres));

            bool parsed_ok = ciy_parse_profile_json_with_brightness(
                exact_prefix, NULL, rgb, out_name, sizeof(out_name), &out_cnt, &out_bright);
            if (s == 0 && len <= 3) {
                // Leer sowie abgeschnittenes BOM ("\xEF", "\xEF\xBB", "\xEF\xBB\xBF") müssen false liefern
                assert(!parsed_ok);
            }
            if (parsed_ok) {
                parse_false_positives++;
            }

            free(exact_prefix);
        }
        assert(parse_false_positives == 0);
    }
}

static void test_cwe_2c_control_chars_utf8_and_nul_injection(void) {
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    CIYValidationResult vres;

    // 1. \u0000-Injektion in "name", "slots" (Key & Value), Flat-Key und Unknown-Feldern -> MUSS abgelehnt werden
    const char *escaped_nul_cases[] = {
        "{\"name\":\"Esc\\u0000Nul\",\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"OK\",\"slots\":{\"0\\u0000\":\"#FF0000\"}}",
        "{\"name\":\"OK\",\"slots\":{\"\\u0000\":\"#FF0000\"}}",
        "{\"name\":\"OK\",\"slots\":{\"0\":\"#FF0000\\u0000\"}}",
        "{\"0\\u0000\":\"#FF0000\"}",
        "{\"name\":\"OK\",\"extra\":\"abc\\u0000def\",\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"OK\",\"extra\":[\"\\u0000\"],\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"OK\",\"extra\":{\"k\":\"\\u0000\"},\"slots\":{\"0\":\"#FF0000\"}}"
    };
    for (size_t i = 0; i < sizeof(escaped_nul_cases) / sizeof(escaped_nul_cases[0]); i++) {
        assert(!ciy_validate_profile_json(escaped_nul_cases[i], &vres));
        assert(!ciy_parse_profile_json_with_brightness(escaped_nul_cases[i], NULL, rgb, NULL, 0, NULL, NULL));
    }

    // 2. In-Memory-NUL mitten im Token (exakter ASan-Puffer)
    const char raw_nul_mem[] = "{\"name\":\"Abc\0Def\",\"slots\":{\"0\":\"#FF0000\"}}";
    char *heap_nul = (char *)malloc(sizeof(raw_nul_mem));
    assert(heap_nul != NULL);
    memcpy(heap_nul, raw_nul_mem, sizeof(raw_nul_mem));
    assert(!ciy_validate_profile_json(heap_nul, &vres));
    assert(!ciy_parse_profile_json_with_brightness(heap_nul, NULL, rgb, NULL, 0, NULL, NULL));
    free(heap_nul);

    // 3. Rohe unescapete Control-Characters 0x01..0x1F in JSON-Strings (RFC 8259 §7)
    size_t ctrl_accepted = 0;
    for (unsigned int c = 0x01; c <= 0x1F; c++) {
        char ctrl_json[128];
        int n = snprintf(ctrl_json, sizeof(ctrl_json),
                         "{\"name\":\"Ctrl%cName\",\"slots\":{\"0\":\"#FF0000\"}}", (char)c);
        assert(n > 0 && (size_t)n < sizeof(ctrl_json));
        char *exact = (char *)malloc((size_t)n + 1);
        assert(exact != NULL);
        memcpy(exact, ctrl_json, (size_t)n + 1);
        bool v = ciy_validate_profile_json(exact, &vres);
        bool p = ciy_parse_profile_json_with_brightness(exact, NULL, rgb, NULL, 0, NULL, NULL);
        if (v || p) ctrl_accepted++;
        free(exact);
    }
    assert(ctrl_accepted == 0);

    // 4. Ungültiges Roh-UTF-8: Overlong-NUL \xC0\x80, Overlongs, CESU-8 Surrogates, Continuation, \xFE/\xFF, >U+10FFFF, Truncated
    const char *bad_utf8_seqs[] = {
        "\xC0\x80",             // Overlong NUL
        "\xC0\xAF",             // Overlong '/'
        "\xE0\x80\x80",         // 3-Byte Overlong NUL
        "\xF0\x80\x80\x80",     // 4-Byte Overlong NUL
        "\xED\xA0\x80",         // CESU-8 High Surrogate U+D800
        "\xED\xBF\xBF",         // CESU-8 Low Surrogate U+DFFF
        "\x80",                 // Isoliertes Continuation-Byte
        "\xFE",                 // Ungültiges UTF-8 Startbyte 0xFE
        "\xFF",                 // Ungültiges UTF-8 Startbyte 0xFF
        "\xF4\x90\x80\x80",     // Codepoint U+110000 (> U+10FFFF)
        "\xC3",                 // Abgeschnittene 2-Byte-Sequenz vor '"'
        "\xE2\x82",             // Abgeschnittene 3-Byte-Sequenz vor '"'
        "\xF0\x9F\x8E"          // Abgeschnittene 4-Byte-Sequenz vor '"'
    };
    size_t bad_utf8_accepted = 0;
    for (size_t i = 0; i < sizeof(bad_utf8_seqs) / sizeof(bad_utf8_seqs[0]); i++) {
        char buf[160];
        int n = snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"slots\":{\"0\":\"#FF0000\"}}", bad_utf8_seqs[i]);
        assert(n > 0 && (size_t)n < sizeof(buf));
        char *exact = (char *)malloc((size_t)n + 1);
        assert(exact != NULL);
        memcpy(exact, buf, (size_t)n + 1);
        bool v = ciy_validate_profile_json(exact, &vres);
        bool p = ciy_parse_profile_json_with_brightness(exact, NULL, rgb, NULL, 0, NULL, NULL);
        if (v || p) bad_utf8_accepted++;
        free(exact);
    }
    assert(bad_utf8_accepted == 0);

    // 5. Ungültige Primitivwerte in Zusatzfeldern (RFC 8259)
    const char *bad_primitives[] = {
        "{\"name\":\"P\",\"extra\":nonsense,\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"P\",\"extra\":NaN,\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"P\",\"extra\":Infinity,\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"P\",\"extra\":tru,\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"P\",\"extra\":fals,\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"P\",\"extra\":nul,\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"P\",\"extra\":01,\"slots\":{\"0\":\"#FF0000\"}}",
        "{\"name\":\"P\",\"extra\":--1,\"slots\":{\"0\":\"#FF0000\"}}"
    };
    size_t bad_prim_accepted = 0;
    for (size_t i = 0; i < sizeof(bad_primitives) / sizeof(bad_primitives[0]); i++) {
        bool v = ciy_validate_profile_json(bad_primitives[i], &vres);
        bool p = ciy_parse_profile_json_with_brightness(bad_primitives[i], NULL, rgb, NULL, 0, NULL, NULL);
        if (v || p) bad_prim_accepted++;
    }
    assert(bad_prim_accepted == 0);
}

typedef struct {
    const char *path;
    atomic_bool stop;
} ConcurrentWriterCtx;

static void *concurrent_file_writer_thread(void *arg) {
    ConcurrentWriterCtx *ctx = (ConcurrentWriterCtx *)arg;
    const char *small_json = "{\"name\":\"RaceSmall\",\"slots\":{\"0\":\"#FF0000\"}}";
    char large_json[2048];
    memset(large_json, ' ', sizeof(large_json));
    memcpy(large_json, "{\"name\":\"RaceLarge\",\"slots\":{\"14\":\"#00FF00\"}}", 45);
    large_json[sizeof(large_json) - 1] = '\0';

    int iter = 0;
    while (!atomic_load_explicit(&ctx->stop, memory_order_relaxed)) {
        int fd = open(ctx->path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            const char *payload = (iter % 2 == 0) ? small_json : large_json;
            size_t len = (iter % 2 == 0) ? strlen(small_json) : (sizeof(large_json) - 1);
            (void)write(fd, payload, len);
            close(fd);
        }
        iter++;
    }
    return NULL;
}

static void test_cwe_3a_toctou_atexit_and_concurrent_growth(void) {
    // 1. Subprozess-Test: Abgelehntes FIFO-Lockfile darf bei Prozessende NICHT durch atexit gelöscht werden
    int pipefd[2];
    assert(pipe(pipefd) == 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(pipefd[0]);
        char child_lock_path[PATH_MAX];
        assert(ciy_bus_lock_path(child_lock_path, sizeof(child_lock_path)) != NULL);
        unlink(child_lock_path);
        assert(mkfifo(child_lock_path, 0600) == 0);

        size_t plen = strlen(child_lock_path) + 1;
        assert(write(pipefd[1], child_lock_path, plen) == (ssize_t)plen);
        close(pipefd[1]);

        // FIFO muss von ciy_bus_lock abgewiesen werden
        assert(!ciy_bus_lock());
        exit(0); // Triggert atexit(ciy_cleanup_test_lockfile)
    }
    close(pipefd[1]);
    char child_fifo[PATH_MAX] = {0};
    ssize_t rd = read(pipefd[0], child_fifo, sizeof(child_fifo) - 1);
    close(pipefd[0]);
    assert(rd > 0);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    struct stat fifo_st;
    bool fifo_survived = (lstat(child_fifo, &fifo_st) == 0 && S_ISFIFO(fifo_st.st_mode));
    if (fifo_survived) {
        unlink(child_fifo);
    }
    assert(fifo_survived);

    // 2. Konkurrierendes Dateiwachstum / Schrumpfen nach fstat() unter ASan
    char race_path[256];
    snprintf(race_path, sizeof(race_path), "/tmp/ciy_toctou_race_%d.json", (int)getpid());
    unlink(race_path);
    {
        FILE *init_f = fopen(race_path, "wb");
        assert(init_f != NULL);
        fputs("{\"name\":\"Init\",\"slots\":{\"0\":\"#FF0000\"}}", init_f);
        fclose(init_f);
    }

    ConcurrentWriterCtx wctx = {.path = race_path, .stop = false};
    pthread_t writer_tid;
    assert(pthread_create(&writer_tid, NULL, concurrent_file_writer_thread, &wctx) == 0);

    for (int i = 0; i < 60; i++) {
        size_t sz = 0;
        CIYValidationResult res;
        char *buf = ciy_read_text_file_with_result(race_path, &sz, &res);
        if (buf != NULL) {
            assert(buf[sz] == '\0');
            assert(memchr(buf, '\0', sz) == NULL);
            free(buf);
        }
    }
    atomic_store_explicit(&wctx.stop, true, memory_order_relaxed);
    pthread_join(writer_tid, NULL);
    unlink(race_path);

    // 3. TMPDIR-Härtung (nicht-existierendes Verzeichnis, reguläre Datei statt Verzeichnis, fremdes /tmp)
    const char *orig_testing = getenv("CIY_TESTING");
    char saved_testing[64] = {0};
    if (orig_testing) strncpy(saved_testing, orig_testing, sizeof(saved_testing) - 1);
    const char *orig_tmp = getenv("TMPDIR");
    char saved_tmp[PATH_MAX] = {0};
    if (orig_tmp) strncpy(saved_tmp, orig_tmp, sizeof(saved_tmp) - 1);

    unsetenv("CIY_TESTING");
    char lock_buf[PATH_MAX];
    char fake_tmp_file[256];
    snprintf(fake_tmp_file, sizeof(fake_tmp_file), "/tmp/ciy_not_a_dir_%d", (int)getpid());
    FILE *ft = fopen(fake_tmp_file, "w");
    assert(ft != NULL);
    fclose(ft);

    setenv("TMPDIR", fake_tmp_file, 1);
    const char *resolved = ciy_bus_lock_path(lock_buf, sizeof(lock_buf));
    assert(resolved != NULL && strncmp(resolved, fake_tmp_file, strlen(fake_tmp_file)) != 0);
    unlink(fake_tmp_file);

    setenv("TMPDIR", "/tmp/ciy_nonexistent_tmpdir_xyz_999", 1);
    resolved = ciy_bus_lock_path(lock_buf, sizeof(lock_buf));
    assert(resolved != NULL);

    if (orig_tmp) setenv("TMPDIR", saved_tmp, 1); else unsetenv("TMPDIR");
    if (orig_testing) setenv("CIY_TESTING", saved_testing, 1);
}

static void test_cwe_3b_poisoned_outputs_on_error(void) {
    // 1. ciy_validate_profile_json & ciy_validate_profile_file mit 0xAA-Poisoning (aktiv grün)
    CIYValidationResult vres;
    memset(&vres, 0xAA, sizeof(vres));
    assert(!ciy_validate_profile_json("{\"slots\":{}}", &vres));
    assert(vres.is_valid == false);
    assert(vres.profile_count == 0);
    assert(vres.total_keys_count == 0);
    assert(vres.read_error == CIYFileReadErrorNone);
    assert(strcmp(vres.error_field, "slots") == 0);
    assert(vres.error_message[0] != '\0');

    memset(&vres, 0xAA, sizeof(vres));
    assert(!ciy_validate_profile_file("tests/fixtures/invalid_color.json", &vres));
    assert(vres.is_valid == false);
    assert(vres.profile_count == 0);
    assert(vres.total_keys_count == 0);
    assert(vres.error_field[0] != '\0' && vres.error_message[0] != '\0');

    // 2. ciy_parse_profile_json_with_brightness mit 0xAA-Poisoning bei ungültigem JSON (aktiv grün)
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name[64];
    int slots_count;
    uint8_t brightness;
    memset(rgb, 0xAA, sizeof(rgb));
    memset(name, 0xAA, sizeof(name));
    memset(&slots_count, 0xAA, sizeof(slots_count));
    memset(&brightness, 0xAA, sizeof(brightness));
    assert(!ciy_parse_profile_json_with_brightness("broken_json", NULL, rgb, name, sizeof(name), &slots_count, &brightness));
    for (size_t i = 0; i < sizeof(rgb); i++) assert(rgb[i] == 0);
    assert(name[0] == '\0');
    assert(slots_count == 0);
    assert(brightness == CIY_MAX_BRIGHTNESS);

    // 3. ciy_load_profile_file_with_brightness, ciy_get_state, ciy_parse_hex_color bei Fehler
    memset(rgb, 0xAA, sizeof(rgb));
    memset(name, 0xAA, sizeof(name));
    memset(&slots_count, 0xAA, sizeof(slots_count));
    memset(&brightness, 0xAA, sizeof(brightness));
    assert(!ciy_load_profile_file_with_brightness("/tmp/ciy_missing_poison_test.json", NULL,
                                                  rgb, name, sizeof(name), &slots_count, &brightness));
    assert(rgb[0] == 0 && name[0] == '\0' && slots_count == 0 && brightness == CIY_MAX_BRIGHTNESS);

    CIYDevice dev = {0};
    assert(ciy_open(&dev));
    CIYLightingState st;
    memset(&st, 0xAA, sizeof(st));
    setenv("CIY_TESTING_FAIL_GET_REPORTS", "1", 1);
    assert(!ciy_get_state(&dev, &st));
    unsetenv("CIY_TESTING_FAIL_GET_REPORTS");
    ciy_close(&dev);
    assert(st.mode == 0 && st.color == 0 && st.speed == 0 && st.brightness == 0);

    uint8_t r = 0xAA, g = 0xAA, b = 0xAA;
    assert(!ciy_parse_hex_color("#ZZZZZZ", &r, &g, &b));
    assert(r == 0 && g == 0 && b == 0);
}

static void test_fuzz_corpus_files(void) {
    struct CorpusCase {
        const char *path;
        int mode; // 1 = valid, 0 = invalid
    };
    static const struct CorpusCase corpus[] = {
        {"tests/fuzz_corpus/01_valid_minimal.json", 1},
        {"tests/fuzz_corpus/02_valid_bom_unicode_surrogate.json", 1},
        {"tests/fuzz_corpus/03_valid_multi_flat_and_nested.json", 1},
        {"tests/fuzz_corpus/04_valid_boundary_lengths.json", 1},
        {"tests/fuzz_corpus/05_invalid_deep_nesting.json", 0},
        {"tests/fuzz_corpus/06_invalid_mixed_brackets.json", 0},
        {"tests/fuzz_corpus/07_invalid_truncated_mid_object.json", 0},
        {"tests/fuzz_corpus/08_invalid_truncated_unicode_escape.json", 0},
        {"tests/fuzz_corpus/09_invalid_raw_control_chars.json", 0},
        {"tests/fuzz_corpus/10_invalid_overlong_utf8_and_cesu8.json", 0},
        {"tests/fuzz_corpus/11_invalid_escaped_nul.json", 0},
        {"tests/fuzz_corpus/12_invalid_embedded_raw_nul.json", 0},
        {"tests/fuzz_corpus/13_invalid_unknown_primitive.json", 0},
        {"tests/fuzz_corpus/14_invalid_integer_overflow_slots_brightness.json", 0},
        {"tests/fuzz_corpus/15_invalid_string_buffer_off_by_one.json", 0}
    };

    for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); i++) {
        // Prüfe zunächst, dass jede Corpus-Datei regulär auf Platte existiert
        int fd = open(corpus[i].path, O_RDONLY | O_NOFOLLOW);
        assert(fd >= 0);
        struct stat st;
        assert(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0);
        close(fd);

        CIYValidationResult vres;
        uint8_t rgb[CIY_PLANAR_RGB_SIZE];
        char name[64];
        int count = 0;
        uint8_t bright = 0;

        bool file_valid = ciy_validate_profile_file(corpus[i].path, &vres);
        bool file_loaded = ciy_load_profile_file_with_brightness(
            corpus[i].path, NULL, rgb, name, sizeof(name), &count, &bright);

        if (corpus[i].mode == 1) {
            assert(file_valid && vres.is_valid);
            assert(file_loaded && count > 0);
        } else {
            assert(!file_valid && !vres.is_valid);
            assert(!file_loaded);
            char *raw = ciy_read_text_file(corpus[i].path, NULL);
            if (raw) {
                assert(!ciy_parse_profile_json_with_brightness(
                    raw, NULL, rgb, name, sizeof(name), &count, &bright));
                free(raw);
            }
        }
    }
}

int main(void) {
    if (!getenv("CIY_TESTING")) {
        setenv("CIY_TESTING", "1", 1);
    }
    test_layout_c_model();
    test_profile_validation();
    test_trailing_comma_rejection();
    test_read_text_file_validation_result_init();
    test_transfer_plan();
    test_transfer_plan_delay_eintr();
    test_nanosleep_uninterrupted_eintr();
    test_build_profile_wasd();
    test_build_profile_yellow_no_base();
    test_build_profile_ignores_out_of_range();
    test_build_profile_null_checks();
    test_build_all_red();
    test_build_wasd_and_arrows();
    test_validation_without_device();
    test_hex_parsing();
    test_solid_color_profile();
    test_parse_profile_json();
    test_json_escape_decoding();
    test_duplicate_slots_count_once();
    test_unicode_surrogate_rejection();
    test_truncated_unicode_escapes();
    test_json_string_length_limits();
    test_json_string_bounds_and_unicode_escapes();
    test_corrupt_strings_in_unknown_fields();
    test_cancelable_apply();
    test_load_profile_file();
    test_load_profile_file_schema_validation();
    test_parse_profile_json_brightness();
    test_fifo_profile_rejection();
    test_bus_lock_reentrancy();
    test_bus_lock_fork_safety();
    test_test_lockfile_atexit_cleanup();
    test_lockfile_security();
    test_bus_lock_path_isolated_per_user();
    test_lockfile_rejects_fifo();
    test_lockfile_rejects_symlink();
    test_cwe_1a_integer_overflow_and_delay_wraparound();
    test_cwe_1b_off_by_one_boundaries();
    test_cwe_1c_null_pointer_matrix();
    test_cwe_2a_deep_nesting_and_mixed_brackets();
    test_cwe_2b_prefix_truncation_asan();
    test_cwe_2c_control_chars_utf8_and_nul_injection();
    test_cwe_3a_toctou_atexit_and_concurrent_growth();
    test_cwe_3b_poisoned_outputs_on_error();
    test_fuzz_corpus_files();
    puts("Alle Tests bestanden.");
    return 0;
}
