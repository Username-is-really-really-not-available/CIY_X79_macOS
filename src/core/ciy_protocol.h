#ifndef CIY_PROTOCOL_H
#define CIY_PROTOCOL_H

// Plattformunabhängiger Pure-C11/C++ Protokoll-Kern für die CIY X79 (VID 0x258A, PID 0x0049).
// Frei von OS-Abhängigkeiten (kein IOKit, kein CoreFoundation) – direkt nutzbar in
// Drittprojekten wie OpenRGB, Linux-Tools oder Embedded-Controllern.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CIY_INLINE static inline __attribute__((unused))
#else
#define CIY_INLINE static inline
#endif

#define CIY_VENDOR_ID          0x258A
#define CIY_PRODUCT_ID         0x0049
#define CIY_INTERFACE_NUM      1

#define CIY_LED_COUNT          126                        // LEDs pro Farbebene in Report 0x0B
#define CIY_PLANAR_RGB_SIZE    (3 * CIY_LED_COUNT)        // Planar R/G/B = 378 Bytes
#define CIY_MAX_MATRIX_SLOT    124                        // Höchster gültiger Matrix-Slot (0..124)

#define CIY_REPORT_0A_ID       0x0A
#define CIY_REPORT_0A_SIZE     42
#define CIY_REPORT_0A_MIN_LEN  5

#define CIY_REPORT_0B_ID       0x0B
#define CIY_REPORT_0B_SIZE     (1 + CIY_PLANAR_RGB_SIZE)  // 379 Bytes (1 Byte ID + 378 Bytes RGB)

#define CIY_CUSTOM_MODE        19
#define CIY_CUSTOM_DELAY_MS    50

#define CIY_MAX_MODE           20
#define CIY_MAX_COLOR          10
#define CIY_MAX_SPEED          4
#define CIY_MAX_BRIGHTNESS     5
#define CIY_JSON_MAX_STRLEN    4096

typedef struct {
    uint8_t mode;       // 0..20
    uint8_t color;      // 0..10
    uint8_t speed;      // 0..4 (Logisch: 0 = langsam, 4 = schnell; im Drahtprotokoll invertiert: 4 - speed)
    uint8_t brightness; // 0..5
} CIYLightingState;

typedef enum {
    CIY_PLAN_STEP_REPORT,
    CIY_PLAN_STEP_DELAY
} CIYPlanStepType;

typedef struct {
    CIYPlanStepType type;
    uint8_t report_id;                     // 0x0B oder 0x0A
    uint16_t length;                       // z. B. 379 oder 42 Bytes
    uint8_t data[CIY_REPORT_0B_SIZE];      // Puffer für Report-Payload
    uint32_t delay_ms;                     // z. B. 50 ms
    uint8_t mode;                          // Für Report 0x0A: Modus (0..20)
    uint8_t color;                         // Für Report 0x0A: Farbe (0..10)
    uint8_t speed;                         // Für Report 0x0A: Tempo (0..4)
    uint8_t brightness;                    // Für Report 0x0A: Helligkeit (0..5)
    char description[64];
} CIYPlanStep;

typedef struct {
    char profile_name[64];
    int keys_count;
    uint8_t brightness;
    size_t step_count;
    CIYPlanStep steps[3];
} CIYTransferPlan;

/// Prüft, ob alle Felder eines Beleuchtungsstatus innerhalb der Hardware-Spezifikation liegen.
CIY_INLINE bool ciy_protocol_validate_state(CIYLightingState st) {
    return (st.mode <= CIY_MAX_MODE &&
            st.color <= CIY_MAX_COLOR &&
            st.speed <= CIY_MAX_SPEED &&
            st.brightness <= CIY_MAX_BRIGHTNESS);
}

/// Kodiert einen Beleuchtungsstatus in einen 42-Byte Report 0x0A.
/// Lehnt Werte außerhalb des gültigen Bereichs strikt mit false ab.
CIY_INLINE bool ciy_protocol_encode_0a(CIYLightingState st, uint8_t out_buf[CIY_REPORT_0A_SIZE]) {
    if (!out_buf || !ciy_protocol_validate_state(st)) {
        return false;
    }
    memset(out_buf, 0, CIY_REPORT_0A_SIZE);
    out_buf[0] = CIY_REPORT_0A_ID;
    out_buf[1] = st.mode;
    out_buf[2] = st.color;
    out_buf[3] = (uint8_t)(CIY_MAX_SPEED - st.speed);
    out_buf[4] = st.brightness;
    return true;
}

/// Dekodiert einen empfangenen Report 0x0A (mindestens 5 Bytes) in CIYLightingState.
/// Clampt Tempo und Helligkeit defensiv auf die gültigen Hardware-Grenzen.
/// out_st darf NULL sein (reine Formatprüfung).
CIY_INLINE bool ciy_protocol_decode_0a(const uint8_t *in_buf, size_t len, CIYLightingState *out_st) {
    if (!in_buf || len < CIY_REPORT_0A_MIN_LEN || in_buf[0] != CIY_REPORT_0A_ID) {
        return false;
    }
    if (out_st) {
        out_st->mode = in_buf[1];
        out_st->color = in_buf[2];
        out_st->speed = (in_buf[3] <= CIY_MAX_SPEED) ? (uint8_t)(CIY_MAX_SPEED - in_buf[3]) : 0;
        out_st->brightness = (in_buf[4] <= CIY_MAX_BRIGHTNESS) ? in_buf[4] : CIY_MAX_BRIGHTNESS;
    }
    return true;
}

/// Kodiert einen 378-Byte Planar-RGB-Puffer in einen 379-Byte Report 0x0B.
CIY_INLINE bool ciy_protocol_encode_0b(const uint8_t *planar_rgb, size_t rgb_len, uint8_t out_buf[CIY_REPORT_0B_SIZE]) {
    if (!planar_rgb || !out_buf || rgb_len != CIY_PLANAR_RGB_SIZE) {
        return false;
    }
    out_buf[0] = CIY_REPORT_0B_ID;
    memcpy(&out_buf[1], planar_rgb, CIY_PLANAR_RGB_SIZE);
    return true;
}

/// Berechnet den exakten Byte-Offset für den Rot-Kanal eines LED-Index (0..125) im 379-Byte Report 0x0B.
/// Gibt -1 bei ungültigem led_index zurück.
CIY_INLINE int ciy_protocol_report_0b_r_offset(int led_index) {
    if (led_index < 0 || led_index >= CIY_LED_COUNT) return -1;
    return 1 + led_index;
}

/// Berechnet den exakten Byte-Offset für den Grün-Kanal eines LED-Index (0..125) im 379-Byte Report 0x0B.
/// Gibt -1 bei ungültigem led_index zurück.
CIY_INLINE int ciy_protocol_report_0b_g_offset(int led_index) {
    if (led_index < 0 || led_index >= CIY_LED_COUNT) return -1;
    return 1 + CIY_LED_COUNT + led_index;
}

/// Berechnet den exakten Byte-Offset für den Blau-Kanal eines LED-Index (0..125) im 379-Byte Report 0x0B.
/// Gibt -1 bei ungültigem led_index zurück.
CIY_INLINE int ciy_protocol_report_0b_b_offset(int led_index) {
    if (led_index < 0 || led_index >= CIY_LED_COUNT) return -1;
    return 1 + (2 * CIY_LED_COUNT) + led_index;
}

/// Baut einen hardwarefreien Übertragungsplan für ein Custom-RGB-Profil auf.
/// Enthaltene Schritte:
/// 1. Report 0x0B (379 Bytes Planar RGB)
/// 2. 50 ms Pause
/// 3. Report 0x0A (42 Bytes, Modus 19, gewählte Helligkeit)
CIY_INLINE bool ciy_build_transfer_plan(CIYTransferPlan *out_plan,
                                           const uint8_t *rgb, size_t length,
                                           uint8_t brightness,
                                           const char *profile_name, int keys_count) {
    if (!out_plan || !rgb || length != CIY_PLANAR_RGB_SIZE) return false;
    memset(out_plan, 0, sizeof(*out_plan));

    if (profile_name && profile_name[0] != '\0') {
        strncpy(out_plan->profile_name, profile_name, sizeof(out_plan->profile_name) - 1);
    }
    out_plan->keys_count = keys_count;
    out_plan->brightness = (brightness <= CIY_MAX_BRIGHTNESS) ? brightness : CIY_MAX_BRIGHTNESS;
    out_plan->step_count = 3;

    // Schritt 1: Report 0x0B (379 Bytes planar RGB: 1 Byte 0x0B + 378 Bytes R/G/B)
    out_plan->steps[0].type = CIY_PLAN_STEP_REPORT;
    out_plan->steps[0].report_id = CIY_REPORT_0B_ID;
    out_plan->steps[0].length = CIY_REPORT_0B_SIZE;
    if (!ciy_protocol_encode_0b(rgb, length, out_plan->steps[0].data)) {
        return false;
    }
    snprintf(out_plan->steps[0].description, sizeof(out_plan->steps[0].description),
             "Report 0x0B (379 bytes)");

    // Schritt 2: 50 ms Pause
    out_plan->steps[1].type = CIY_PLAN_STEP_DELAY;
    out_plan->steps[1].delay_ms = CIY_CUSTOM_DELAY_MS;
    snprintf(out_plan->steps[1].description, sizeof(out_plan->steps[1].description),
             "wait %u ms", (unsigned)CIY_CUSTOM_DELAY_MS);

    // Schritt 3: Report 0x0A (42 Bytes, Modus 19, gewählte Helligkeit)
    CIYLightingState custom_st = {
        .mode = CIY_CUSTOM_MODE,
        .color = 0,
        .speed = 0,
        .brightness = out_plan->brightness
    };
    out_plan->steps[2].type = CIY_PLAN_STEP_REPORT;
    out_plan->steps[2].report_id = CIY_REPORT_0A_ID;
    out_plan->steps[2].length = CIY_REPORT_0A_SIZE;
    out_plan->steps[2].mode = custom_st.mode;
    out_plan->steps[2].color = custom_st.color;
    out_plan->steps[2].speed = custom_st.speed;
    out_plan->steps[2].brightness = custom_st.brightness;
    if (!ciy_protocol_encode_0a(custom_st, out_plan->steps[2].data)) {
        return false;
    }
    snprintf(out_plan->steps[2].description, sizeof(out_plan->steps[2].description),
             "Report 0x0A (42 bytes, mode 19)");

    return true;
}

/// Baut einen hardwarefreien 1-Schritt-Übertragungsplan für eine globale Statusänderung (Report 0x0A).
CIY_INLINE bool ciy_build_state_transfer_plan(CIYTransferPlan *out_plan, CIYLightingState st) {
    if (!out_plan || !ciy_protocol_validate_state(st)) return false;
    memset(out_plan, 0, sizeof(*out_plan));

    snprintf(out_plan->profile_name, sizeof(out_plan->profile_name), "Mode %u", st.mode);
    out_plan->keys_count = 0;
    out_plan->brightness = st.brightness;
    out_plan->step_count = 1;

    out_plan->steps[0].type = CIY_PLAN_STEP_REPORT;
    out_plan->steps[0].report_id = CIY_REPORT_0A_ID;
    out_plan->steps[0].length = CIY_REPORT_0A_SIZE;
    out_plan->steps[0].mode = st.mode;
    out_plan->steps[0].color = st.color;
    out_plan->steps[0].speed = st.speed;
    out_plan->steps[0].brightness = st.brightness;
    if (!ciy_protocol_encode_0a(st, out_plan->steps[0].data)) {
        return false;
    }
    snprintf(out_plan->steps[0].description, sizeof(out_plan->steps[0].description),
             "Report 0x0A (42 bytes, mode %u, color %u, speed %u, brightness %u)",
             st.mode, st.color, st.speed, st.brightness);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // CIY_PROTOCOL_H
