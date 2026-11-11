#include "CIYController.h"
#include <thread>
#include <cstring>
#include <algorithm>

CIYController::CIYController(hid_device* dev_handle, const char* path)
    : dev(dev_handle),
      location(path ? path : ""),
      has_updated(false)
{
}

CIYController::~CIYController()
{
    std::lock_guard<std::mutex> lock(bus_mutex);
    if (dev)
    {
        hid_close(dev);
        dev = nullptr;
    }
}

std::string CIYController::GetLocation() const
{
    return location;
}

std::string CIYController::GetSerial() const
{
    return serial;
}

bool CIYController::SendDirect(const std::vector<RGBColor>& colors, const std::vector<int>& led_indices)
{
    std::lock_guard<std::mutex> lock(bus_mutex);
    if (!dev) return false;

    // Frame-Rate Limiter: Mindestabstand 60 ms (~16 FPS) gegen Pufferüberläufe
    auto now = std::chrono::steady_clock::now();
    if (has_updated)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
        if (elapsed < FRAME_INTERVAL_MS)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_INTERVAL_MS - elapsed));
        }
    }

    // Planaren 378-Byte-RGB-Puffer befüllen
    uint8_t planar[CIY_PLANAR_RGB_SIZE] = {0};
    size_t count = std::min(colors.size(), led_indices.size());
    for (size_t i = 0; i < count; i++)
    {
        int led = led_indices[i];
        if (led >= 0 && led < CIY_LED_COUNT)
        {
            RGBColor c = colors[i];
            planar[led] = (uint8_t)RGBGetRValue(c);
            planar[CIY_LED_COUNT + led] = (uint8_t)RGBGetGValue(c);
            planar[2 * CIY_LED_COUNT + led] = (uint8_t)RGBGetBValue(c);
        }
    }

    uint8_t buf0b[CIY_REPORT_0B_SIZE];
    buf0b[0] = CIY_REPORT_0B_ID;
    memcpy(&buf0b[1], planar, CIY_PLANAR_RGB_SIZE);

    int res = hid_send_feature_report(dev, buf0b, sizeof(buf0b));
    has_updated = true;
    last_update = std::chrono::steady_clock::now();
    return res >= 0;
}

bool CIYController::SetMode(uint8_t mode, uint8_t color, uint8_t speed, uint8_t brightness)
{
    std::lock_guard<std::mutex> lock(bus_mutex);
    if (!dev) return false;

    auto now = std::chrono::steady_clock::now();
    if (has_updated)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
        if (elapsed < FRAME_INTERVAL_MS)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_INTERVAL_MS - elapsed));
        }
    }

    uint8_t buf0a[CIY_REPORT_0A_SIZE];
    memset(buf0a, 0, sizeof(buf0a));
    buf0a[0] = CIY_REPORT_0A_ID;
    buf0a[1] = (mode <= CIY_MAX_MODE) ? mode : 0;
    buf0a[2] = (color <= CIY_MAX_COLOR) ? color : 0;
    buf0a[3] = (speed <= CIY_MAX_SPEED) ? (uint8_t)(CIY_MAX_SPEED - speed) : 2;
    buf0a[4] = (brightness <= CIY_MAX_BRIGHTNESS) ? brightness : CIY_MAX_BRIGHTNESS;

    int res = hid_send_feature_report(dev, buf0a, sizeof(buf0a));
    has_updated = true;
    last_update = std::chrono::steady_clock::now();
    return res >= 0;
}

bool CIYController::GetState(uint8_t &mode, uint8_t &color, uint8_t &speed, uint8_t &brightness)
{
    mode = 0;
    color = 0;
    speed = 0;
    brightness = 0;

    std::lock_guard<std::mutex> lock(bus_mutex);
    if (!dev) return false;

    uint8_t buf0a[CIY_REPORT_0A_SIZE] = {CIY_REPORT_0A_ID};
    int res = hid_get_feature_report(dev, buf0a, sizeof(buf0a));
    if (res < 5 || buf0a[0] != CIY_REPORT_0A_ID)
    {
        return false;
    }

    mode = (buf0a[1] <= CIY_MAX_MODE) ? buf0a[1] : 0;
    color = (buf0a[2] <= CIY_MAX_COLOR) ? buf0a[2] : 0;
    speed = (buf0a[3] <= CIY_MAX_SPEED) ? (uint8_t)(CIY_MAX_SPEED - buf0a[3]) : 0;
    brightness = (buf0a[4] <= CIY_MAX_BRIGHTNESS) ? buf0a[4] : CIY_MAX_BRIGHTNESS;
    return true;
}
