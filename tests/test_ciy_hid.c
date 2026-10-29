// Hardware-freie Tests für ciy_hid (Pufferaufbau, Validierung).
// Ausführen: ./scripts/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/core/ciy_hid.h"
#include "../src/layouts/ciy_layout.h"

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
    char user_path[128];
    ciy_bus_lock_path(user_path, sizeof(user_path));

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
    assert(access(user_path, F_OK) != 0);

    // Nicht-reguläre Datei als Lockfile abweisen
    assert(mkfifo(user_path, 0600) == 0);
    assert(!ciy_bus_lock());
    assert(unlink(user_path) == 0);
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
    // Erwartet: Trunkierung auf 63 Zeichen, Profil wird geladen
    char long_name[70];
    memset(long_name, 'A', 64);
    long_name[64] = '\0';
    char json_buf[512];
    snprintf(json_buf, sizeof(json_buf), "{\"name\": \"%s\", \"slots\": {\"0\": \"#FF0000\"}}", long_name);
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    // Name sollte auf 63 Zeichen trunkiert sein
    assert(strlen(name) == 63);
    assert(strncmp(name, long_name, 63) == 0);

    // 2. Profilname mit exakt 63 Zeichen (Grenzfall: passt inklusive '\0' genau in 64 Byte)
    memset(long_name, 'B', 63);
    long_name[63] = '\0';
    snprintf(json_buf, sizeof(json_buf), "{\"name\": \"%s\", \"slots\": {\"0\": \"#FF0000\"}}", long_name);
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);
    assert(strcmp(name, long_name) == 0);

    // 3. JSON-Key überschreitet 64-Byte-Puffer (64 Zeichen Key) -> trunkiert
    char long_key[70];
    memset(long_key, 'k', 64);
    long_key[64] = '\0';
    snprintf(json_buf, sizeof(json_buf), "{\"%s\": 123, \"name\": \"OK\", \"slots\": {\"0\": \"#FF0000\"}}", long_key);
    memset(name, 0, sizeof(name));
    count = 0;
    assert(ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));
    assert(count == 1);

    // 4. Slot-Key überschreitet 32-Byte-Puffer (32 Zeichen) -> trunkiert, aber ungültige Slot-Nummer
    char long_slot[40];
    memset(long_slot, '9', 32);
    long_slot[32] = '\0';
    snprintf(json_buf, sizeof(json_buf), "{\"name\": \"OK\", \"slots\": {\"%s\": \"#FF0000\"}}", long_slot);
    memset(name, 0, sizeof(name));
    count = 0;
    // Erwartet: Parse schlägt fehl, da trunkierter Key "999..." keine gültige Slot-Nummer ist
    assert(!ciy_parse_profile_json(json_buf, NULL, rgb, name, sizeof(name), &count));

    // 5. String in unbekanntem Feld überschreitet CIY_JSON_MAX_STRLEN (4096) -> DoS-Schutz (trunkiert)
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
    assert(!ciy_build_transfer_plan(NULL, rgb, sizeof(rgb), 3, "X", 1));
    assert(!ciy_build_transfer_plan(&plan, NULL, sizeof(rgb), 3, "X", 1));
    assert(!ciy_build_transfer_plan(&plan, rgb, sizeof(rgb) - 1, 3, "X", 1));

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
}

int main(void) {
    test_layout_c_model();
    test_profile_validation();
    test_transfer_plan();
    test_build_profile_wasd();
    test_build_profile_yellow_no_base();
    test_build_profile_ignores_out_of_range();
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
    test_corrupt_strings_in_unknown_fields();
    test_cancelable_apply();
    test_load_profile_file();
    test_fifo_profile_rejection();
    test_bus_lock_reentrancy();
    test_lockfile_security();
    puts("Alle Tests bestanden.");
    return 0;
}
