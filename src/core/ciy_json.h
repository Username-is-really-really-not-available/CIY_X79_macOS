#ifndef CIY_JSON_H
#define CIY_JSON_H

// Reiner ISO-C11 Zero-Allocation JSON- und Hex-Farbparser für CIY X79 Profile.
// Vollständig isoliert: keine OS-, IOKit-, CoreFoundation-, HIDAPI- oder POSIX-Abhängigkeiten.

#include "ciy_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Grund, warum ciy_read_text_file_with_result fehlgeschlagen ist (0 = kein Lesefehler).
/// Dient der GUI fuer lokalisierte Meldungen; error_message bleibt deutscher Diagnosetext.
typedef enum {
    CIYFileReadErrorNone = 0,
    CIYFileReadErrorNullPath,
    CIYFileReadErrorOpenFailed,
    CIYFileReadErrorSymlinkRejected,
    CIYFileReadErrorNotRegularFile,
    CIYFileReadErrorEmptyFile,
    CIYFileReadErrorTooLarge,
    CIYFileReadErrorStreamFailed,
    CIYFileReadErrorOutOfMemory,
    CIYFileReadErrorReadFailed
} CIYFileReadError;

typedef struct {
    bool is_valid;
    int profile_count;
    int total_keys_count;
    char error_field[64];
    char error_message[256];
    CIYFileReadError read_error;
} CIYValidationResult;

/// Parst einen Hex-Farbstring (#RRGGBB, RRGGBB, #RGB oder RGB). Gibt false bei ungültigem Format zurück.
bool ciy_parse_hex_color(const char *str, uint8_t *r, uint8_t *g, uint8_t *b);

/// Validiert ein Profil oder Profil-Array hardwarefrei gegen das JSON-Schema und Hardware-Grenzen.
/// Prüft Syntax, Datentypen, Slot-Bereiche (0..124), Farbformate (#RRGGBB/#RGB) und Helligkeit (0..5).
bool ciy_validate_profile_json(const char *json, CIYValidationResult *out_res);

/// Parst ein JSON-Profil (Array oder Objekt mit "slots") und befüllt den Planar-Puffer.
/// target_profile (optional) wählt ein Profil nach Name oder 1-basiertem Index; NULL wählt das erste Profil.
/// Gibt true zurück, wenn mindestens eine Taste erfolgreich geparst wurde.
/// out_name (optional) nimmt den Profilnamen auf (max_name_len Zeichen).
/// out_slots_count (optional) nimmt die Anzahl der geparsten Tasten auf.
bool ciy_parse_profile_json(const char *json, const char *target_profile,
                            uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                            char *out_name, size_t max_name_len,
                            int *out_slots_count);

/// Parst ein JSON-Profil inklusive Helligkeit (0..CIY_MAX_BRIGHTNESS) und befüllt den Planar-Puffer.
/// out_brightness (optional) nimmt die Helligkeit auf (Default: CIY_MAX_BRIGHTNESS).
bool ciy_parse_profile_json_with_brightness(const char *json, const char *target_profile,
                                            uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                                            char *out_name, size_t max_name_len,
                                            int *out_slots_count,
                                            uint8_t *out_brightness);

#ifdef __cplusplus
}
#endif

#endif // CIY_JSON_H
