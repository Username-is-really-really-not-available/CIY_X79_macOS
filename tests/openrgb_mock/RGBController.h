#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>

typedef unsigned int RGBColor;

#define ToRGBColor(r, g, b) (((RGBColor)(r)) | (((RGBColor)(g)) << 8) | (((RGBColor)(b)) << 16))
#define RGBGetRValue(rgb) ((rgb) & 0xFF)
#define RGBGetGValue(rgb) (((rgb) >> 8) & 0xFF)
#define RGBGetBValue(rgb) (((rgb) >> 16) & 0xFF)

enum
{
    ZONE_TYPE_SINGLE = 0,
    ZONE_TYPE_LINEAR,
    ZONE_TYPE_MATRIX
};

typedef unsigned int zone_type;

enum
{
    MODE_FLAG_HAS_SPEED                 = (1 << 0),
    MODE_FLAG_HAS_DIRECTION_LR          = (1 << 1),
    MODE_FLAG_HAS_DIRECTION_UD          = (1 << 2),
    MODE_FLAG_HAS_DIRECTION_HV          = (1 << 3),
    MODE_FLAG_HAS_BRIGHTNESS            = (1 << 4),
    MODE_FLAG_HAS_PER_LED_COLOR         = (1 << 5),
    MODE_FLAG_HAS_MODE_SPECIFIC_COLOR   = (1 << 6),
    MODE_FLAG_HAS_RANDOM_COLOR          = (1 << 7)
};

enum
{
    MODE_COLORS_NONE                    = 0,
    MODE_COLORS_PER_LED                 = 1,
    MODE_COLORS_MODE_SPECIFIC           = 2,
    MODE_COLORS_RANDOM                  = 3
};

struct matrix_map_type
{
    unsigned int height;
    unsigned int width;
    unsigned int *map;
};

struct zone
{
    std::string         name;
    zone_type           type;
    unsigned int        leds_min;
    unsigned int        leds_max;
    unsigned int        leds_count;
    matrix_map_type     *matrix_map;
};

struct led
{
    std::string         name;
    unsigned int        value;
};

struct mode
{
    std::string         name;
    int                 value;
    unsigned int        flags;
    unsigned int        speed_min;
    unsigned int        speed_max;
    unsigned int        color_mode;
    unsigned int        speed;
    unsigned int        brightness_min;
    unsigned int        brightness_max;
    unsigned int        brightness;
    unsigned int        direction;
    std::vector<RGBColor> colors;
};

class RGBController
{
public:
    std::string             name;
    std::string             vendor;
    std::string             type;
    std::string             description;
    std::string             version;
    std::string             location;
    std::string             serial;

    std::vector<zone>       zones;
    std::vector<led>        leds;
    std::vector<RGBColor>   colors;
    std::vector<mode>       modes;
    int                     active_mode = 0;

    virtual ~RGBController()
    {
        for (auto& z : zones)
        {
            if (z.matrix_map)
            {
                delete[] z.matrix_map->map;
                delete z.matrix_map;
                z.matrix_map = nullptr;
            }
        }
    }

    virtual void SetupZones() = 0;
    virtual void ResizeZone(int /*zone*/, int /*new_size*/) {}
    virtual void DeviceUpdateLEDs() = 0;
    virtual void UpdateZoneLEDs(int /*zone*/) { DeviceUpdateLEDs(); }
    virtual void UpdateSingleLED(int /*led*/) { DeviceUpdateLEDs(); }
    virtual void DeviceUpdateMode() = 0;
    virtual void KeepaliveThread() {}
};

class ResourceManager
{
private:
    std::vector<RGBController*> controllers;

public:
    static ResourceManager* get()
    {
        static ResourceManager inst;
        return &inst;
    }

    void RegisterRGBController(RGBController* controller)
    {
        controllers.push_back(controller);
    }

    const std::vector<RGBController*>& GetControllers() const
    {
        return controllers;
    }

    void ClearControllers()
    {
        for (auto c : controllers)
        {
            delete c;
        }
        controllers.clear();
    }
};
