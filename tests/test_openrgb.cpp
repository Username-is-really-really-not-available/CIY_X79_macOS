#include <iostream>
#include <cassert>
#include <climits>
#include <vector>
#include <cstring>
#include "RGBController.h"
#include "hidapi.h"
#include "../contrib/openrgb/CIYController.h"
#include "../contrib/openrgb/RGBController_CIYX79.h"

// Deklaration aus CIYX79Detector.cpp
void DetectCIYX79Controllers(hid_device_info* info, const std::string& name);

static void test_detector_safe_handshake()
{
    std::cout << "[Test 1] OpenRGB Detector: Nicht-destruktiver Handshake & Sino Wealth Schutz... " << std::flush;
    hid_mock_reset();
    ResourceManager::get()->ClearControllers();

    hid_device_info info;
    memset(&info, 0, sizeof(info));
    info.path = (char*)"/dev/hidraw1";
    info.vendor_id = CIY_VENDOR_ID;
    info.product_id = CIY_PRODUCT_ID;
    info.interface_number = CIY_INTERFACE_NUM;

    // 0. info == nullptr oder info.path == nullptr
    DetectCIYX79Controllers(nullptr, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);

    info.path = nullptr;
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);
    info.path = (char*)"/dev/hidraw1";

    // 1. Falsche VID
    info.vendor_id = 0x1234;
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);
    info.vendor_id = CIY_VENDOR_ID;

    // 2. Falsche PID
    info.product_id = 0x5678;
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);
    info.product_id = CIY_PRODUCT_ID;

    // 3. Falsches Interface (z. B. Interface 0 = Boot Keyboard)
    info.interface_number = 0;
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);
    info.interface_number = CIY_INTERFACE_NUM;

    // 4. Inkompatible Firmware: Falsche Report-ID (Schutz vor Issue #2679)
    uint8_t bad_report_id[] = {0x05, 1, 0, 0, 5};
    hid_mock_set_feature_report_response(bad_report_id, sizeof(bad_report_id));
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);

    // 5. Inkompatible Firmware: Modus außerhalb 0..20
    uint8_t bad_mode[] = {0x0A, 50, 0, 0, 5};
    hid_mock_set_feature_report_response(bad_mode, sizeof(bad_mode));
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);

    // 5b. Inkompatible Firmware: Geschwindigkeit > 4 (CIY_MAX_SPEED = 4)
    uint8_t bad_speed[] = {0x0A, 19, 0, 5, 5};
    hid_mock_set_feature_report_response(bad_speed, sizeof(bad_speed));
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);

    // 6. Inkompatible Firmware: Helligkeit > 5
    uint8_t bad_bright[] = {0x0A, 19, 0, 0, 9};
    hid_mock_set_feature_report_response(bad_bright, sizeof(bad_bright));
    DetectCIYX79Controllers(&info, "Test");
    assert(ResourceManager::get()->GetControllers().empty());
    assert(hid_mock_get_sent_count() == 0);

    // 7. Valide CIY X79 Firmware-Antwort
    uint8_t valid_resp[] = {0x0A, 19, 0, 2, 5};
    hid_mock_set_feature_report_response(valid_resp, sizeof(valid_resp));
    DetectCIYX79Controllers(&info, "CIY X79");
    assert(ResourceManager::get()->GetControllers().size() == 1);
    // Beachte: Während der Erkennung wurden KEINERLEI Reports gesendet (0 Schreibzugriffe!)
    assert(hid_mock_get_sent_count() == 0);

    std::cout << "PASSED (0 Schreibzugriffe vor Verifikation)\n";
}

static void test_matrix_and_dual_cell_enter()
{
    std::cout << "[Test 2] OpenRGB Matrix Zone & Dual-Cell ISO Enter... " << std::flush;
    auto controllers = ResourceManager::get()->GetControllers();
    assert(!controllers.empty());
    RGBController* c = controllers[0];

    assert(c->zones.size() == 1);
    assert(c->zones[0].type == ZONE_TYPE_MATRIX);
    assert(c->zones[0].matrix_map != nullptr);
    assert(c->zones[0].matrix_map->height == 5);
    assert(c->zones[0].matrix_map->width == 16);
    assert(c->leds.size() == 69);
    assert(c->colors.size() == 69);

    // Dual-Cell Enter Mapping:
    // Row 1, Col 13 (Index 1 * 16 + 13 = 29) und Row 2, Col 13 (Index 2 * 16 + 13 = 45)
    unsigned int enter_idx_r1 = c->zones[0].matrix_map->map[1 * 16 + 13];
    unsigned int enter_idx_r2 = c->zones[0].matrix_map->map[2 * 16 + 13];

    assert(enter_idx_r1 != 0xFFFFFFFF);
    assert(enter_idx_r2 != 0xFFFFFFFF);
    assert(enter_idx_r1 == enter_idx_r2);
    assert(enter_idx_r1 < c->leds.size());

    // Hardware LED-Index von Enter muss 82 sein (Slot 81)
    assert(c->leds[enter_idx_r1].value == 82);
    assert(c->leds[enter_idx_r1].name.find("Enter") != std::string::npos);

    // Reentrancy-Prüfung für SetupZones (kein Speicherleck, keine Duplikate)
    c->SetupZones();
    assert(c->zones.size() == 1);
    assert(c->leds.size() == 69);

    std::cout << "PASSED (Row 1 Col 13 == Row 2 Col 13 == LED 82, Reentrancy safe)\n";
}

static void test_direct_mode_rgb_transfer()
{
    std::cout << "[Test 3] OpenRGB Direct Mode (Planar Report 0x0B Übertragung)... " << std::flush;
    RGBController* c = ResourceManager::get()->GetControllers()[0];

    // Alle Tasten auf Grün setzen, Enter auf Rot
    for (size_t i = 0; i < c->colors.size(); i++)
    {
        c->colors[i] = ToRGBColor(0, 255, 0); // Grün
    }

    unsigned int enter_idx = c->zones[0].matrix_map->map[1 * 16 + 13];
    c->colors[enter_idx] = ToRGBColor(255, 0, 0); // Rot

    c->DeviceUpdateLEDs();

    size_t last_len = 0;
    const unsigned char* last_buf = hid_mock_get_last_sent(&last_len);
    assert(last_buf != nullptr);
    assert(last_len == CIY_REPORT_0B_SIZE);
    assert(last_buf[0] == CIY_REPORT_0B_ID);

    // Enter: LED 82
    // R-Byte Offset = 1 + 82 = 83
    // G-Byte Offset = 1 + 126 + 82 = 209
    // B-Byte Offset = 1 + 252 + 82 = 335
    assert(last_buf[83] == 255);
    assert(last_buf[209] == 0);
    assert(last_buf[335] == 0);

    // Esc: LED 2 (Slot 1)
    // R = 0, G = 255, B = 0
    assert(last_buf[1 + 2] == 0);
    assert(last_buf[1 + 126 + 2] == 255);
    assert(last_buf[1 + 252 + 2] == 0);

    std::cout << "PASSED (379 Bytes Planar RGB exakt verifiziert)\n";
}

static void test_mode_switching()
{
    std::cout << "[Test 4] OpenRGB Mode Switching & Farbabstand... " << std::flush;
    RGBController* c = ResourceManager::get()->GetControllers()[0];

    // 1. Static Mode (Modus 15) mit gedimmtem Grün (0, 120, 0)
    c->active_mode = 1;
    assert(c->modes[1].value == 15);
    c->modes[1].colors[0] = ToRGBColor(0, 120, 0);
    c->DeviceUpdateMode();

    size_t last_len = 0;
    const unsigned char* last_buf = hid_mock_get_last_sent(&last_len);
    assert(last_buf != nullptr);
    assert(last_len == CIY_REPORT_0A_SIZE);
    assert(last_buf[0] == CIY_REPORT_0A_ID);
    assert(last_buf[1] == 15);
    // Euklidischer Farbabstand muss zuverlässig Grün (Index 3) erkennen:
    assert(last_buf[2] == 3);

    // 1b. Static Mode mit Orange (230, 110, 10) -> Palette Index 1 (Orange)
    c->modes[1].colors[0] = ToRGBColor(230, 110, 10);
    c->DeviceUpdateMode();
    last_buf = hid_mock_get_last_sent(&last_len);
    assert(last_buf[2] == 1);

    // 1c. Static Mode mit Blau (10, 20, 180) -> Palette Index 5 (Blau)
    c->modes[1].colors[0] = ToRGBColor(10, 20, 180);
    c->DeviceUpdateMode();
    last_buf = hid_mock_get_last_sent(&last_len);
    assert(last_buf[2] == 5);

    // 2. Off Mode (Modus 0)
    c->active_mode = 2;
    assert(c->modes[2].value == 0);
    c->DeviceUpdateMode();

    last_buf = hid_mock_get_last_sent(&last_len);
    assert(last_buf != nullptr);
    assert(last_len == CIY_REPORT_0A_SIZE);
    assert(last_buf[0] == CIY_REPORT_0A_ID);
    assert(last_buf[1] == 0);

    // 3. Preset Wave (Modus 1)
    int wave_idx = -1;
    for (size_t i = 0; i < c->modes.size(); i++)
    {
        if (c->modes[i].value == 1)
        {
            wave_idx = (int)i;
            break;
        }
    }
    assert(wave_idx >= 0);
    c->active_mode = wave_idx;
    c->DeviceUpdateMode();

    last_buf = hid_mock_get_last_sent(&last_len);
    assert(last_buf != nullptr);
    assert(last_len == CIY_REPORT_0A_SIZE);
    assert(last_buf[0] == CIY_REPORT_0A_ID);
    assert(last_buf[1] == 1);

    // 4. Direct Mode (Modus 19) Übertragung
    c->active_mode = 0;
    assert(c->modes[0].value == 19);
    c->DeviceUpdateMode();

    last_buf = hid_mock_get_last_sent(&last_len);
    assert(last_buf != nullptr);
    assert(last_len == CIY_REPORT_0A_SIZE);
    assert(last_buf[0] == CIY_REPORT_0A_ID);
    assert(last_buf[1] == 19);

    std::cout << "PASSED\n";
}

static void test_send_direct_bounds_and_null_guards()
{
    std::cout << "[Test 5] OpenRGB SendDirect Bounds (INT_MIN/INT_MAX, asymmetrische Vektoren) & NULL-Guards... " << std::flush;
    hid_mock_reset();

    // 1c: CIYController mit nullptr-Handle und nullptr-Pfad darf niemals dereferenzieren
    {
        CIYController null_ctrl(nullptr, nullptr);
        assert(null_ctrl.GetLocation().empty());
        assert(null_ctrl.GetSerial().empty());
        std::vector<RGBColor> cols = {ToRGBColor(255, 0, 0)};
        std::vector<int> idxs = {2};
        assert(!null_ctrl.SendDirect(cols, idxs));
        assert(!null_ctrl.SetMode(19, 0, 0, 5));
        uint8_t m = 0xAA, c = 0xAA, s = 0xAA, b = 0xAA;
        assert(!null_ctrl.GetState(m, c, s, b));
        assert(m == 0 && c == 0 && s == 0 && b == 0);
    }

    // 1c: RGBController_CIYX79(nullptr) Null-Guard im Konstruktor & Update-Pfaden
    {
        RGBController_CIYX79 bad_rgb_ctrl(nullptr);
        assert(bad_rgb_ctrl.location.empty());
        assert(bad_rgb_ctrl.serial.empty());
        bad_rgb_ctrl.DeviceUpdateLEDs();
        bad_rgb_ctrl.DeviceUpdateMode();
    }

    // 1a & 1b: SendDirect mit INT_MIN, -1, 0, 125, 126, INT_MAX und asymmetrischen Vektoren
    hid_device* raw_dev = hid_open_path("/dev/hidraw1");
    assert(raw_dev != nullptr);
    CIYController ctrl(raw_dev, "/dev/hidraw1");

    // Fall A: colors größer als led_indices + Rand-/Extremindizes
    std::vector<RGBColor> more_colors = {
        ToRGBColor(10, 20, 30),   // INT_MIN -> ignoriert
        ToRGBColor(40, 50, 60),   // -1 -> ignoriert
        ToRGBColor(11, 22, 33),   // LED 0 -> geschrieben
        ToRGBColor(44, 55, 66),   // LED 125 -> geschrieben
        ToRGBColor(70, 80, 90),   // 126 -> ignoriert
        ToRGBColor(91, 92, 93),   // INT_MAX -> ignoriert
        ToRGBColor(99, 99, 99)    // Überschüssiges Element ohne Index -> ignoriert
    };
    std::vector<int> edge_indices = {INT_MIN, -1, 0, 125, 126, INT_MAX};
    assert(ctrl.SendDirect(more_colors, edge_indices));

    size_t sent_len = 0;
    const unsigned char* sent = hid_mock_get_last_sent(&sent_len);
    assert(sent != nullptr && sent_len == CIY_REPORT_0B_SIZE);
    assert(sent[0] == CIY_REPORT_0B_ID);
    // LED 0
    assert(sent[1 + 0] == 11);
    assert(sent[1 + CIY_LED_COUNT + 0] == 22);
    assert(sent[1 + 2 * CIY_LED_COUNT + 0] == 33);
    // LED 125
    assert(sent[1 + 125] == 44);
    assert(sent[1 + CIY_LED_COUNT + 125] == 55);
    assert(sent[1 + 2 * CIY_LED_COUNT + 125] == 66);
    // Alle übrigen LEDs (1..124) müssen 0 sein
    for (int led = 1; led < 125; led++) {
        assert(sent[1 + led] == 0);
        assert(sent[1 + CIY_LED_COUNT + led] == 0);
        assert(sent[1 + 2 * CIY_LED_COUNT + led] == 0);
    }

    // Fall B: led_indices größer als colors (std::min verhindert Vektor-Überleser unter ASan)
    std::vector<RGBColor> fewer_colors = {ToRGBColor(200, 100, 50)};
    std::vector<int> many_indices = {15, 16, 17, 18, INT_MAX, INT_MIN};
    assert(ctrl.SendDirect(fewer_colors, many_indices));
    sent = hid_mock_get_last_sent(&sent_len);
    assert(sent != nullptr && sent_len == CIY_REPORT_0B_SIZE);
    assert(sent[1 + 15] == 200 && sent[1 + CIY_LED_COUNT + 15] == 100 && sent[1 + 2 * CIY_LED_COUNT + 15] == 50);
    assert(sent[1 + 16] == 0);

    // Fall C: Leere Vektoren und abgebrochene HID-Reports in GetState / Detector
    std::vector<RGBColor> empty_colors;
    std::vector<int> empty_indices;
    assert(ctrl.SendDirect(empty_colors, empty_indices));

    const uint8_t truncated_4b[4] = {0x0A, 19, 0, 2};
    hid_mock_set_feature_report_response(truncated_4b, sizeof(truncated_4b));
    uint8_t gm = 0, gc = 0, gs = 0, gb = 0;
    assert(!ctrl.GetState(gm, gc, gs, gb));

    hid_mock_set_fail_get(true);
    assert(!ctrl.GetState(gm, gc, gs, gb));
    hid_mock_set_fail_get(false);

    std::cout << "PASSED\n";
}

int main()
{
    std::cout << "=== Starte OpenRGB Contribution Bundle Unit-Tests ===\n";
    test_detector_safe_handshake();
    test_matrix_and_dual_cell_enter();
    test_direct_mode_rgb_transfer();
    test_mode_switching();
    test_send_direct_bounds_and_null_guards();
    ResourceManager::get()->ClearControllers();
    std::cout << "=== Alle OpenRGB Unit-Tests erfolgreich bestanden! ===\n";
    return 0;
}
