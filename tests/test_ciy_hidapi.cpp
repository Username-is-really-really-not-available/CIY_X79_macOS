#include <cassert>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include "../src/core/ciy_hid.h"
#include "openrgb_mock/hidapi.h"

int main() {
    std::cout << "=== Starte Linux hidapi Backend Tests (ASan/UBSan) ===\n";

    // Unset CIY_TESTING und CIY_FAIL_ON_HARDWARE_IO, um echten Backend-Aufruf zu testen
    unsetenv("CIY_TESTING");
    unsetenv("CIY_FAIL_ON_HARDWARE_IO");

    hid_mock_reset();

    // 1. Gerät nicht gefunden (falsche PID)
    hid_mock_set_device(CIY_VENDOR_ID, 0x1234, CIY_INTERFACE_NUM, "/dev/hidraw1");
    CIYDevice dev1 = {nullptr};
    assert(!ciy_open(&dev1));
    assert(dev1.dev == nullptr);

    // 2. Falsches Interface (Interface 0 statt Interface 1)
    hid_mock_set_device(CIY_VENDOR_ID, CIY_PRODUCT_ID, 0, "/dev/hidraw0");
    CIYDevice dev2 = {nullptr};
    assert(!ciy_open(&dev2));
    assert(dev2.dev == nullptr);

    // 3. Erfolgreiches Öffnen auf Interface 1
    hid_mock_reset();
    CIYDevice dev = {nullptr};
    assert(ciy_open(&dev));
    assert(dev.dev != nullptr);

    // 4. ciy_get_state über hidapi
    uint8_t resp[] = {0x0A, 15, 3, 2, 4}; // Mode 15 (Static), Color 3 (Grün), Wire Speed 2, Brightness 4
    hid_mock_set_feature_report_response(resp, sizeof(resp));
    CIYLightingState st;
    assert(ciy_get_state(&dev, &st));
    assert(st.mode == 15);
    assert(st.color == 3);
    assert(st.speed == 2);
    assert(st.brightness == 4);

    // 5. ciy_set_state über hidapi
    CIYLightingState new_st = {1, 0, 3, 5}; // Wave
    assert(ciy_set_state(&dev, new_st));
    size_t last_len = 0;
    const unsigned char* last_sent = hid_mock_get_last_sent(&last_len);
    assert(last_sent != nullptr);
    assert(last_len == CIY_REPORT_0A_SIZE);
    assert(last_sent[0] == CIY_REPORT_0A_ID);
    assert(last_sent[1] == 1);
    assert(last_sent[4] == 5);

    // 6. ciy_send_planar_rgb über hidapi
    uint8_t rgb[CIY_PLANAR_RGB_SIZE] = {0};
    rgb[2] = 255; // Esc Red
    assert(ciy_send_planar_rgb(&dev, rgb, sizeof(rgb)));
    last_sent = hid_mock_get_last_sent(&last_len);
    assert(last_sent != nullptr);
    assert(last_len == CIY_REPORT_0B_SIZE);
    assert(last_sent[0] == CIY_REPORT_0B_ID);
    assert(last_sent[1 + 2] == 255);

    // 7. ciy_close
    ciy_close(&dev);
    assert(dev.dev == nullptr);

    std::cout << "Alle Linux hidapi Backend Tests erfolgreich bestanden!\n";
    return 0;
}
