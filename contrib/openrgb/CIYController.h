#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include "RGBController.h"

#if defined(__has_include)
  #if __has_include(<hidapi/hidapi.h>)
    #include <hidapi/hidapi.h>
  #elif __has_include("hidapi.h")
    #include "hidapi.h"
  #else
    #include <hidapi.h>
  #endif
#else
  #include <hidapi/hidapi.h>
#endif

// CIY X79 Hardware & Protocol Constants
#define CIY_VENDOR_ID          0x258A
#define CIY_PRODUCT_ID         0x0049
#define CIY_INTERFACE_NUM      1

#define CIY_LED_COUNT          126
#define CIY_PLANAR_RGB_SIZE    (3 * CIY_LED_COUNT) // 378 bytes

#define CIY_REPORT_0A_ID       0x0A
#define CIY_REPORT_0A_SIZE     42
#define CIY_REPORT_0B_ID       0x0B
#define CIY_REPORT_0B_SIZE     (1 + CIY_PLANAR_RGB_SIZE) // 379 bytes

#define CIY_CUSTOM_MODE        19
#define CIY_MAX_MODE           20
#define CIY_MAX_COLOR          10
#define CIY_MAX_SPEED          4
#define CIY_MAX_BRIGHTNESS     5

class CIYController
{
public:
    CIYController(hid_device* dev_handle, const char* path);
    ~CIYController();

    std::string GetLocation() const;
    std::string GetSerial() const;

    // Send per-LED colors in planar format (379-byte Report 0x0B)
    bool SendDirect(const std::vector<RGBColor>& colors, const std::vector<int>& led_indices);

    // Set Lighting State (42-byte Report 0x0A)
    bool SetMode(uint8_t mode, uint8_t color, uint8_t speed, uint8_t brightness);

    // Read current Lighting State (42-byte GET_FEATURE Report 0x0A)
    bool GetState(uint8_t &mode, uint8_t &color, uint8_t &speed, uint8_t &brightness);

private:
    hid_device*                             dev;
    std::string                             location;
    std::string                             serial;
    std::mutex                              bus_mutex;
    std::chrono::steady_clock::time_point   last_update;
    bool                                    has_updated = false;

    // Minimum delay between frames (~16 FPS) to prevent MCU buffer overflows
    static constexpr int                    FRAME_INTERVAL_MS = 60;
};
