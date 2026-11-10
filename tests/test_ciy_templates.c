#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "../src/core/ciy_templates.h"

// ---------------------------------------------------------------------------
// 1. Tests für Vorlagen-Registry & IDs
// ---------------------------------------------------------------------------
static void test_templates_registry(void) {
    size_t count = ciy_template_count();
    assert(count == 6);

    const char *expected_ids[6] = {
        "wasd-only",
        "arrows-only",
        "home-row",
        "game-fps",
        "coding",
        "night"
    };

    for (size_t i = 0; i < count; i++) {
        const CIYTemplate *t = ciy_template_get(i);
        assert(t != NULL);
        assert(t->id != NULL && strlen(t->id) > 0);
        assert(t->name != NULL && strlen(t->name) > 0);
        assert(t->description != NULL && strlen(t->description) > 0);
        assert(strcmp(t->id, expected_ids[i]) == 0);

        // Find-Suche per ID
        const CIYTemplate *found = ciy_template_find(expected_ids[i]);
        assert(found == t);
    }

    // Grenzfälle für ciy_template_get
    assert(ciy_template_get(6) == NULL);
    assert(ciy_template_get(999) == NULL);

    // Grenzfälle für ciy_template_find
    assert(ciy_template_find(NULL) == NULL);
    assert(ciy_template_find("") == NULL);
    assert(ciy_template_find("unknown-template") == NULL);
    assert(ciy_template_find("WASD-ONLY") == NULL); // Exakt case-sensitiv

    printf("[Test 1] Vorlagen-Registry & ID-Suche (6 Vorlagen, Grenzfälle)... PASSED\n");
}

// ---------------------------------------------------------------------------
// 2. Tests für Slot-Rollen & ISO-DE Hardware-Sicherheit
// ---------------------------------------------------------------------------
static void test_slot_roles_and_iso_de_safety(void) {
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    assert(layout != NULL);
    assert(layout->key_count == 69);

    // Sicherheitsregel 1: Matrix-Löcher (56 unbelegte Slots in 0..124)
    // müssen bei ALLEN Vorlagen immer zwingend CIY_TEMPLATE_ROLE_OFF erhalten!
    size_t hole_count = 0;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        if (!ciy_layout_find_key_by_slot(layout, s)) {
            hole_count++;
            for (size_t t = 0; t < ciy_template_count(); t++) {
                const CIYTemplate *tpl = ciy_template_get(t);
                assert(ciy_template_get_slot_role(tpl, s) == CIY_TEMPLATE_ROLE_OFF);
            }
        }
    }
    assert(hole_count == (125 - 69)); // 56 Matrix-Löcher

    // 2.1 wasd-only: Genau W (14), A (9), S (15), D (21) sind ACCENT, alle übrigen 65 Tasten BG
    const CIYTemplate *t_wasd = ciy_template_find("wasd-only");
    assert(t_wasd != NULL);
    size_t wasd_accent = 0, wasd_bg = 0, wasd_off = 0;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        CIYTemplateRole r = ciy_template_get_slot_role(t_wasd, s);
        if (s == 14 || s == 9 || s == 15 || s == 21) {
            assert(r == CIY_TEMPLATE_ROLE_ACCENT);
            wasd_accent++;
        } else if (ciy_layout_find_key_by_slot(layout, s)) {
            assert(r == CIY_TEMPLATE_ROLE_BG);
            wasd_bg++;
        } else {
            assert(r == CIY_TEMPLATE_ROLE_OFF);
            wasd_off++;
        }
    }
    assert(wasd_accent == 4);
    assert(wasd_bg == 65);
    assert(wasd_off == 56);

    // 2.2 arrows-only: Genau ▲ (88), ▼ (89), ◄ (83), ► (95) sind ACCENT
    const CIYTemplate *t_arrows = ciy_template_find("arrows-only");
    assert(t_arrows != NULL);
    size_t arr_accent = 0, arr_bg = 0;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        CIYTemplateRole r = ciy_template_get_slot_role(t_arrows, s);
        if (s == 88 || s == 89 || s == 83 || s == 95) {
            assert(r == CIY_TEMPLATE_ROLE_ACCENT);
            arr_accent++;
        } else if (ciy_layout_find_key_by_slot(layout, s)) {
            assert(r == CIY_TEMPLATE_ROLE_BG);
            arr_bg++;
        }
    }
    assert(arr_accent == 4);
    assert(arr_bg == 65);

    // 2.3 home-row: Grundstellung A (9), S (15), D (21), F (27), J (45), K (51), L (57), Ö (63)
    // inkl. Prüfung, dass Slot 63 wirklich "Ö" im ISO-DE Layout ist! Space (35) dezent als SECONDARY.
    const CIYTemplate *t_home = ciy_template_find("home-row");
    assert(t_home != NULL);
    const CIYLogicalKey *oe_key = ciy_layout_find_key_by_slot(layout, 63);
    assert(oe_key != NULL && strcmp(oe_key->name, "Ö") == 0);

    size_t home_accent = 0, home_sec = 0, home_bg = 0;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        CIYTemplateRole r = ciy_template_get_slot_role(t_home, s);
        if (s == 9 || s == 15 || s == 21 || s == 27 || s == 45 || s == 51 || s == 57 || s == 63) {
            assert(r == CIY_TEMPLATE_ROLE_ACCENT);
            home_accent++;
        } else if (s == 35) {
            assert(r == CIY_TEMPLATE_ROLE_SECONDARY);
            home_sec++;
        } else if (ciy_layout_find_key_by_slot(layout, s)) {
            assert(r == CIY_TEMPLATE_ROLE_BG);
            home_bg++;
        }
    }
    assert(home_accent == 8);
    assert(home_sec == 1);
    assert(home_bg == 60);

    // 2.4 game-fps: WASD Hauptakzent, Waffenreihe 1..5, R, E, F, Space, Shift (4, 82), Strg (5, 80), Esc (1)
    const CIYTemplate *t_fps = ciy_template_find("game-fps");
    assert(t_fps != NULL);
    size_t fps_accent = 0, fps_sec = 0, fps_bg = 0;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        CIYTemplateRole r = ciy_template_get_slot_role(t_fps, s);
        if (s == 14 || s == 9 || s == 15 || s == 21) {
            assert(r == CIY_TEMPLATE_ROLE_ACCENT);
            fps_accent++;
        } else if (s == 7 || s == 13 || s == 19 || s == 25 || s == 31 ||
                   s == 26 || s == 20 || s == 27 || s == 35 ||
                   s == 4 || s == 82 || s == 5 || s == 80 || s == 1) {
            assert(r == CIY_TEMPLATE_ROLE_SECONDARY);
            fps_sec++;
        } else if (ciy_layout_find_key_by_slot(layout, s)) {
            assert(r == CIY_TEMPLATE_ROLE_BG);
            fps_bg++;
        }
    }
    assert(fps_accent == 4);
    assert(fps_sec == 14); // 5 Ziffern + 3 Aktionen + Space + 2x Shift + 2x Strg + Esc
    assert(fps_bg == 51);

    // 2.5 coding: Modifikatoren (beidseitig) + Esc, Tab, Enter, Backspace, Pfeile sind ACCENT, Alphas DIM
    const CIYTemplate *t_code = ciy_template_find("coding");
    assert(t_code != NULL);
    size_t code_accent = 0, code_dim = 0;
    for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
        CIYTemplateRole r = ciy_template_get_slot_role(t_code, s);
        if (s == 1 || s == 2 || s == 3 || s == 4 || s == 82 || s == 5 || s == 80 ||
            s == 11 || s == 17 || s == 53 || s == 77 || s == 79 || s == 81 ||
            s == 88 || s == 89 || s == 83 || s == 95) {
            assert(r == CIY_TEMPLATE_ROLE_ACCENT);
            code_accent++;
        } else if (ciy_layout_find_key_by_slot(layout, s)) {
            assert(r == CIY_TEMPLATE_ROLE_DIM);
            code_dim++;
        }
    }
    assert(code_accent == 17);
    assert(code_dim == 52);

    printf("[Test 2] ISO-DE Slot-Rollen & Matrix-Loch-Schutz (Ö-Taste, Dual-Shift/Strg)... PASSED\n");
}

// ---------------------------------------------------------------------------
// 3. Tests für Farbgenerierung & Benutzerdefinierte Overrides
// ---------------------------------------------------------------------------
static void test_color_generation_and_overrides(void) {
    const CIYTemplate *t_wasd = ciy_template_find("wasd-only");
    assert(t_wasd != NULL);

    uint8_t slots[CIY_MAX_MATRIX_SLOT + 1][3];

    // Standard-Farben
    assert(ciy_template_generate_slots(t_wasd, NULL, NULL, slots));
    // W (14) muss Standard-Akzent haben
    assert(slots[14][0] == t_wasd->default_accent[0]);
    assert(slots[14][1] == t_wasd->default_accent[1]);
    assert(slots[14][2] == t_wasd->default_accent[2]);
    // Q (8) muss Standard-BG (#000000) haben
    assert(slots[8][0] == 0 && slots[8][1] == 0 && slots[8][2] == 0);
    // Unbelegter Slot 0 muss zwingend 0 sein
    assert(slots[0][0] == 0 && slots[0][1] == 0 && slots[0][2] == 0);

    // Overrides: Akzent #FF0055, Hintergrund #001122
    uint8_t custom_accent[3] = {0xFF, 0x00, 0x55};
    uint8_t custom_bg[3] = {0x00, 0x11, 0x22};
    assert(ciy_template_generate_slots(t_wasd, custom_accent, custom_bg, slots));

    // W (14) muss custom_accent haben
    assert(slots[14][0] == 0xFF && slots[14][1] == 0x00 && slots[14][2] == 0x55);
    // Q (8) muss custom_bg haben
    assert(slots[8][0] == 0x00 && slots[8][1] == 0x11 && slots[8][2] == 0x22);
    // WICHTIG: Slot 0 (Matrix-Loch) MUSS immer 0 bleiben, selbst mit custom_bg!
    assert(slots[0][0] == 0 && slots[0][1] == 0 && slots[0][2] == 0);

    // Planar-Puffer Generierung
    uint8_t planar[CIY_PLANAR_RGB_SIZE];
    assert(ciy_template_generate_planar(t_wasd, custom_accent, custom_bg, planar));

    // LED 0 (nicht belegt im Report 0x0B) muss 0 sein
    assert(planar[0] == 0);
    assert(planar[CIY_LED_COUNT] == 0);
    assert(planar[2 * CIY_LED_COUNT] == 0);

    // Taste W (Slot 14 -> LED-Index 15)
    assert(planar[15] == 0xFF);
    assert(planar[CIY_LED_COUNT + 15] == 0x00);
    assert(planar[2 * CIY_LED_COUNT + 15] == 0x55);

    // Taste Q (Slot 8 -> LED-Index 9)
    assert(planar[9] == 0x00);
    assert(planar[CIY_LED_COUNT + 9] == 0x11);
    assert(planar[2 * CIY_LED_COUNT + 9] == 0x22);

    printf("[Test 3] Farbgenerierung, Overrides (--accent, --bg) & Planar-Layout... PASSED\n");
}

// ---------------------------------------------------------------------------
// 4. Tests für JSON-Export & 100 % Roundtrip-Validierung
// ---------------------------------------------------------------------------
static void test_json_export_and_roundtrip(void) {
    char json_buf[8192];

    // Pufferüberlaufschutz
    char tiny_buf[64];
    const CIYTemplate *t_first = ciy_template_get(0);
    assert(!ciy_template_to_json(t_first, NULL, NULL, NULL, tiny_buf, sizeof(tiny_buf)));

    // Alle 6 Vorlagen serialisieren und JSON-Struktur prüfen
    for (size_t i = 0; i < ciy_template_count(); i++) {
        const CIYTemplate *tpl = ciy_template_get(i);
        assert(tpl != NULL);

        // Export mit benutzerdefiniertem Namen
        char custom_name[64];
        snprintf(custom_name, sizeof(custom_name), "Testprofil-%s", tpl->id);
        assert(ciy_template_to_json(tpl, custom_name, NULL, NULL, json_buf, sizeof(json_buf)));

        // Prüfen, ob Name und Slots-Objekt enthalten sind
        assert(strstr(json_buf, "{\n  \"name\": \"Testprofil-") != NULL);
        assert(strstr(json_buf, "\",\n  \"slots\": {\n") != NULL);
        assert(strstr(json_buf, "    \"0\": \"#") != NULL);
        assert(strstr(json_buf, "    \"124\": \"#") != NULL);
        assert(strstr(json_buf, "  }\n}\n") != NULL);

        // Prüfen, dass alle 125 Slots (0..124) definiert sind
        for (int s = 0; s <= CIY_MAX_MATRIX_SLOT; s++) {
            char key_str[32];
            snprintf(key_str, sizeof(key_str), "\"%d\":", s);
            assert(strstr(json_buf, key_str) != NULL);
        }
    }

    // Grenzfall-Tests für Profilnamen-Länge: 63 Bytes erlaubt, 64 Bytes verboten
    char name_63[64];
    memset(name_63, 'x', 63);
    name_63[63] = '\0';
    assert(strlen(name_63) == CIY_TEMPLATE_MAX_NAME_LEN);
    assert(ciy_template_to_json(t_first, name_63, NULL, NULL, json_buf, sizeof(json_buf)));
    assert(strstr(json_buf, name_63) != NULL);

    char name_64[65];
    memset(name_64, 'y', 64);
    name_64[64] = '\0';
    assert(strlen(name_64) == 64);
    json_buf[0] = 'Z';
    assert(!ciy_template_to_json(t_first, name_64, NULL, NULL, json_buf, sizeof(json_buf)));
    assert(json_buf[0] == '\0');

    // Steuerzeichen im Namen: RFC-8259-Maskierung (\b, \f, \u0001, \u001b)
    const char *ctrl_name = "Test\b\f\x01\x1bName";
    assert(ciy_template_to_json(t_first, ctrl_name, NULL, NULL, json_buf, sizeof(json_buf)));
    assert(strstr(json_buf, "\"name\": \"Test\\b\\f\\u0001\\u001bName\"") != NULL);

    printf("[Test 4] JSON-Export Struktur & Validität (125 Slots)... PASSED\n");
}

// ---------------------------------------------------------------------------
// 5. CLI-Integrationstests per Subprozess
// ---------------------------------------------------------------------------
static void run_cli_argv(const char *cli_path, const char *const args[], char *out_buf, size_t buf_size, int *out_exit_code) {
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
        if (n <= 0) break;
        total_read += (size_t)n;
    }
    if (buf_size > 0) out_buf[total_read] = '\0';
    close(pipefd[0]);

    int status = 0;
    pid_t waited = waitpid(pid, &status, 0);
    assert(waited == pid);
    assert(WIFEXITED(status));
    if (out_exit_code) *out_exit_code = WEXITSTATUS(status);
}

static void test_cli_integration(const char *cli_path) {
    if (!cli_path) {
        printf("[Test 5] CLI Integration übersprungen (kein Binary übergeben).\n");
        return;
    }

    char out[16384];
    int exit_code = 0;

    // 5.1 Hilfe enthält template und Optionen
    const char *const args_5_1[] = {cli_path, "--help", NULL};
    run_cli_argv(cli_path, args_5_1, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "template") != NULL);
    assert(strstr(out, "--accent") != NULL);
    assert(strstr(out, "--bg") != NULL);
    assert(strstr(out, "--export") != NULL);

    // 5.2 template --list (Text)
    const char *const args_5_2[] = {cli_path, "template", "--list", NULL};
    run_cli_argv(cli_path, args_5_2, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "wasd-only") != NULL);
    assert(strstr(out, "arrows-only") != NULL);
    assert(strstr(out, "home-row") != NULL);
    assert(strstr(out, "game-fps") != NULL);
    assert(strstr(out, "coding") != NULL);
    assert(strstr(out, "night") != NULL);

    // 5.3 template --list --json
    const char *const args_5_3[] = {cli_path, "template", "--list", "--json", NULL};
    run_cli_argv(cli_path, args_5_3, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "\"id\": \"wasd-only\"") != NULL);
    assert(strstr(out, "\"default_accent\": \"#00FFCC\"") != NULL);

    // 5.4 template wasd-only --dry-run
    const char *const args_5_4[] = {cli_path, "--dry-run", "template", "wasd-only", NULL};
    run_cli_argv(cli_path, args_5_4, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "DRY RUN") != NULL);
    assert(strstr(out, "WASD Only") != NULL);

    // 5.5 template wasd-only --dry-run --dump-bytes --json mit -b und --key-brightness
    const char *const args_5_5[] = {
        cli_path, "--dry-run", "template", "wasd-only", "-b", "3",
        "--key-brightness", "14", "50", "--dump-bytes", "--json", NULL
    };
    run_cli_argv(cli_path, args_5_5, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "\"dry_run\": true") != NULL);
    assert(strstr(out, "\"brightness\": 3") != NULL);
    assert(strstr(out, "\"key_brightness\"") != NULL);

    // 5.6 template wasd-only --export - (stdout)
    const char *const args_5_6[] = {cli_path, "template", "wasd-only", "--export", "-", NULL};
    run_cli_argv(cli_path, args_5_6, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "\"name\": \"WASD Only\"") != NULL);
    assert(strstr(out, "\"slots\": {") != NULL);

    // 5.7 template export aller 6 Vorlagen & Rückvalidierung per validate-profile und load --dry-run
    // inkl. Byte-für-Byte-Vergleich der Wire-Reports (Report 0x0B)
    for (size_t t = 0; t < ciy_template_count(); t++) {
        const CIYTemplate *tpl = ciy_template_get(t);
        char exp_file[512];
        snprintf(exp_file, sizeof(exp_file), "/tmp/ciy_test_exp_%s_%ld.json", tpl->id, (long)getpid());
        unlink(exp_file);

        // Export mit Name
        char prof_name[64];
        snprintf(prof_name, sizeof(prof_name), "Export-%s", tpl->name);
        const char *const args_exp[] = {
            cli_path, "template", tpl->id, "--name", prof_name, "--export", exp_file, NULL
        };
        run_cli_argv(cli_path, args_exp, out, sizeof(out), &exit_code);
        assert(exit_code == 0);

        // validate-profile aufrufen (verifies file exists and is readable)
        const char *const args_val[] = {cli_path, "validate-profile", exp_file, "--json", NULL};
        run_cli_argv(cli_path, args_val, out, sizeof(out), &exit_code);
        assert(exit_code == 0);
        assert(strstr(out, "\"valid\": true") != NULL);
        assert(strstr(out, "\"keys\": 125") != NULL);

        // load --dry-run aufrufen
        char out_load[16384];
        const char *const args_load[] = {cli_path, "--dry-run", "load", exp_file, "--dump-bytes", "--json", NULL};
        run_cli_argv(cli_path, args_load, out_load, sizeof(out_load), &exit_code);
        assert(exit_code == 0);
        assert(strstr(out_load, "\"dry_run\": true") != NULL);

        // template --dry-run direkt aufrufen
        char out_tpl[16384];
        const char *const args_tpl[] = {cli_path, "--dry-run", "template", tpl->id, "--dump-bytes", "--json", NULL};
        run_cli_argv(cli_path, args_tpl, out_tpl, sizeof(out_tpl), &exit_code);
        assert(exit_code == 0);

        // Hex-Wire-Bytes von Report 0x0B (758 Hex-Zeichen) vergleichen
        const char *hex_marker = "\"hex\": \"";
        const char *hex_load = strstr(out_load, hex_marker);
        const char *hex_tpl = strstr(out_tpl, hex_marker);
        assert(hex_load != NULL);
        assert(hex_tpl != NULL);
        assert(strncmp(hex_load, hex_tpl, strlen(hex_marker) + 758) == 0);

        unlink(exp_file);
    }

    // 5.8 Fehlerbehandlung: Datei existiert bereits
    char exist_file[512];
    snprintf(exist_file, sizeof(exist_file), "/tmp/ciy_test_exist_%ld.json", (long)getpid());
    int exist_fd = open(exist_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(exist_fd >= 0);
    close(exist_fd);

    const char *const args_5_8[] = {
        cli_path, "template", "wasd-only", "--export", exist_file, NULL
    };
    run_cli_argv(cli_path, args_5_8, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "existiert bereits") != NULL);
    unlink(exist_file);

    // 5.9 Fehlerbehandlung: Symlinks zwingend ablehnen (O_NOFOLLOW)
    char symlink_target[512];
    char symlink_file[512];
    snprintf(symlink_target, sizeof(symlink_target), "/tmp/ciy_sym_target_%ld.json", (long)getpid());
    snprintf(symlink_file, sizeof(symlink_file), "/tmp/ciy_sym_link_%ld.json", (long)getpid());
    unlink(symlink_file);
    unlink(symlink_target);
    int sym_rc = symlink(symlink_target, symlink_file);
    assert(sym_rc == 0);

    const char *const args_5_9[] = {
        cli_path, "template", "wasd-only", "--export", symlink_file, NULL
    };
    run_cli_argv(cli_path, args_5_9, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "symbolischer Link") != NULL || strstr(out, "abgelehnt") != NULL);
    unlink(symlink_file);

    // 5.10 Fehlerbehandlung: Unbekannte Template-ID
    const char *const args_5_10[] = {cli_path, "template", "nonexistent-id", "--json", NULL};
    run_cli_argv(cli_path, args_5_10, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "\"success\": false") != NULL);
    assert(strstr(out, "Unbekannte Vorlage") != NULL);

    // 5.11 Fehlerbehandlung: Fehlende Template-ID
    const char *const args_5_11[] = {cli_path, "template", "--json", NULL};
    run_cli_argv(cli_path, args_5_11, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "Fehlende Template-ID") != NULL);

    // 5.12 Fehlerbehandlung: Ungültige Hex-Farbe für --accent
    const char *const args_5_12[] = {cli_path, "template", "wasd-only", "--accent", "XYZ", "--json", NULL};
    run_cli_argv(cli_path, args_5_12, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "Ungültige Hex-Farbe") != NULL);

    // 5.13 Fehlerbehandlung: --name ohne --export
    const char *const args_5_13[] = {cli_path, "template", "wasd-only", "--name", "Foo", "--json", NULL};
    run_cli_argv(cli_path, args_5_13, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "--name kann nur in Kombination mit --export") != NULL);

    // 5.14 Fehlerbehandlung: --list mit anderen Optionen kombiniert
    const char *const args_5_14[] = {cli_path, "template", "--list", "--accent", "#FF0000", "--json", NULL};
    run_cli_argv(cli_path, args_5_14, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "--list kann nicht mit anderen Vorlagen-Optionen kombiniert werden") != NULL);

    // 5.15 Fehlerbehandlung: --export mit nachfolgendem Flag (--json, --list) oder leer
    unlink("--json");
    unlink("--list");
    const char *const args_5_15a[] = {cli_path, "template", "wasd-only", "--export", "--json", NULL};
    run_cli_argv(cli_path, args_5_15a, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(access("--json", F_OK) != 0);

    const char *const args_5_15b[] = {cli_path, "template", "wasd-only", "--export", "--list", NULL};
    run_cli_argv(cli_path, args_5_15b, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(access("--list", F_OK) != 0);

    const char *const args_5_15c[] = {cli_path, "template", "wasd-only", "--export=", NULL};
    run_cli_argv(cli_path, args_5_15c, out, sizeof(out), &exit_code);
    assert(exit_code == 1);

    // 5.16 --name Validierung: 64 Zeichen abgelehnt, 63 Zeichen inkl. Steuerzeichen akzeptiert
    char name_64[65];
    memset(name_64, 'A', 64);
    name_64[64] = '\0';
    const char *const args_5_16a[] = {cli_path, "template", "wasd-only", "--name", name_64, "--export", "-", NULL};
    run_cli_argv(cli_path, args_5_16a, out, sizeof(out), &exit_code);
    assert(exit_code == 1);
    assert(strstr(out, "Profilname zu lang") != NULL);

    const char *const args_5_16b[] = {cli_path, "template", "wasd-only", "--name", "", "--export", "-", NULL};
    run_cli_argv(cli_path, args_5_16b, out, sizeof(out), &exit_code);
    assert(exit_code == 1);

    char name_63[64];
    memset(name_63, 'B', 63);
    name_63[10] = '\t';
    name_63[63] = '\0';
    char file_roundtrip[512];
    snprintf(file_roundtrip, sizeof(file_roundtrip), "/tmp/ciy_test_name63_%ld.json", (long)getpid());
    unlink(file_roundtrip);

    const char *const args_5_16c[] = {cli_path, "template", "wasd-only", "--name", name_63, "--export", file_roundtrip, NULL};
    run_cli_argv(cli_path, args_5_16c, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    int exported_fd = open(file_roundtrip, O_RDONLY);
    assert(exported_fd >= 0);
    close(exported_fd);

    const char *const args_5_16d[] = {cli_path, "validate-profile", file_roundtrip, "--json", NULL};
    run_cli_argv(cli_path, args_5_16d, out, sizeof(out), &exit_code);
    assert(exit_code == 0);
    assert(strstr(out, "\"valid\": true") != NULL);
    unlink(file_roundtrip);

    // 5.17 Nicht unterstützte Optionen und Kombinationsverbote (-3, --apply, ciy_ctl -, --list --dry-run, --list -b 3)
    const char *const args_5_17a[] = {cli_path, "template", "wasd-only", "-3", NULL};
    run_cli_argv(cli_path, args_5_17a, out, sizeof(out), &exit_code);
    assert(exit_code == 1);

    const char *const args_5_17b[] = {cli_path, "template", "wasd-only", "--apply", NULL};
    run_cli_argv(cli_path, args_5_17b, out, sizeof(out), &exit_code);
    assert(exit_code == 1);

    const char *const args_5_17c[] = {cli_path, "-", NULL};
    run_cli_argv(cli_path, args_5_17c, out, sizeof(out), &exit_code);
    assert(exit_code == 1);

    const char *const args_5_17d[] = {cli_path, "template", "--list", "--dry-run", NULL};
    run_cli_argv(cli_path, args_5_17d, out, sizeof(out), &exit_code);
    assert(exit_code == 1);

    const char *const args_5_17e[] = {cli_path, "template", "--list", "-b", "3", NULL};
    run_cli_argv(cli_path, args_5_17e, out, sizeof(out), &exit_code);
    assert(exit_code == 1);

    // 5.18 template wasd-only --export - mit schreibgeschütztem STDOUT_FILENO
    {
        int pipefd[2];
        int rc = pipe(pipefd);
        assert(rc == 0);

        pid_t pid = fork();
        assert(pid >= 0);

        if (pid == 0) {
            close(pipefd[0]);
            int ro_fd = open("/dev/null", O_RDONLY);
            assert(ro_fd >= 0);
            dup2(ro_fd, STDOUT_FILENO);
            close(ro_fd);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            const char *const args_5_18[] = {cli_path, "template", "wasd-only", "--export", "-", NULL};
            execv(cli_path, (char *const *)args_5_18);
            _exit(127);
        }

        close(pipefd[1]);
        size_t total_read = 0;
        while (sizeof(out) > 1 && total_read < sizeof(out) - 1) {
            ssize_t n = read(pipefd[0], out + total_read, (sizeof(out) - 1) - total_read);
            if (n <= 0) break;
            total_read += (size_t)n;
        }
        out[total_read] = '\0';
        close(pipefd[0]);

        int status = 0;
        pid_t waited = waitpid(pid, &status, 0);
        assert(waited == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 1);
        assert(strstr(out, "Fehler") != NULL);
    }

    printf("[Test 5] CLI Integration (--list, --export, Symlink-Schutz, Validierung, Dry-Run)... PASSED\n");
}

int main(int argc, char **argv) {
    printf("=== Starte CIY Profile Templates Engine Unit Tests ===\n");
    test_templates_registry();
    test_slot_roles_and_iso_de_safety();
    test_color_generation_and_overrides();
    test_json_export_and_roundtrip();

    const char *cli_binary = (argc > 1) ? argv[1] : NULL;
    test_cli_integration(cli_binary);

    printf("=== Alle Profile Templates Tests erfolgreich bestanden! ===\n");
    return 0;
}
