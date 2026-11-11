#ifndef CIY_BATTERY_H
#define CIY_BATTERY_H

// CIY X79 Multi-Source & Cross-Platform Akku-Engine (C11 Header-Only)
// Unterstützt macOS (IOPowerSources / IOPS), Linux (/sys/class/power_supply)
// sowie Test-Mocks via CIY_BATTERY_MOCK_PERCENT und CIY_BATTERY_MOCK_CHARGING.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ciy_protocol.h"

#if defined(__APPLE__) && !defined(CIY_NO_IOKIT)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif

#if defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Akku-Statusfarben (Hardware sRGB)
#define CIY_BATTERY_COLOR_RED_R       0xFF
#define CIY_BATTERY_COLOR_RED_G       0x17
#define CIY_BATTERY_COLOR_RED_B       0x44    // #FF1744 (Warn-Rot <= 20%)

#define CIY_BATTERY_COLOR_YELLOW_R    0xFF
#define CIY_BATTERY_COLOR_YELLOW_G    0xD6
#define CIY_BATTERY_COLOR_YELLOW_B    0x00    // #FFD600 (Bernstein / Gelb 21..50%)

#define CIY_BATTERY_COLOR_GREEN_R     0x00
#define CIY_BATTERY_COLOR_GREEN_G     0xE6
#define CIY_BATTERY_COLOR_GREEN_B     0x76    // #00E676 (Grün > 50%)

#define CIY_BATTERY_COLOR_CYAN_R      0x00
#define CIY_BATTERY_COLOR_CYAN_G      0xE5
#define CIY_BATTERY_COLOR_CYAN_B      0xFF    // #00E5FF (Charging Electric Cyan)

typedef enum {
    CIY_BATTERY_SOURCE_INTERNAL = 0,
    CIY_BATTERY_SOURCE_PERIPHERAL,
    CIY_BATTERY_SOURCE_UPS,
    CIY_BATTERY_SOURCE_ALL
} CIYBatterySourceType;

typedef struct {
    char name[64];
    CIYBatterySourceType type;
    int percent;          // 0..100 (-1 falls unbekannt / nicht vorhanden)
    bool is_charging;     // true wenn Netzteil aktiv / am Laden
    bool is_present;      // true wenn Akku physisch erkannt
} CIYBatteryInfo;

// Vordefinierte Standard-Zielzonen
static const int CIY_BATTERY_TARGET_1_0_SLOTS[10] = {7, 13, 19, 25, 31, 37, 43, 49, 55, 61};
static const int CIY_BATTERY_TARGET_SPACE_SLOTS[1] = {35};
static const int CIY_BATTERY_TARGET_NAV_SLOTS[4] = {91, 94, 87, 86}; // Ende -> Bild ⇣ -> Bild ⇡ -> Entf

/// Ermittelt die passende RGB-Farbe für einen Akkuladestand und Ladestatus.
CIY_INLINE void ciy_battery_get_color(int percent, bool is_charging,
                                     uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    if (!out_r || !out_g || !out_b) return;

    if (is_charging) {
        *out_r = CIY_BATTERY_COLOR_CYAN_R;
        *out_g = CIY_BATTERY_COLOR_CYAN_G;
        *out_b = CIY_BATTERY_COLOR_CYAN_B;
    } else if (percent > 50) {
        *out_r = CIY_BATTERY_COLOR_GREEN_R;
        *out_g = CIY_BATTERY_COLOR_GREEN_G;
        *out_b = CIY_BATTERY_COLOR_GREEN_B;
    } else if (percent >= 21) {
        *out_r = CIY_BATTERY_COLOR_YELLOW_R;
        *out_g = CIY_BATTERY_COLOR_YELLOW_G;
        *out_b = CIY_BATTERY_COLOR_YELLOW_B;
    } else {
        *out_r = CIY_BATTERY_COLOR_RED_R;
        *out_g = CIY_BATTERY_COLOR_RED_G;
        *out_b = CIY_BATTERY_COLOR_RED_B;
    }
}

/// Prüft, ob Test-Mocks über Umgebungsvariablen aktiv sind.
CIY_INLINE bool ciy_battery_check_mocks(CIYBatteryInfo *out_info) {
#if defined(CIY_ENABLE_TEST_HOOKS)
    const char *mock_p = getenv("CIY_BATTERY_MOCK_PERCENT");
    if (!mock_p || mock_p[0] == '\0') {
        return false;
    }

    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
        const char *mock_name = getenv("CIY_BATTERY_MOCK_NAME");
        if (mock_name && mock_name[0] != '\0') {
            strncpy(out_info->name, mock_name, sizeof(out_info->name) - 1);
        } else {
            strncpy(out_info->name, "Mock Battery", sizeof(out_info->name) - 1);
        }
        out_info->type = CIY_BATTERY_SOURCE_INTERNAL;
        int p = atoi(mock_p);
        if (p < -1) p = -1;
        if (p > 100) p = 100;
        out_info->percent = p;

        const char *mock_c = getenv("CIY_BATTERY_MOCK_CHARGING");
        out_info->is_charging = (mock_c && (strcmp(mock_c, "1") == 0 ||
                                            strcasecmp(mock_c, "true") == 0 ||
                                            strcasecmp(mock_c, "yes") == 0));
        out_info->is_present = true;
    }
    return true;
#else
    (void)out_info;
    return false;
#endif
}

/// Listet alle verfügbaren Stromquellen auf (macOS, Linux oder Mock).
CIY_INLINE size_t ciy_battery_list_sources(CIYBatteryInfo *out_sources, size_t max_count) {
    if (!out_sources || max_count == 0) return 0;

    CIYBatteryInfo mock_info;
    if (ciy_battery_check_mocks(&mock_info)) {
        out_sources[0] = mock_info;
        return 1;
    }

    size_t found = 0;

#if defined(__APPLE__) && !defined(CIY_NO_IOKIT)
    CFTypeRef infoBlob = IOPSCopyPowerSourcesInfo();
    if (infoBlob) {
        CFArrayRef sourcesList = IOPSCopyPowerSourcesList(infoBlob);
        if (sourcesList) {
            CFIndex count = CFArrayGetCount(sourcesList);
            for (CFIndex i = 0; i < count && found < max_count; i++) {
                CFTypeRef ps = CFArrayGetValueAtIndex(sourcesList, i);
                CFDictionaryRef desc = IOPSGetPowerSourceDescription(infoBlob, ps);
                if (!desc) continue;

                CIYBatteryInfo info;
                memset(&info, 0, sizeof(info));

                // Name
                CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSNameKey));
                if (nameRef && CFGetTypeID(nameRef) == CFStringGetTypeID()) {
                    CFStringGetCString(nameRef, info.name, sizeof(info.name), kCFStringEncodingUTF8);
                } else {
                    snprintf(info.name, sizeof(info.name), "PowerSource_%ld", (long)i);
                }

                // Type
                CFStringRef typeRef = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSTypeKey));
                info.type = CIY_BATTERY_SOURCE_PERIPHERAL;
                if (typeRef && CFGetTypeID(typeRef) == CFStringGetTypeID()) {
                    if (CFStringCompare(typeRef, CFSTR(kIOPSInternalBatteryType), 0) == kCFCompareEqualTo) {
                        info.type = CIY_BATTERY_SOURCE_INTERNAL;
                    } else if (CFStringCompare(typeRef, CFSTR(kIOPSUPSType), 0) == kCFCompareEqualTo) {
                        info.type = CIY_BATTERY_SOURCE_UPS;
                    }
                }

                // Presence
                CFBooleanRef presentRef = (CFBooleanRef)CFDictionaryGetValue(desc, CFSTR(kIOPSIsPresentKey));
                info.is_present = (presentRef && CFGetTypeID(presentRef) == CFBooleanGetTypeID()) ?
                                  CFBooleanGetValue(presentRef) : true;

                // Capacities & Percentage
                int curCap = -1, maxCap = -1;
                CFNumberRef curRef = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSCurrentCapacityKey));
                if (curRef && CFGetTypeID(curRef) == CFNumberGetTypeID()) {
                    CFNumberGetValue(curRef, kCFNumberIntType, &curCap);
                }
                CFNumberRef maxRef = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSMaxCapacityKey));
                if (maxRef && CFGetTypeID(maxRef) == CFNumberGetTypeID()) {
                    CFNumberGetValue(maxRef, kCFNumberIntType, &maxCap);
                }

        if (curCap >= 0 && maxCap > 0) {
            int64_t raw_p = ((int64_t)curCap * 100) / (int64_t)maxCap;
            if (raw_p < 0) raw_p = 0;
            if (raw_p > 100) raw_p = 100;
            info.percent = (int)raw_p;
        } else {
            info.percent = -1;
        }

                // Charging status
                CFBooleanRef chargingRef = (CFBooleanRef)CFDictionaryGetValue(desc, CFSTR(kIOPSIsChargingKey));
                bool isCharging = (chargingRef && CFGetTypeID(chargingRef) == CFBooleanGetTypeID()) ?
                                  CFBooleanGetValue(chargingRef) : false;

                CFStringRef stateRef = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSPowerSourceStateKey));
                if (stateRef && CFGetTypeID(stateRef) == CFStringGetTypeID()) {
                    if (CFStringCompare(stateRef, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) {
                        // Am Netzteil: gilt als geladen/ladend
                        isCharging = true;
                    }
                }
                info.is_charging = isCharging;

                out_sources[found++] = info;
            }
            CFRelease(sourcesList);
        }
        CFRelease(infoBlob);
    }

    // Bluetooth- und IOHID-Peripheriegeräte (z. B. Magic Mouse, Magic Trackpad) abfragen
    const char *service_classes[] = {"AppleDeviceManagementHIDEventService", "IOHIDDevice"};
    for (size_t sc = 0; sc < 2 && found < max_count; sc++) {
        CFMutableDictionaryRef matching = IOServiceMatching(service_classes[sc]);
        if (!matching) continue;
        io_iterator_t iter = IO_OBJECT_NULL;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) == KERN_SUCCESS && iter != IO_OBJECT_NULL) {
            io_service_t service;
            while (found < max_count && (service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
                CFTypeRef batRef = IORegistryEntryCreateCFProperty(service, CFSTR("BatteryPercent"), kCFAllocatorDefault, 0);
                if (!batRef) {
                    batRef = IORegistryEntryCreateCFProperty(service, CFSTR("BatteryPercentCase"), kCFAllocatorDefault, 0);
                }
                if (!batRef) {
                    batRef = IORegistryEntryCreateCFProperty(service, CFSTR("BatteryCurrentCapacity"), kCFAllocatorDefault, 0);
                }

                if (batRef) {
                    int pct = -1;
                    if (CFGetTypeID(batRef) == CFNumberGetTypeID()) {
                        CFNumberGetValue((CFNumberRef)batRef, kCFNumberIntType, &pct);
                    }
                    CFRelease(batRef);

                    if (pct >= 0 && pct <= 100) {
                        char dev_name[64] = "Bluetooth Peripherie";
                        CFTypeRef nameRef = IORegistryEntryCreateCFProperty(service, CFSTR("Product"), kCFAllocatorDefault, 0);
                        if (!nameRef) {
                            nameRef = IORegistryEntryCreateCFProperty(service, CFSTR("USB Product Name"), kCFAllocatorDefault, 0);
                        }
                        if (nameRef && CFGetTypeID(nameRef) == CFStringGetTypeID()) {
                            CFStringGetCString((CFStringRef)nameRef, dev_name, sizeof(dev_name), kCFStringEncodingUTF8);
                        }
                        if (nameRef) CFRelease(nameRef);

                        // Duplikate vermeiden
                        bool duplicate = false;
                        for (size_t d = 0; d < found; d++) {
                            if (strcmp(out_sources[d].name, dev_name) == 0) {
                                duplicate = true;
                                break;
                            }
                        }

                        if (!duplicate) {
                            CIYBatteryInfo info;
                            memset(&info, 0, sizeof(info));
                            strncpy(info.name, dev_name, sizeof(info.name) - 1);
                            info.type = CIY_BATTERY_SOURCE_PERIPHERAL;
                            info.percent = pct;
                            info.is_present = true;
                            info.is_charging = false;
                            out_sources[found++] = info;
                        }
                    }
                }
                IOObjectRelease(service);
            }
            IOObjectRelease(iter);
        }
    }
#elif defined(__linux__)
    DIR *dir = opendir("/sys/class/power_supply");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && found < max_count) {
            if (entry->d_name[0] == '.') continue;

            char path[256];
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", entry->d_name);
            FILE *f = fopen(path, "r");
            if (!f) continue;
            char type_str[32] = {0};
            if (fgets(type_str, sizeof(type_str), f)) {
                size_t l = strlen(type_str);
                while (l > 0 && (type_str[l - 1] == '\n' || type_str[l - 1] == '\r')) {
                    type_str[--l] = '\0';
                }
            }
            fclose(f);

            if (strcmp(type_str, "Battery") != 0 && strcmp(type_str, "UPS") != 0) {
                continue;
            }

            CIYBatteryInfo info;
            memset(&info, 0, sizeof(info));
            info.percent = -1;
            strncpy(info.name, entry->d_name, sizeof(info.name) - 1);
            info.type = (strcmp(type_str, "UPS") == 0) ? CIY_BATTERY_SOURCE_UPS : CIY_BATTERY_SOURCE_INTERNAL;
            info.is_present = true;

            // Capacity
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", entry->d_name);
            f = fopen(path, "r");
            if (f) {
                int cap = -1;
                if (fscanf(f, "%d", &cap) == 1) {
                    if (cap < 0) cap = 0;
                    if (cap > 100) cap = 100;
                    info.percent = cap;
                }
                fclose(f);
            }

            // Status
            snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", entry->d_name);
            f = fopen(path, "r");
            if (f) {
                char st[32] = {0};
                if (fgets(st, sizeof(st), f)) {
                    if (strstr(st, "Charging") != NULL || strstr(st, "Full") != NULL) {
                        info.is_charging = true;
                    }
                }
                fclose(f);
            }

            out_sources[found++] = info;
        }
        closedir(dir);
    }
#endif

    return found;
}

/// Holt Informationen über die bevorzugte Stromquelle (oder die erste verfügbare).
CIY_INLINE bool ciy_battery_get_info(CIYBatterySourceType preferred_type, CIYBatteryInfo *out_info) {
    if (!out_info) return false;

    if (ciy_battery_check_mocks(out_info)) {
        return true;
    }

    CIYBatteryInfo sources[16];
    size_t count = ciy_battery_list_sources(sources, 16);
    if (count == 0) {
        memset(out_info, 0, sizeof(*out_info));
        strncpy(out_info->name, "Keine Stromquelle erkannt", sizeof(out_info->name) - 1);
        out_info->type = preferred_type;
        out_info->percent = -1;
        out_info->is_present = false;
        return false;
    }

    if (preferred_type != CIY_BATTERY_SOURCE_ALL) {
        for (size_t i = 0; i < count; i++) {
            if (sources[i].type == preferred_type && sources[i].is_present && sources[i].percent >= 0) {
                *out_info = sources[i];
                return true;
            }
        }
        // Bevorzugter Typ wurde explizit angefordert (z. B. Peripherie oder USV), ist aber nicht vorhanden
        if (preferred_type != CIY_BATTERY_SOURCE_INTERNAL) {
            memset(out_info, 0, sizeof(*out_info));
            const char *type_name = (preferred_type == CIY_BATTERY_SOURCE_PERIPHERAL) ? "Peripherie" : "USV";
            snprintf(out_info->name, sizeof(out_info->name), "Keine %s-Stromquelle verbunden", type_name);
            out_info->type = preferred_type;
            out_info->percent = -1;
            out_info->is_present = false;
            return false;
        }
    }

    // Fallback: Erste gefundene Stromquelle mit gültigem Prozentwert
    for (size_t i = 0; i < count; i++) {
        if (sources[i].is_present && sources[i].percent >= 0) {
            *out_info = sources[i];
            return true;
        }
    }

    *out_info = sources[0];
    return (sources[0].percent >= 0);
}

/// Parst einen Ziel-String wie "1-0", "space", "nav" oder eine Liste wie "14,9,15,21".
CIY_INLINE bool ciy_battery_parse_target(const char *target_str, int *out_slots,
                                         size_t max_slots, size_t *out_count) {
    if (out_count) {
        *out_count = 0;
    }
    if (out_slots && max_slots > 0) {
        size_t init_cnt = (max_slots <= CIY_LED_COUNT) ? max_slots : CIY_LED_COUNT;
        memset(out_slots, 0, init_cnt * sizeof(int));
    }
    if (!target_str || !out_slots || max_slots == 0 || !out_count) return false;

    if (strcmp(target_str, "1-0") == 0 || strcmp(target_str, "1..0") == 0 || strcmp(target_str, "numbers") == 0) {
        if (max_slots < 10) return false;
        for (size_t i = 0; i < 10; i++) {
            out_slots[i] = CIY_BATTERY_TARGET_1_0_SLOTS[i];
        }
        *out_count = 10;
        return true;
    }

    if (strcmp(target_str, "space") == 0 || strcmp(target_str, "leertaste") == 0) {
        out_slots[0] = CIY_BATTERY_TARGET_SPACE_SLOTS[0];
        *out_count = 1;
        return true;
    }

    if (strcmp(target_str, "nav") == 0 || strcmp(target_str, "navigation") == 0) {
        if (max_slots < 4) return false;
        for (size_t i = 0; i < 4; i++) {
            out_slots[i] = CIY_BATTERY_TARGET_NAV_SLOTS[i];
        }
        *out_count = 4;
        return true;
    }

    // Komma-separierte Slot-Liste parsen (z. B. "14,9,15,21")
    size_t count = 0;
    const char *p = target_str;
    while (*p && count < max_slots) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *end = NULL;
        long val = strtol(p, &end, 10);
        if (end == p || val < 0 || val > CIY_MAX_MATRIX_SLOT) {
            if (count > 0) memset(out_slots, 0, count * sizeof(int));
            return false;
        }
        out_slots[count++] = (int)val;
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ',') p++;
    }

    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    if (*p != '\0') {
        if (count > 0) memset(out_slots, 0, count * sizeof(int));
        return false; // Überzählige oder unlesbare Zeichen vorhanden
    }

    if (count == 0) return false;
    *out_count = count;
    return true;
}

/// Berechnet einen 378-Byte Planar-RGB-Puffer mit Akku-Overlay.
/// Übernimmt Basisfarben aus base_planar_rgb (falls nicht NULL, sonst schwarz).
/// target_slots enthält die geordnete Sequenz der Tasten-Slots.
/// Wenn segmented == false, wird die Farbe anhand des Gesamtprozentsatzes bestimmt.
/// Wenn segmented == true, spiegelt jede Taste ihren Teilbereich (Ampel) wider.
CIY_INLINE bool ciy_battery_calculate_overlay(const uint8_t *base_planar_rgb,
                                             int percent,
                                             bool is_charging,
                                             const int *target_slots,
                                             size_t target_count,
                                             bool segmented,
                                             uint8_t out_planar_rgb[CIY_PLANAR_RGB_SIZE]) {
    if (!target_slots || target_count == 0 || target_count > CIY_LED_COUNT || !out_planar_rgb) return false;

    if (base_planar_rgb) {
        if (base_planar_rgb != out_planar_rgb) {
            memcpy(out_planar_rgb, base_planar_rgb, CIY_PLANAR_RGB_SIZE);
        }
    } else {
        memset(out_planar_rgb, 0, CIY_PLANAR_RGB_SIZE);
    }

    int p = percent;
    if (p < 0) p = 0;
    if (p > 100) p = 100;

    // Ermittle Anzahl aktiver Tasten im Indikator
    size_t active_count = 0;
    if (target_count == 1) {
        // Einzeltaste (z. B. Leertaste): kontinuierliche Statusanzeige (immer 1 aktive Taste)
        active_count = 1;
    } else if (p > 0) {
        active_count = (size_t)(((uint64_t)p * (uint64_t)target_count + 99ULL) / 100ULL);
        if (active_count > target_count) active_count = target_count;
    }

    // Bestimme Grundfarbe für den Pegel
    uint8_t lvl_r = 0, lvl_g = 0, lvl_b = 0;
    ciy_battery_get_color(p, is_charging, &lvl_r, &lvl_g, &lvl_b);

    for (size_t i = 0; i < target_count; i++) {
        int slot = target_slots[i];
        if (slot < 0 || slot > CIY_MAX_MATRIX_SLOT) continue;

        int led = slot + 1; // CIY X79 LED-Index = slot + 1
        if (led >= CIY_LED_COUNT) continue;

        uint8_t r = 0, g = 0, b = 0;
        if (i < active_count) {
            if (is_charging) {
                r = CIY_BATTERY_COLOR_CYAN_R;
                g = CIY_BATTERY_COLOR_CYAN_G;
                b = CIY_BATTERY_COLOR_CYAN_B;
            } else if (segmented && target_count > 1) {
                // Segmentierte Ampelfarbe pro Taste
                int key_p = (int)(((uint64_t)(i + 1) * 100ULL) / (uint64_t)target_count);
                ciy_battery_get_color(key_p, false, &r, &g, &b);
            } else {
                r = lvl_r;
                g = lvl_g;
                b = lvl_b;
            }
        } else {
            // Nicht aktive Stufe: LED aus (0, 0, 0)
            r = 0;
            g = 0;
            b = 0;
        }

        out_planar_rgb[led] = r;
        out_planar_rgb[CIY_LED_COUNT + led] = g;
        out_planar_rgb[(2 * CIY_LED_COUNT) + led] = b;
    }

    return true;
}

#ifdef __cplusplus
}
#endif

#endif // CIY_BATTERY_H
