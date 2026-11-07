#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../src/core/ciy_brightness.h"

// ---------------------------------------------------------------------------
// 1. Tests für ciy_brightness_scale_channel
// ---------------------------------------------------------------------------
static void test_scale_channel(void) {
    // Randwerte 0% und 100%
    for (int ch = 0; ch <= 255; ch++) {
        assert(ciy_brightness_scale_channel((uint8_t)ch, 0) == 0);
        assert(ciy_brightness_scale_channel((uint8_t)ch, 100) == (uint8_t)ch);
    }

    // 50% Rundung (kaufmännisch aufrunden ab .5)
    // 255 * 50% = 127.5 -> 128
    assert(ciy_brightness_scale_channel(255, 50) == 128);
    // 1 * 50% = 0.5 -> 1
    assert(ciy_brightness_scale_channel(1, 50) == 1);
    // 1 * 49% = 0.49 -> 0
    assert(ciy_brightness_scale_channel(1, 49) == 0);
    // 3 * 50% = 1.5 -> 2
    assert(ciy_brightness_scale_channel(3, 50) == 2);
    // 2 * 50% = 1.0 -> 1
    assert(ciy_brightness_scale_channel(2, 50) == 1);
    // 100 * 50% = 50.0 -> 50
    assert(ciy_brightness_scale_channel(100, 50) == 50);

    // Clamping: Werte > 100% werden auf 100% gedeckelt
    assert(ciy_brightness_scale_channel(200, 101) == 200);
    assert(ciy_brightness_scale_channel(200, 150) == 200);
    assert(ciy_brightness_scale_channel(200, 255) == 200);

    // Monotonie und Exhaustive Prüfung für alle 256 Kanäle und 0..100%
    for (int ch = 0; ch <= 255; ch++) {
        uint8_t prev = 0;
        for (int pct = 0; pct <= 100; pct++) {
            uint8_t val = ciy_brightness_scale_channel((uint8_t)ch, (uint8_t)pct);
            uint32_t expected = ((uint32_t)ch * (uint32_t)pct + 50u) / 100u;
            assert(val == (uint8_t)expected);
            assert(val >= prev); // Monoton steigend mit Prozent
            prev = val;
        }
    }

    printf("[Test 1] ciy_brightness_scale_channel (Rundung, Clamping, Monotonie)... PASSED\n");
}

// ---------------------------------------------------------------------------
// 2. Tests für ciy_brightness_apply_planar
// ---------------------------------------------------------------------------
static void test_apply_planar(void) {
    uint8_t buffer[CIY_PLANAR_RGB_SIZE];
    uint8_t mask[CIY_LED_COUNT];

    // NULL-Guards
    assert(!ciy_brightness_apply_planar(NULL, mask));
    assert(!ciy_brightness_apply_planar(buffer, NULL));
    assert(!ciy_brightness_apply_planar(NULL, NULL));

    // Canary-Schutz: Puffer mit Pufferzone davor und danach
    struct {
        uint8_t canary_front[16];
        uint8_t planar[CIY_PLANAR_RGB_SIZE];
        uint8_t canary_back[16];
    } guard_buf;

    memset(guard_buf.canary_front, 0xAA, sizeof(guard_buf.canary_front));
    memset(guard_buf.canary_back, 0x55, sizeof(guard_buf.canary_back));

    // Initialisieren mit Testmuster (R = 200, G = 150, B = 100)
    for (size_t i = 0; i < CIY_LED_COUNT; i++) {
        guard_buf.planar[i] = 200;
        guard_buf.planar[CIY_LED_COUNT + i] = 150;
        guard_buf.planar[(2 * CIY_LED_COUNT) + i] = 100;
        mask[i] = 100; // Alles 100%
    }

    // Identität bei 100%
    assert(ciy_brightness_apply_planar(guard_buf.planar, mask));
    for (size_t i = 0; i < CIY_LED_COUNT; i++) {
        assert(guard_buf.planar[i] == 200);
        assert(guard_buf.planar[CIY_LED_COUNT + i] == 150);
        assert(guard_buf.planar[(2 * CIY_LED_COUNT) + i] == 100);
    }

    // Gezielt LED 15 (W-Taste) auf 50% skalieren
    mask[15] = 50;
    assert(ciy_brightness_apply_planar(guard_buf.planar, mask));

    // LED 15 muss skaliert sein:
    // 200 * 50% = 100, 150 * 50% = 75, 100 * 50% = 50
    assert(guard_buf.planar[15] == 100);
    assert(guard_buf.planar[CIY_LED_COUNT + 15] == 75);
    assert(guard_buf.planar[(2 * CIY_LED_COUNT) + 15] == 50);

    // Alle anderen LEDs müssen unverändert geblieben sein
    for (size_t i = 0; i < CIY_LED_COUNT; i++) {
        if (i == 15) continue;
        assert(guard_buf.planar[i] == 200);
        assert(guard_buf.planar[CIY_LED_COUNT + i] == 150);
        assert(guard_buf.planar[(2 * CIY_LED_COUNT) + i] == 100);
    }

    // Rand-LEDs: LED 0 und LED 125
    mask[0] = 0;    // 0% -> komplett dunkel
    mask[125] = 10; // 10%
    assert(ciy_brightness_apply_planar(guard_buf.planar, mask));
    assert(guard_buf.planar[0] == 0);
    assert(guard_buf.planar[CIY_LED_COUNT + 0] == 0);
    assert(guard_buf.planar[(2 * CIY_LED_COUNT) + 0] == 0);

    assert(guard_buf.planar[125] == ciy_brightness_scale_channel(200, 10));
    assert(guard_buf.planar[CIY_LED_COUNT + 125] == ciy_brightness_scale_channel(150, 10));
    assert(guard_buf.planar[(2 * CIY_LED_COUNT) + 125] == ciy_brightness_scale_channel(100, 10));

    // Canaries prüfen: kein Memory Corruption vor oder nach dem Puffer
    for (size_t i = 0; i < 16; i++) {
        assert(guard_buf.canary_front[i] == 0xAA);
        assert(guard_buf.canary_back[i] == 0x55);
    }

    printf("[Test 2] ciy_brightness_apply_planar (Planar-Ebenen, Canaries, Rand-LEDs)... PASSED\n");
}

// ---------------------------------------------------------------------------
// 3. Tests für ciy_brightness_apply_slots
// ---------------------------------------------------------------------------
static void test_apply_slots(void) {
    uint8_t buffer[CIY_PLANAR_RGB_SIZE];
    uint8_t slot_mask[CIY_BRIGHTNESS_SLOT_COUNT];

    // NULL-Guards
    assert(!ciy_brightness_apply_slots(NULL, slot_mask));
    assert(!ciy_brightness_apply_slots(buffer, NULL));
    assert(!ciy_brightness_apply_slots(NULL, NULL));

    // Alles auf 255 setzen (Weiß)
    memset(buffer, 255, sizeof(buffer));
    for (size_t s = 0; s < CIY_BRIGHTNESS_SLOT_COUNT; s++) {
        slot_mask[s] = 100;
    }

    // Slot 1 (Esc) -> LED-Index 2 auf 50%
    // Slot 14 (W)  -> LED-Index 15 auf 20%
    slot_mask[1] = 50;
    slot_mask[14] = 20;

    assert(ciy_brightness_apply_slots(buffer, slot_mask));

    // Esc (LED 2)
    assert(buffer[2] == 128);
    assert(buffer[CIY_LED_COUNT + 2] == 128);
    assert(buffer[(2 * CIY_LED_COUNT) + 2] == 128);

    // W (LED 15)
    assert(buffer[15] == 51); // 255 * 0.20 = 51.0 -> 51
    assert(buffer[CIY_LED_COUNT + 15] == 51);
    assert(buffer[(2 * CIY_LED_COUNT) + 15] == 51);

    // Test: Ein Slot ohne Taste (z. B. Slot 0 oder nicht belegter Slot)
    // Suche einen Slot im Bereich 0..124, der keine Taste hat
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    assert(layout != NULL);
    int empty_slot = -1;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        if (!ciy_layout_find_key_by_slot(layout, s)) {
            empty_slot = s;
            break;
        }
    }
    assert(empty_slot >= 0);

    // Slot-Maske zurücksetzen und empty_slot auf 10% setzen
    for (size_t s = 0; s < CIY_BRIGHTNESS_SLOT_COUNT; s++) {
        slot_mask[s] = 100;
    }
    slot_mask[empty_slot] = 10;
    // Puffer merken
    uint8_t buffer_backup[CIY_PLANAR_RGB_SIZE];
    memcpy(buffer_backup, buffer, sizeof(buffer));

    // apply_slots muss true zurückgeben und der Puffer darf sich für unbenutzte Slots nicht verändern
    assert(ciy_brightness_apply_slots(buffer, slot_mask));
    assert(memcmp(buffer, buffer_backup, sizeof(buffer)) == 0);

    // Teste jede der 69 physischen Layout-Tasten einzeln
    for (size_t k = 0; k < layout->key_count; k++) {
        const CIYLogicalKey *key = &layout->keys[k];
        int slot = key->matrix_slot;
        int led = key->led_index;
        assert(led >= 0 && led < CIY_LED_COUNT);

        memset(buffer, 200, sizeof(buffer));
        for (size_t s = 0; s < CIY_BRIGHTNESS_SLOT_COUNT; s++) {
            slot_mask[s] = 100;
        }
        slot_mask[slot] = 40; // 40%

        assert(ciy_brightness_apply_slots(buffer, slot_mask));

        // Nur LED `led` darf skaliert sein: 200 * 40% = 80
        assert(buffer[led] == 80);
        assert(buffer[CIY_LED_COUNT + led] == 80);
        assert(buffer[(2 * CIY_LED_COUNT) + led] == 80);

        // Prüfe stichprobenartig die Nachbar-LEDs
        if (led > 0) assert(buffer[led - 1] == 200);
        if (led + 1 < CIY_LED_COUNT) assert(buffer[led + 1] == 200);
    }

    printf("[Test 3] ciy_brightness_apply_slots (ISO-DE Layout, ungenutzte Slots, 69 Tasten)... PASSED\n");
}

// ---------------------------------------------------------------------------
// 4. CLI Integrationstests via fork/execv (optional, wenn Binärpfad übergeben)
// ---------------------------------------------------------------------------
static void run_cli_argv(const char *cli_path, const char *const args[], char *out_buf, size_t buf_size, int *out_exit_code) {
    assert(cli_path != NULL);
    assert(args != NULL);
    int pipefd[2];
    int rc = pipe(pipefd);
    assert(rc == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execv(cli_path, (char *const *)args);
        _exit(127);
    }

    close(pipefd[1]);
    size_t total_read = 0;
    while (buf_size > 1 && total_read < buf_size - 1) {
        ssize_t n = read(pipefd[0], out_buf + total_read, (buf_size - 1) - total_read);
        if (n <= 0) {
            break;
        }
        total_read += (size_t)n;
    }
    if (buf_size > 0) {
        out_buf[total_read] = '\0';
    }
    close(pipefd[0]);

    int status = 0;
    pid_t waited = waitpid(pid, &status, 0);
    assert(waited == pid);
    assert(WIFEXITED(status));
    if (out_exit_code) {
        *out_exit_code = WEXITSTATUS(status);
    }
}

static void test_cli_integration(const char *cli_path) {
    if (!cli_path) {
        printf("[Test 4] CLI Integration übersprungen (kein Binary übergeben).\n");
        return;
    }

    char out[16384];
    int exit_code = 0;

    // 4.1 --help muss --key-brightness enthalten
    const char *const args_4_1[] = {cli_path, "--help", NULL};
    run_cli_argv(cli_path, args_4_1, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "--key-brightness") != NULL);

    // 4.2 Dry-Run mit fill: Hex-Dump Textprüfung (Taste W ist Slot 14 -> LED-Index 15)
    // Offset R: 1 + 15 = 16 im Report 0x0B
    // Bei --dump-bytes: fill #FF8040 --key-brightness 14 50
    const char *const args_4_2[] = {
        cli_path, "--dry-run", "fill", "#FF8040", "--key-brightness", "14", "50", "--dump-bytes", NULL
    };
    run_cli_argv(cli_path, args_4_2, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "DRY RUN") != NULL);
    assert(strstr(out, "Key-Brightness:") != NULL || strstr(out, "Slot 14") != NULL);
    assert(strstr(out, "Raw Wire Bytes (Hex):") != NULL);
    assert(strstr(out, "Schritt 1 (Report 0x0B, 379 Bytes):") != NULL);

    // 4.3 Dry-Run mit JSON & Prüfung der Wire-Bytes von Report 0x0B
    const char *const args_4_3[] = {
        cli_path, "--dry-run", "fill", "#FF8040", "--key-brightness", "14", "50", "--dump-bytes", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_3, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "\"dry_run\": true") != NULL);
    assert(strstr(out, "\"key_brightness\"") != NULL);
    assert(strstr(out, "\"slot\": 14") != NULL || strstr(out, "\"slot\":14") != NULL);
    assert(strstr(out, "\"percent\": 50") != NULL || strstr(out, "\"percent\":50") != NULL);

    // Exakte Report-0x0B Wire-Bytes prüfen (379 Bytes = 758 Hex-Zeichen)
    const char *hex_marker = "\"hex\": \"";
    const char *hex_start = strstr(out, hex_marker);
    assert(hex_start != NULL);
    hex_start += strlen(hex_marker);
    assert(strncmp(hex_start, "0b", 2) == 0);

    int r15 = ciy_protocol_report_0b_r_offset(15);
    int g15 = ciy_protocol_report_0b_g_offset(15);
    int b15 = ciy_protocol_report_0b_b_offset(15);
    assert(r15 == 16 && g15 == 142 && b15 == 268);

    // LED 15 (Slot 14, Taste W): 50% von #FF8040 -> R: 128 (80), G: 64 (40), B: 32 (20)
    assert(strncmp(hex_start + (r15 * 2), "80", 2) == 0);
    assert(strncmp(hex_start + (g15 * 2), "40", 2) == 0);
    assert(strncmp(hex_start + (b15 * 2), "20", 2) == 0);

    // Benachbarte LEDs 14 und 16: unskaliert auf ff (R), 80 (G), 40 (B)
    int r14 = ciy_protocol_report_0b_r_offset(14);
    int g14 = ciy_protocol_report_0b_g_offset(14);
    int b14 = ciy_protocol_report_0b_b_offset(14);
    assert(strncmp(hex_start + (r14 * 2), "ff", 2) == 0);
    assert(strncmp(hex_start + (g14 * 2), "80", 2) == 0);
    assert(strncmp(hex_start + (b14 * 2), "40", 2) == 0);

    int r16 = ciy_protocol_report_0b_r_offset(16);
    int g16 = ciy_protocol_report_0b_g_offset(16);
    int b16 = ciy_protocol_report_0b_b_offset(16);
    assert(strncmp(hex_start + (r16 * 2), "ff", 2) == 0);
    assert(strncmp(hex_start + (g16 * 2), "80", 2) == 0);
    assert(strncmp(hex_start + (b16 * 2), "40", 2) == 0);

    // 4.4 Syntax --key-brightness=<slot>:<percent>
    const char *const args_4_4[] = {
        cli_path, "--dry-run", "fill", "#00FF00", "--key-brightness=14:50", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_4, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "\"key_brightness\"") != NULL);

    // 4.5 Ablehnung bei Befehl set (Entscheidung 1)
    const char *const args_4_5[] = {
        cli_path, "--dry-run", "set", "1", "0", "2", "5", "--key-brightness", "14", "50", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_5, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "\"error\":") != NULL);
    assert(strstr(out, "nicht unterstützt") != NULL || strstr(out, "Report 0x0A") != NULL || strstr(out, "set") != NULL);

    // 4.6 Ablehnung bei ungültigem Slot (Slot 125 > 124)
    const char *const args_4_6[] = {
        cli_path, "--dry-run", "fill", "#FFFFFF", "--key-brightness", "125", "50", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_6, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "\"error\":") != NULL);

    // 4.7 Ablehnung bei Prozent > 100
    const char *const args_4_7[] = {
        cli_path, "--dry-run", "fill", "#FFFFFF", "--key-brightness", "14", "101", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_7, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "\"error\":") != NULL);

    // 4.8 Ablehnung bei Slot ohne Taste im ISO-DE-Layout (Entscheidung 3)
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    int empty_slot = -1;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        if (!ciy_layout_find_key_by_slot(layout, s)) {
            empty_slot = s;
            break;
        }
    }
    assert(empty_slot >= 0);
    char empty_slot_str[16];
    snprintf(empty_slot_str, sizeof(empty_slot_str), "%d", empty_slot);
    const char *const args_4_8[] = {
        cli_path, "--dry-run", "fill", "#FFFFFF", "--key-brightness", empty_slot_str, "50", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_8, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "\"error\":") != NULL);

    // 4.9 Regression: fill OHNE --key-brightness darf kein "key_brightness"-Feld im JSON haben
    const char *const args_4_9[] = {
        cli_path, "--dry-run", "fill", "#112233", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_9, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "key_brightness") == NULL);

    // 4.10 JSON-Escaping (Regression zu Stufe 1)
    const char *const args_4_10[] = {
        cli_path, "foo\"bar", "--key-brightness", "14", "50", "--json", NULL
    };
    run_cli_argv(cli_path, args_4_10, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "\"success\": false") != NULL);
    assert(strstr(out, "foo\\\"bar") != NULL);

    // 4.11 cli_path mit Leerzeichen (Symlink-Test)
    char symlink_path[512];
    snprintf(symlink_path, sizeof(symlink_path), "/tmp/ciy_ctl_space_%ld test", (long)getpid());
    unlink(symlink_path);
    int sym_rc = symlink(cli_path, symlink_path);
    assert(sym_rc == 0);

    const char *const args_4_11[] = {
        symlink_path, "--help", NULL
    };
    run_cli_argv(symlink_path, args_4_11, out, sizeof(out), &exit_code);
    unlink(symlink_path);
    assert(exit_code == 0);
    assert(strstr(out, "--key-brightness") != NULL);

    printf("[Test 4] CLI Integration (--key-brightness, dry-run, json, dump-bytes, error handling)... PASSED\n");
}

int main(int argc, char **argv) {
    printf("=== Starte CIY Brightness Engine Unit Tests ===\n");
    test_scale_channel();
    test_apply_planar();
    test_apply_slots();

    const char *cli_binary = (argc > 1) ? argv[1] : NULL;
    test_cli_integration(cli_binary);

    printf("=== Alle Brightness Tests erfolgreich! ===\n");
    return 0;
}
