#include "ciy_layout.h"

// 69 eindeutige logische Tasten der CIY X79 ISO-DE
static const CIYLogicalKey CIY_X79_ISO_DE_KEYS[69] = {
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

// 70 grafische Flächen der CIY X79 ISO-DE
// Beachte: Slot 81 (ISO Enter) besitzt 2 Flächen:
// - Index 28: Row 1, 1.5U ("↵ Enter")
// - Index 43: Row 2, 1.25U ("↵")
static const CIYKeySurface CIY_X79_ISO_DE_SURFACES[70] = {
    // Row 0 (15 Flächen)
    {1, "Esc", 1.0f, 0},
    {7, "1", 1.0f, 0},
    {13, "2", 1.0f, 0},
    {19, "3", 1.0f, 0},
    {25, "4", 1.0f, 0},
    {31, "5", 1.0f, 0},
    {37, "6", 1.0f, 0},
    {43, "7", 1.0f, 0},
    {49, "8", 1.0f, 0},
    {55, "9", 1.0f, 0},
    {61, "0", 1.0f, 0},
    {67, "ß", 1.0f, 0},
    {73, "´", 1.0f, 0},
    {79, "⌫ Back", 2.0f, 0},
    {86, "Entf", 1.0f, 0},

    // Row 1 (15 Flächen)
    {2, "⇥ Tab", 1.5f, 1},
    {8, "Q", 1.0f, 1},
    {14, "W", 1.0f, 1},
    {20, "E", 1.0f, 1},
    {26, "R", 1.0f, 1},
    {32, "T", 1.0f, 1},
    {38, "Z", 1.0f, 1},
    {44, "U", 1.0f, 1},
    {50, "I", 1.0f, 1},
    {56, "O", 1.0f, 1},
    {62, "P", 1.0f, 1},
    {68, "Ü", 1.0f, 1},
    {74, "+ *", 1.0f, 1},
    {81, "↵ Enter", 1.5f, 1},
    {87, "Bild ⇡", 1.0f, 1},

    // Row 2 (15 Flächen)
    {3, "⇪ Caps", 1.75f, 2},
    {9, "A", 1.0f, 2},
    {15, "S", 1.0f, 2},
    {21, "D", 1.0f, 2},
    {27, "F", 1.0f, 2},
    {33, "G", 1.0f, 2},
    {39, "H", 1.0f, 2},
    {45, "J", 1.0f, 2},
    {51, "K", 1.0f, 2},
    {57, "L", 1.0f, 2},
    {63, "Ö", 1.0f, 2},
    {69, "Ä", 1.0f, 2},
    {75, "# '", 1.0f, 2},
    {81, "↵", 1.25f, 2},
    {94, "Bild ⇣", 1.0f, 2},

    // Row 3 (15 Flächen)
    {4, "⇧ Shift", 1.25f, 3},
    {76, "< >", 1.0f, 3},
    {10, "Y", 1.0f, 3},
    {16, "X", 1.0f, 3},
    {22, "C", 1.0f, 3},
    {28, "V", 1.0f, 3},
    {34, "B", 1.0f, 3},
    {40, "N", 1.0f, 3},
    {46, "M", 1.0f, 3},
    {52, ", ;", 1.0f, 3},
    {58, ". :", 1.0f, 3},
    {64, "- _", 1.0f, 3},
    {82, "⇧ Shift", 1.75f, 3},
    {88, "▲", 1.0f, 3},
    {91, "Ende", 1.0f, 3},

    // Row 4 (10 Flächen)
    {5, "Strg", 1.25f, 4},
    {11, "Win", 1.25f, 4},
    {17, "Alt", 1.25f, 4},
    {35, "Leertaste (Space)", 6.25f, 4},
    {53, "AltGr", 1.25f, 4},
    {77, "Fn", 1.0f, 4},
    {80, "Strg", 1.0f, 4},
    {83, "◄", 1.0f, 4},
    {89, "▼", 1.0f, 4},
    {95, "►", 1.0f, 4}
};

static const CIYKeyboardLayout CIY_X79_ISO_DE_LAYOUT = {
    .layout_id = "ciy_x79_iso_de",
    .display_name = "CIY X79 (ISO-DE)",
    .key_count = sizeof(CIY_X79_ISO_DE_KEYS) / sizeof(CIY_X79_ISO_DE_KEYS[0]),
    .keys = CIY_X79_ISO_DE_KEYS,
    .surface_count = sizeof(CIY_X79_ISO_DE_SURFACES) / sizeof(CIY_X79_ISO_DE_SURFACES[0]),
    .surfaces = CIY_X79_ISO_DE_SURFACES
};

const CIYKeyboardLayout *ciy_layout_x79_iso_de(void) {
    return &CIY_X79_ISO_DE_LAYOUT;
}

const CIYLogicalKey *ciy_layout_find_key_by_slot(const CIYKeyboardLayout *layout, int matrix_slot) {
    if (!layout || !layout->keys) return NULL;
    for (size_t i = 0; i < layout->key_count; i++) {
        if (layout->keys[i].matrix_slot == matrix_slot) {
            return &layout->keys[i];
        }
    }
    return NULL;
}

const CIYLogicalKey *ciy_layout_find_key_by_name(const CIYKeyboardLayout *layout, const char *name) {
    if (!layout || !layout->keys || !name) return NULL;
    for (size_t i = 0; i < layout->key_count; i++) {
        if (strcmp(layout->keys[i].name, name) == 0) {
            return &layout->keys[i];
        }
    }
    return NULL;
}
