#ifndef CIY_HID_H
#define CIY_HID_H

// Gemeinsames USB-Protokollmodul für CIY X79 (genutzt von ciy_ctl und KeyboardBackend).
// Reine C-/IOKit-Schicht, kennt weder Cocoa noch UI.
// Plattformunabhängige Paket-Enkodierung, Konstanten und Transferpläne liegen in ciy_protocol.h.

#include "ciy_protocol.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    IOUSBDeviceInterface **dev;
} CIYDevice;

/// Öffnet die erste gefundene CIY X79. Bei Fehler false und dev == NULL.
bool ciy_open(CIYDevice *d);
void ciy_close(CIYDevice *d);

/// Systemweiter Inter-Process-Lock auf den USB-Bus (verhindert Race Conditions zwischen GUI und CLI).
/// Reentrant innerhalb desselben Threads/Prozesses und kernel-basiert via flock().
bool ciy_bus_lock(void);
void ciy_bus_unlock(void);

/// Gibt den verwendeten Lockfile-Pfad zurück (gleiche Logik wie ciy_bus_lock).
/// Nützlich für Tests, damit die Produktions-Pfad-Logik nicht dupliziert werden muss.
const char *ciy_bus_lock_path(char *buf, size_t bufsize);

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

/// Cancelbarer Callback: true = Abbruch anfordern.
typedef bool (*CIYCancelCheck)(void *ctx);

/// Führt einen vorbereiteten Übertragungsplan auf einem echten oder gemockten CIYDevice aus.
bool ciy_execute_transfer_plan(CIYDevice *d, const CIYTransferPlan *plan,
                               CIYCancelCheck cancel_check, void *cancel_ctx);

/// Wie ciy_apply_custom_profile_with_brightness, prüft aber zwischen den Schritten
/// (nach Report 0x0B, während der 50-ms-Pause und vor Modus 19) cancel_check.
/// Bei Abbruch wird Modus 19 nicht mehr gesendet; der Bus-Lock wird sauber freigegeben.
bool ciy_apply_custom_profile_with_brightness_cancelable(
    CIYDevice *d, const uint8_t *rgb, size_t length, uint8_t brightness,
    CIYCancelCheck cancel_check, void *cancel_ctx);

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

typedef struct {
    bool is_valid;
    int profile_count;
    int total_keys_count;
    char error_field[64];
    char error_message[256];
} CIYValidationResult;

/// Validiert ein Profil oder Profil-Array hardwarefrei gegen das JSON-Schema und Hardware-Grenzen.
/// Prüft Syntax, Datentypen, Slot-Bereiche (0..124), Farbformate (#RRGGBB/#RGB) und Helligkeit (0..5).
bool ciy_validate_profile_json(const char *json, CIYValidationResult *out_res);

/// Liest eine Profildatei hardwarefrei ein und validiert ihren Inhalt.
bool ciy_validate_profile_file(const char *path, CIYValidationResult *out_res);

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

#ifdef __cplusplus
}
#endif

#endif
