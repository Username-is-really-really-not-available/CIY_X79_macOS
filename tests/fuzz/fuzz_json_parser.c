// Isoliertes LLVM LibFuzzer-Harness für den Zero-Allocation JSON-Profilparser.
// Bindet ausschließlich den reinen ISO-C11 Parser-Kern (ciy_json.h) ein —
// kein IOKit, kein CoreFoundation, kein HIDAPI, keine POSIX- oder Drittbibliotheken.

#include "ciy_json.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CIY_FUZZ_MAX_INPUT_SIZE (512U * 1024U)

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

static void ciy_fuzz_assert(bool cond, char *owned_buf) {
    if (!cond) {
        free(owned_buf);
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size > CIY_FUZZ_MAX_INPUT_SIZE) {
        return 0;
    }

    // Exakter Heap-Puffer (Size + 1) für bytegenaue AddressSanitizer-Redzones am Pufferende
    char *buf = (char *)malloc(Size + 1U);
    if (!buf) {
        return 0;
    }
    if (Size > 0U && Data != NULL) {
        memcpy(buf, Data, Size);
    } else {
        Size = 0U;
    }
    buf[Size] = '\0';

    // 1. Schema- & Grenzvalidierung (mit 0xAA-Poisoning gegen uninitialisierte Ausgaben)
    CIYValidationResult vres;
    memset(&vres, 0xAA, sizeof(vres));
    bool valid = ciy_validate_profile_json(buf, &vres);
    bool valid_no_out = ciy_validate_profile_json(buf, NULL);
    ciy_fuzz_assert(valid == valid_no_out, buf);

    if (valid) {
        ciy_fuzz_assert(vres.is_valid, buf);
        ciy_fuzz_assert(vres.profile_count >= 1, buf);
        ciy_fuzz_assert(vres.total_keys_count >= vres.profile_count, buf);
    } else {
        ciy_fuzz_assert(!vres.is_valid, buf);
    }

    // 2. Primärer Parse-Pfad (erstes Profil, vollständiger Namenspuffer & Helligkeit)
    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    char name_buf[64];
    int slots_count = -1;
    uint8_t brightness = 0xFF;
    memset(rgb, 0xAA, sizeof(rgb));
    memset(name_buf, 0xAA, sizeof(name_buf));

    bool parsed = ciy_parse_profile_json_with_brightness(
        buf, NULL, rgb, name_buf, sizeof(name_buf), &slots_count, &brightness);

    // Jedes vom Validator akzeptierte JSON muss vom Parser fehlerfrei geladen werden
    if (valid) {
        ciy_fuzz_assert(parsed, buf);
    }

    if (parsed) {
        ciy_fuzz_assert(slots_count >= 1 && slots_count <= (CIY_LED_COUNT - 1), buf);
        ciy_fuzz_assert(brightness <= CIY_MAX_BRIGHTNESS, buf);
        ciy_fuzz_assert(memchr(name_buf, '\0', sizeof(name_buf)) != NULL, buf);
        // Hardware +1 Offset-Schutz: LED-Index 0 darf durch Slots 0..124 niemals beschrieben werden
        ciy_fuzz_assert(rgb[0] == 0U, buf);
        ciy_fuzz_assert(rgb[CIY_LED_COUNT] == 0U, buf);
        ciy_fuzz_assert(rgb[2 * CIY_LED_COUNT] == 0U, buf);
    } else {
        ciy_fuzz_assert(name_buf[0] == '\0', buf);
        ciy_fuzz_assert(slots_count == 0, buf);
        ciy_fuzz_assert(brightness == CIY_MAX_BRIGHTNESS, buf);
        ciy_fuzz_assert(rgb[0] == 0U, buf);
    }

    // 3. Sekundäre Pfade: Multi-Profil-Index ("2"), kleiner Namenspuffer & Namenssuche
    uint8_t rgb2[CIY_PLANAR_RGB_SIZE];
    char tiny_name[4];
    int slots2 = -1;
    uint8_t bright2 = 0xFF;
    memset(rgb2, 0xAA, sizeof(rgb2));
    memset(tiny_name, 0xAA, sizeof(tiny_name));

    bool parsed2 = ciy_parse_profile_json_with_brightness(
        buf, "2", rgb2, tiny_name, sizeof(tiny_name), &slots2, &bright2);
    if (parsed2) {
        ciy_fuzz_assert(slots2 >= 1 && slots2 <= (CIY_LED_COUNT - 1), buf);
        ciy_fuzz_assert(bright2 <= CIY_MAX_BRIGHTNESS, buf);
        ciy_fuzz_assert(memchr(tiny_name, '\0', sizeof(tiny_name)) != NULL, buf);
        ciy_fuzz_assert(rgb2[0] == 0U && rgb2[CIY_LED_COUNT] == 0U && rgb2[2 * CIY_LED_COUNT] == 0U, buf);
    }

    if (name_buf[0] != '\0') {
        bool parsed_by_name = ciy_parse_profile_json_with_brightness(
            buf, name_buf, rgb2, NULL, 0U, NULL, NULL);
        if (parsed_by_name) {
            ciy_fuzz_assert(rgb2[0] == 0U && rgb2[CIY_LED_COUNT] == 0U && rgb2[2 * CIY_LED_COUNT] == 0U, buf);
        }
    }

    free(buf);
    return 0;
}

#ifdef CIY_FUZZ_STANDALONE
#include <stdio.h>

int main(int argc, char **argv) {
    // Leereingaben & Randfälle vorab testen
    (void)LLVMFuzzerTestOneInput(NULL, 0U);
    const uint8_t empty_byte = 0U;
    (void)LLVMFuzzerTestOneInput(&empty_byte, 0U);
    (void)LLVMFuzzerTestOneInput(&empty_byte, 1U);

    if (argc < 2) {
        fprintf(stderr, "Verwendung: %s <seed-datei> [weitere-dateien...]\n", argv[0]);
        return 1;
    }

    size_t files_count = 0U;
    size_t runs_count = 3U;

    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            fprintf(stderr, "FEHLER: Konnte Seed-Datei nicht öffnen: %s\n", argv[i]);
            return 1;
        }

        size_t cap = 4096U;
        size_t len = 0U;
        uint8_t *data = (uint8_t *)malloc(cap);
        if (!data) {
            fclose(f);
            return 1;
        }

        for (;;) {
            if (len == cap) {
                if (cap > CIY_FUZZ_MAX_INPUT_SIZE * 2U) {
                    break;
                }
                size_t next_cap = cap * 2U;
                uint8_t *grown = (uint8_t *)realloc(data, next_cap);
                if (!grown) {
                    free(data);
                    fclose(f);
                    return 1;
                }
                data = grown;
                cap = next_cap;
            }
            size_t n = fread(data + len, 1U, cap - len, f);
            len += n;
            if (n == 0U) {
                break;
            }
        }

        bool io_err = (ferror(f) != 0);
        fclose(f);
        if (io_err) {
            free(data);
            fprintf(stderr, "FEHLER: Lesefehler bei Seed-Datei: %s\n", argv[i]);
            return 1;
        }

        // Vollständiges Seed sowie alle Byte-Präfixe 0..len durch das Harness schicken
        for (size_t prefix = 0U; prefix <= len; prefix++) {
            (void)LLVMFuzzerTestOneInput(data, prefix);
            runs_count++;
        }

        free(data);
        files_count++;
    }

    printf("[Standalone-Fuzzer] %zu Seed-Dateien (%zu Präfix-/Rand-Mutationen) fehlerfrei unter ASan/UBSan geprüft.\n",
           files_count, runs_count);
    return 0;
}
#endif
