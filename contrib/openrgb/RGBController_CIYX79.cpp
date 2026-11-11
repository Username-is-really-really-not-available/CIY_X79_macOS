#include "RGBController_CIYX79.h"
#include <map>

RGBController_CIYX79::RGBController_CIYX79(CIYController* controller_ptr)
    : controller(controller_ptr)
{
    name        = "CIY X79 RGB Keyboard";
    vendor      = "CIY";
    type        = "Keyboard";
    description = "CIY X79 68-Key / 65% Compact RGB Mechanical Keyboard (ISO-DE)";
    location    = controller ? controller->GetLocation() : "";
    serial      = controller ? controller->GetSerial() : "";

    // Mode 0: Direct (Benutzerdefiniert per-LED Matrix, sw_mode 19)
    mode direct_mode;
    direct_mode.name           = "Direct";
    direct_mode.value          = 19;
    direct_mode.flags          = MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_PER_LED_COLOR;
    direct_mode.color_mode     = MODE_COLORS_PER_LED;
    direct_mode.brightness_min = 0;
    direct_mode.brightness_max = CIY_MAX_BRIGHTNESS;
    direct_mode.brightness     = CIY_MAX_BRIGHTNESS;
    modes.push_back(direct_mode);

    // Mode 1: Static (sw_mode 15)
    mode static_mode;
    static_mode.name           = "Static";
    static_mode.value          = 15;
    static_mode.flags          = MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_MODE_SPECIFIC_COLOR;
    static_mode.color_mode     = MODE_COLORS_MODE_SPECIFIC;
    static_mode.colors.resize(1);
    static_mode.colors[0]      = ToRGBColor(255, 0, 0); // Rot
    static_mode.brightness_min = 0;
    static_mode.brightness_max = CIY_MAX_BRIGHTNESS;
    static_mode.brightness     = CIY_MAX_BRIGHTNESS;
    modes.push_back(static_mode);

    // Mode 2: Off (sw_mode 0)
    mode off_mode;
    off_mode.name              = "Off";
    off_mode.value             = 0;
    off_mode.flags             = 0;
    off_mode.color_mode        = MODE_COLORS_NONE;
    modes.push_back(off_mode);

    // Hardware Presets (1..14, 16..18, 20)
    struct PresetDef { int val; const char* name; };
    static const PresetDef presets[] = {
        {1,  "Wave"},
        {2,  "Breathing"},
        {3,  "Ripple"},
        {4,  "Raindrops"},
        {5,  "Neon Stream"},
        {6,  "Stars"},
        {7,  "Snake"},
        {8,  "Spectrum Cycle"},
        {9,  "Vortex"},
        {10, "Meteor"},
        {11, "Aurora"},
        {12, "Windmill"},
        {13, "Ripple Bloom"},
        {14, "Laser Trace"},
        {16, "Wavebreaker"},
        {17, "Glow"},
        {18, "Sparks"},
        {20, "Rainbow Wave"}
    };

    for (const auto& p : presets)
    {
        mode m;
        m.name           = p.name;
        m.value          = p.val;
        m.flags          = MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_HAS_RANDOM_COLOR;
        m.color_mode     = MODE_COLORS_RANDOM;
        m.speed_min      = 0;
        m.speed_max      = CIY_MAX_SPEED;
        m.speed          = 2;
        m.brightness_min = 0;
        m.brightness_max = CIY_MAX_BRIGHTNESS;
        m.brightness     = CIY_MAX_BRIGHTNESS;
        modes.push_back(m);
    }

    SetupZones();

    // Init state from controller if possible
    uint8_t d_mode = 19, d_color = 0, d_speed = 2, d_bright = 5;
    if (controller && controller->GetState(d_mode, d_color, d_speed, d_bright))
    {
        for (size_t i = 0; i < modes.size(); i++)
        {
            if (modes[i].value == d_mode)
            {
                active_mode = (int)i;
                modes[i].brightness = d_bright;
                if (modes[i].flags & MODE_FLAG_HAS_SPEED)
                {
                    modes[i].speed = d_speed;
                }
                break;
            }
        }
    }
}

RGBController_CIYX79::~RGBController_CIYX79()
{
    delete controller;
}

struct CIYKeyInfo {
    int matrix_slot;
    int led_index;
    const char* name;
};

static const CIYKeyInfo CIY_X79_ISO_DE_KEYS[69] = {
    {1, 2, "Esc"},
    {7, 8, "1"},
    {13, 14, "2"},
    {19, 20, "3"},
    {25, 26, "4"},
    {31, 32, "5"},
    {37, 38, "6"},
    {43, 44, "7"},
    {49, 50, "8"},
    {55, 56, "9"},
    {61, 62, "0"},
    {67, 68, "ß"},
    {73, 74, "´"},
    {79, 80, "⌫ Back"},
    {86, 87, "Entf"},
    {2, 3, "⇥ Tab"},
    {8, 9, "Q"},
    {14, 15, "W"},
    {20, 21, "E"},
    {26, 27, "R"},
    {32, 33, "T"},
    {38, 39, "Z"},
    {44, 45, "U"},
    {50, 51, "I"},
    {56, 57, "O"},
    {62, 63, "P"},
    {68, 69, "Ü"},
    {74, 75, "+ *"},
    {81, 82, "↵ Enter"},
    {87, 88, "Bild ⇡"},
    {3, 4, "⇪ Caps"},
    {9, 10, "A"},
    {15, 16, "S"},
    {21, 22, "D"},
    {27, 28, "F"},
    {33, 34, "G"},
    {39, 40, "H"},
    {45, 46, "J"},
    {51, 52, "K"},
    {57, 58, "L"},
    {63, 64, "Ö"},
    {69, 70, "Ä"},
    {75, 76, "# '"},
    {94, 95, "Bild ⇣"},
    {4, 5, "⇧ Shift"},
    {76, 77, "< >"},
    {10, 11, "Y"},
    {16, 17, "X"},
    {22, 23, "C"},
    {28, 29, "V"},
    {34, 35, "B"},
    {40, 41, "N"},
    {46, 47, "M"},
    {52, 53, ", ;"},
    {58, 59, ". :"},
    {64, 65, "- _"},
    {82, 83, "⇧ Shift"},
    {88, 89, "▲"},
    {91, 92, "Ende"},
    {5, 6, "Strg"},
    {11, 12, "Win"},
    {17, 18, "Alt"},
    {35, 36, "Leertaste (Space)"},
    {53, 54, "AltGr"},
    {77, 78, "Fn"},
    {80, 81, "Strg"},
    {83, 84, "◄"},
    {89, 90, "▼"},
    {95, 96, "►"}
};

void RGBController_CIYX79::SetupZones()
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
    zones.clear();
    leds.clear();
    colors.clear();
    led_hardware_indices.clear();
    std::map<int, unsigned int> slot_to_idx;

    for (size_t i = 0; i < 69; i++)
    {
        const CIYKeyInfo& k = CIY_X79_ISO_DE_KEYS[i];
        led l;
        l.name = std::string("Key: ") + k.name;
        l.value = (unsigned int)k.led_index;
        leds.push_back(l);
        colors.push_back(ToRGBColor(0, 0, 0));
        led_hardware_indices.push_back(k.led_index);
        slot_to_idx[k.matrix_slot] = (unsigned int)i;
    }

    zone matrix_zone;
    matrix_zone.name        = "Keyboard";
    matrix_zone.type        = ZONE_TYPE_MATRIX;
    matrix_zone.leds_min    = 69;
    matrix_zone.leds_max    = 69;
    matrix_zone.leds_count  = 69;

    matrix_zone.matrix_map  = new matrix_map_type();
    matrix_zone.matrix_map->height = 5;
    matrix_zone.matrix_map->width  = 16;
    matrix_zone.matrix_map->map    = new unsigned int[5 * 16];

    for (size_t i = 0; i < 5 * 16; i++)
    {
        matrix_zone.matrix_map->map[i] = 0xFFFFFFFF;
    }

    auto map_slot = [&](size_t row, size_t col, int slot) {
        auto it = slot_to_idx.find(slot);
        if (it != slot_to_idx.end()) {
            matrix_zone.matrix_map->map[row * 16 + col] = it->second;
        }
    };

    // Row 0: Esc, 1..0, ß, ´, Backspace, Del (Cols 0..14)
    static const int row0_slots[] = {1, 7, 13, 19, 25, 31, 37, 43, 49, 55, 61, 67, 73, 79, 86};
    for (size_t col = 0; col < sizeof(row0_slots)/sizeof(row0_slots[0]); col++)
    {
        map_slot(0, col, row0_slots[col]);
    }

    // Row 1: Tab, Q..P, Ü, +*, Enter (Dual-Cell Top), Bild Auf (Cols 0..14)
    static const int row1_slots[] = {2, 8, 14, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74, 81, 87};
    for (size_t col = 0; col < sizeof(row1_slots)/sizeof(row1_slots[0]); col++)
    {
        map_slot(1, col, row1_slots[col]);
    }

    // Row 2: Caps, A..L, Ö, Ä, #', Enter (Dual-Cell Bottom), Bild Ab (Cols 0..14)
    static const int row2_slots[] = {3, 9, 15, 21, 27, 33, 39, 45, 51, 57, 63, 69, 75, 81, 94};
    for (size_t col = 0; col < sizeof(row2_slots)/sizeof(row2_slots[0]); col++)
    {
        map_slot(2, col, row2_slots[col]);
    }

    // Row 3: Shift L, <>, Y..-, Shift R, Up, End (Cols 0..14)
    static const int row3_slots[] = {4, 76, 10, 16, 22, 28, 34, 40, 46, 52, 58, 64, 82, 88, 91};
    for (size_t col = 0; col < sizeof(row3_slots)/sizeof(row3_slots[0]); col++)
    {
        map_slot(3, col, row3_slots[col]);
    }

    // Row 4: Strg, Win, Alt, Space, AltGr, Fn, Strg, Left, Down, Right
    static const struct { int col; int slot; } row4_entries[] = {
        {0, 5}, {1, 11}, {2, 17}, {6, 35}, {10, 53}, {11, 77}, {12, 80}, {13, 83}, {14, 89}, {15, 95}
    };
    for (const auto& e : row4_entries)
    {
        map_slot(4, (size_t)e.col, e.slot);
    }

    zones.push_back(matrix_zone);
}

void RGBController_CIYX79::DeviceUpdateLEDs()
{
    if (!controller) return;
    if (active_mode < 0 || active_mode >= (int)modes.size()) return;
    if (modes[active_mode].value == 19)
    {
        controller->SendDirect(colors, led_hardware_indices);
    }
}

void RGBController_CIYX79::DeviceUpdateMode()
{
    if (!controller) return;
    if (active_mode < 0 || active_mode >= (int)modes.size()) return;
    const mode& m = modes[active_mode];
    if (m.value == 19)
    {
        DeviceUpdateLEDs();
        controller->SetMode(19, 0, 0, (uint8_t)m.brightness);
    }
    else if (m.value == 15)
    {
        uint8_t col = 0; // Rot default
        if (!m.colors.empty())
        {
            // Finde nächste Hardware-Farbe via euklidischer Farbabstand
            struct PaletteEntry { uint8_t r, g, b, idx; };
            static const PaletteEntry PALETTE[8] = {
                {255,   0,   0, 0}, // Rot
                {255, 128,   0, 1}, // Orange
                {255, 255,   0, 2}, // Gelb
                {  0, 255,   0, 3}, // Grün
                {  0, 255, 255, 4}, // Cyan
                {  0,   0, 255, 5}, // Blau
                {255,   0, 255, 6}, // Lila
                {255, 255, 255, 7}  // Weiß
            };

            RGBColor c = m.colors[0];
            int r = (int)RGBGetRValue(c);
            int g = (int)RGBGetGValue(c);
            int b = (int)RGBGetBValue(c);
            int best_dist = 1000000;
            for (const auto& pe : PALETTE)
            {
                int dr = r - (int)pe.r;
                int dg = g - (int)pe.g;
                int db = b - (int)pe.b;
                int dist = dr * dr + dg * dg + db * db;
                if (dist < best_dist)
                {
                    best_dist = dist;
                    col = pe.idx;
                }
            }
        }
        controller->SetMode(15, col, 0, (uint8_t)m.brightness);
    }
    else if (m.value == 0)
    {
        controller->SetMode(0, 0, 0, 0);
    }
    else
    {
        controller->SetMode((uint8_t)m.value, 0, (uint8_t)m.speed, (uint8_t)m.brightness);
    }
}
