#ifndef CIY_HID_H
#define CIY_HID_H

// Gemeinsames USB-Protokollmodul für CIY X79 (genutzt von ciy_ctl und KeyboardBackend).
// Reine C-/IOKit-Schicht, kennt weder Cocoa noch UI.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#define CIY_VENDOR_ID       0x258A
#define CIY_PRODUCT_ID      0x0049
#define CIY_LED_COUNT       126                 // LEDs pro Farbebene in Report 0x0B
#define CIY_PLANAR_RGB_SIZE (3 * CIY_LED_COUNT) // Planar R/G/B = 378 Bytes

#define CIY_MAX_MODE        20
#define CIY_MAX_COLOR       10
#define CIY_MAX_SPEED       4
#define CIY_MAX_BRIGHTNESS  5

typedef struct {
    IOUSBDeviceInterface **dev;
} CIYDevice;

typedef struct {
    uint8_t mode;       // 0..20
    uint8_t color;      // 0..10
    uint8_t speed;      // 0..4
    uint8_t brightness; // 0..5
} CIYLightingState;

/// Öffnet die erste gefundene CIY X79. Bei Fehler false und dev == NULL.
bool ciy_open(CIYDevice *d);
void ciy_close(CIYDevice *d);

/// Liest Report 0x0A. Ausgabeparameter dürfen NULL sein.
bool ciy_get_state(CIYDevice *d, CIYLightingState *out);

/// Sendet Report 0x0A. Werte außerhalb der Bereiche werden abgelehnt (false).
bool ciy_set_state(CIYDevice *d, CIYLightingState st);

/// Sendet Report 0x0B (genau CIY_PLANAR_RGB_SIZE Bytes: Rot-, Grün-, Blau-Ebene).
bool ciy_send_planar_rgb(CIYDevice *d, const uint8_t *rgb, size_t length);

/// Offizielle Reihenfolge: Report 0x0B, 50 ms Pause, danach Modus 19 (Customize mit Standardhelligkeit 5).
bool ciy_apply_custom_profile(CIYDevice *d, const uint8_t *rgb, size_t length);

/// Sendet Report 0x0B und setzt Modus 19 mit individueller Helligkeit (0..CIY_MAX_BRIGHTNESS).
bool ciy_apply_custom_profile_with_brightness(CIYDevice *d, const uint8_t *rgb, size_t length, uint8_t brightness);

/// Baut eine Planar-RGB-Matrix: optionale blaue Basis, `indices` (USB-Index = Slot + 1) in Rot/Grün.
void ciy_build_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], const int *indices, int count,
                       bool red, bool green, bool baseBlue);

/// Baut das kombinierte Profil: WASD (Indizes 15, 10, 16, 22) in Rot, Pfeile (89, 84, 90, 96) in Gelb, Rest optional in Blau.
void ciy_build_wasd_and_arrows_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], bool baseBlue);

/// Baut das Vollfarb-Profil (Alles Rot: alle LEDs 0xFF in Rot, 0x00 in Grün/Blau).
void ciy_build_all_red_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE]);

/// Baut ein Profil mit einheitlicher Vollfarbe (r, g, b) für alle LEDs.
void ciy_build_solid_color_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], uint8_t r, uint8_t g, uint8_t b);

/// Parst einen Hex-Farbstring (#RRGGBB, RRGGBB, #RGB oder RGB). Gibt false bei ungültigem Format zurück.
bool ciy_parse_hex_color(const char *str, uint8_t *r, uint8_t *g, uint8_t *b);

/// Parst ein JSON-Profil (Array oder Objekt mit "slots") und befüllt den Planar-Puffer.
/// target_profile (optional) wählt ein Profil nach Name oder 1-basiertem Index; NULL wählt das erste Profil.
/// Gibt true zurück, wenn mindestens eine Taste erfolgreich geparst wurde.
/// out_name (optional) nimmt den Profilnamen auf (max_name_len Zeichen).
/// out_slots_count (optional) nimmt die Anzahl der geparsten Tasten auf.
bool ciy_parse_profile_json(const char *json, const char *target_profile,
                            uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                            char *out_name, size_t max_name_len,
                            int *out_slots_count);

/// Liest eine JSON-Profildatei von der Festplatte und parst sie in den Planar-Puffer.
/// target_profile (optional) wählt ein Profil nach Name oder 1-basiertem Index; NULL wählt das erste Profil.
bool ciy_load_profile_file(const char *path, const char *target_profile,
                           uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                           char *out_name, size_t max_name_len,
                           int *out_slots_count);

#endif
