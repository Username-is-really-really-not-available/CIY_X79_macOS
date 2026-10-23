#include "ciy_hid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

bool ciy_open(CIYDevice *d) {
    if (!d) return false;
    d->dev = NULL;

    CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchingDict) return false;
    int vid = CIY_VENDOR_ID, pid = CIY_PRODUCT_ID;
    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    CFDictionarySetValue(matchingDict, CFSTR(kUSBVendorID), vidRef);
    CFDictionarySetValue(matchingDict, CFSTR(kUSBProductID), pidRef);
    CFRelease(vidRef);
    CFRelease(pidRef);

    io_iterator_t iter = 0;
    // Das matchingDict wird von IOServiceGetMatchingServices verbraucht.
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iter) != KERN_SUCCESS || !iter) {
        return false;
    }
    io_service_t service = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (!service) return false;

    IOCFPlugInInterface **plugIn = NULL;
    SInt32 score = 0;
    kern_return_t kr = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID,
                                                         kIOCFPlugInInterfaceID, &plugIn, &score);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS || !plugIn) return false;

    IOUSBDeviceInterface **dev = NULL;
    HRESULT qr = (*plugIn)->QueryInterface(plugIn, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&dev);
    (*plugIn)->Release(plugIn);
    if (qr != S_OK || !dev) return false;

    d->dev = dev;
    return true;
}

void ciy_close(CIYDevice *d) {
    if (d && d->dev) {
        (*d->dev)->Release(d->dev);
        d->dev = NULL;
    }
}

static pthread_mutex_t s_bus_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static int s_bus_lock_fd = -1;
static int s_bus_lock_depth = 0;

static int ciy_open_lock_file(void) {
    int fd = open("/tmp/ciy_x79_bus.lock", O_RDONLY | O_CREAT | O_NOFOLLOW, 0644);
    if (fd >= 0) {
        fchmod(fd, 0644);
    } else if (errno == EACCES || errno == EPERM || errno == ELOOP) {
        char user_path[64];
        snprintf(user_path, sizeof(user_path), "/tmp/ciy_x79_bus_%u.lock", (unsigned)getuid());
        fd = open(user_path, O_RDONLY | O_CREAT | O_NOFOLLOW, 0644);
        if (fd >= 0) {
            fchmod(fd, 0644);
        }
    }
    return fd;
}

bool ciy_bus_lock(void) {
    pthread_mutex_lock(&s_bus_mutex);
    if (s_bus_lock_depth == 0) {
        s_bus_lock_fd = ciy_open_lock_file();
        if (s_bus_lock_fd >= 0) {
            // Bis zu 2.0s Timeout für Inter-Process-Locking gegen Race Conditions
            struct timespec start, now;
            clock_gettime(CLOCK_MONOTONIC, &start);
            while (flock(s_bus_lock_fd, LOCK_EX | LOCK_NB) != 0) {
                clock_gettime(CLOCK_MONOTONIC, &now);
                double elapsed = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) / 1e9;
                if (elapsed > 2.0) {
                    flock(s_bus_lock_fd, LOCK_EX); // Fallback auf blockierenden Lock
                    break;
                }
                usleep(5000); // 5 ms Pause
            }
        }
    }
    s_bus_lock_depth++;
    return true;
}

void ciy_bus_unlock(void) {
    if (s_bus_lock_depth > 0) {
        s_bus_lock_depth--;
        if (s_bus_lock_depth == 0) {
            if (s_bus_lock_fd >= 0) {
                flock(s_bus_lock_fd, LOCK_UN);
                close(s_bus_lock_fd);
                s_bus_lock_fd = -1;
            }
        }
    }
    pthread_mutex_unlock(&s_bus_mutex);
}

static bool ciy_request(CIYDevice *d, bool out, uint8_t bRequest, uint8_t reportId, uint8_t *buf, uint16_t len,
                        UInt32 *done) {
    if (!d || !d->dev) return false;
    IOUSBDevRequest req = {
        .bmRequestType = (UInt8)USBmakebmRequestType(out ? kUSBOut : kUSBIn, kUSBClass, kUSBInterface),
        .bRequest = bRequest,
        .wValue = (uint16_t)((0x03 << 8) | reportId),
        .wIndex = 1,
        .wLength = len,
        .pData = buf
    };
    IOReturn kr = (*d->dev)->DeviceRequest(d->dev, &req);
    if (done) *done = req.wLenDone;
    return kr == kIOReturnSuccess;
}

bool ciy_get_state(CIYDevice *d, CIYLightingState *out) {
    if (!d) return false;
    uint8_t buf[42] = {0x0A};
    UInt32 done = 0;
    ciy_bus_lock();
    bool ok = ciy_request(d, false, 0x01 /* GET_REPORT */, 0x0A, buf, sizeof(buf), &done);
    ciy_bus_unlock();
    if (!ok || done < 5) return false;
    if (out) {
        out->mode = buf[1];
        out->color = buf[2];
        out->speed = (buf[3] <= CIY_MAX_SPEED) ? (CIY_MAX_SPEED - buf[3]) : 0;
        out->brightness = (buf[4] <= CIY_MAX_BRIGHTNESS) ? buf[4] : CIY_MAX_BRIGHTNESS;
    }
    return true;
}

bool ciy_set_state(CIYDevice *d, CIYLightingState st) {
    if (!d) return false;
    if (st.mode > CIY_MAX_MODE || st.color > CIY_MAX_COLOR ||
        st.speed > CIY_MAX_SPEED || st.brightness > CIY_MAX_BRIGHTNESS) {
        return false;
    }
    uint8_t buf[42] = {0};
    buf[0] = 0x0A;
    buf[1] = st.mode;
    buf[2] = st.color;
    buf[3] = CIY_MAX_SPEED - st.speed;
    buf[4] = st.brightness;
    ciy_bus_lock();
    bool ok = ciy_request(d, true, 0x09 /* SET_REPORT */, 0x0A, buf, sizeof(buf), NULL);
    ciy_bus_unlock();
    return ok;
}

bool ciy_send_planar_rgb(CIYDevice *d, const uint8_t *rgb, size_t length) {
    if (!d || !rgb || length != CIY_PLANAR_RGB_SIZE) return false;
    uint8_t buf[1 + CIY_PLANAR_RGB_SIZE];
    buf[0] = 0x0B;
    memcpy(&buf[1], rgb, CIY_PLANAR_RGB_SIZE);
    ciy_bus_lock();
    bool ok = ciy_request(d, true, 0x09, 0x0B, buf, sizeof(buf), NULL);
    ciy_bus_unlock();
    return ok;
}

bool ciy_apply_custom_profile_with_brightness(CIYDevice *d, const uint8_t *rgb, size_t length, uint8_t brightness) {
    if (!d) return false;
    ciy_bus_lock();
    bool ok = ciy_send_planar_rgb(d, rgb, length);
    usleep(50000); // 50 ms wie im Hersteller-Treiber
    uint8_t b = (brightness <= CIY_MAX_BRIGHTNESS) ? brightness : CIY_MAX_BRIGHTNESS;
    CIYLightingState st = {19, 0, 0, b};
    bool modeOk = ciy_set_state(d, st);
    ciy_bus_unlock();
    return ok && modeOk;
}

bool ciy_apply_custom_profile(CIYDevice *d, const uint8_t *rgb, size_t length) {
    return ciy_apply_custom_profile_with_brightness(d, rgb, length, CIY_MAX_BRIGHTNESS);
}

void ciy_build_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], const int *indices, int count,
                       bool red, bool green, bool baseBlue) {
    memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    uint8_t *r = &rgb[0], *g = &rgb[CIY_LED_COUNT], *b = &rgb[2 * CIY_LED_COUNT];
    if (baseBlue) memset(b, 0xFF, CIY_LED_COUNT);
    for (int k = 0; k < count; k++) {
        int idx = indices[k];
        if (idx < 0 || idx >= CIY_LED_COUNT) continue;
        b[idx] = 0x00;
        if (red)   r[idx] = 0xFF;
        if (green) g[idx] = 0xFF;
    }
}

void ciy_build_wasd_and_arrows_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], bool baseBlue) {
    memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    uint8_t *r = &rgb[0], *g = &rgb[CIY_LED_COUNT], *b = &rgb[2 * CIY_LED_COUNT];
    if (baseBlue) {
        memset(b, 0xFF, CIY_LED_COUNT);
    }
    // WASD: W=15, A=10, S=16, D=22 in ROT
    const int wasd_idx[] = {15, 10, 16, 22};
    for (int k = 0; k < 4; k++) {
        int idx = wasd_idx[k];
        b[idx] = 0x00;
        r[idx] = 0xFF;
        g[idx] = 0x00;
    }
    // Pfeiltasten: Hoch=89, Links=84, Runter=90, Rechts=96 in GELB (Rot + Grün)
    const int arrow_idx[] = {89, 84, 90, 96};
    for (int k = 0; k < 4; k++) {
        int idx = arrow_idx[k];
        b[idx] = 0x00;
        r[idx] = 0xFF;
        g[idx] = 0xFF;
    }
}

void ciy_build_solid_color_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], uint8_t r, uint8_t g, uint8_t b) {
    memset(&rgb[0], r, CIY_LED_COUNT);
    memset(&rgb[CIY_LED_COUNT], g, CIY_LED_COUNT);
    memset(&rgb[2 * CIY_LED_COUNT], b, CIY_LED_COUNT);
}

void ciy_build_all_red_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE]) {
    ciy_build_solid_color_profile(rgb, 0xFF, 0x00, 0x00);
}

static int hex_digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ciy_parse_hex_color(const char *str, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (!str || !r || !g || !b) return false;
    while (*str && (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')) str++;
    if (*str == '#') str++;
    size_t len = 0;
    while (str[len] && str[len] != ' ' && str[len] != '\t' && str[len] != '\r' && str[len] != '\n') {
        len++;
    }
    for (size_t i = len; str[i]; i++) {
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\r' && str[i] != '\n') return false;
    }
    if (len == 6) {
        int r1 = hex_digit_val(str[0]), r2 = hex_digit_val(str[1]);
        int g1 = hex_digit_val(str[2]), g2 = hex_digit_val(str[3]);
        int b1 = hex_digit_val(str[4]), b2 = hex_digit_val(str[5]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
        *r = (uint8_t)((r1 << 4) | r2);
        *g = (uint8_t)((g1 << 4) | g2);
        *b = (uint8_t)((b1 << 4) | b2);
        return true;
    } else if (len == 3) {
        int r1 = hex_digit_val(str[0]);
        int g1 = hex_digit_val(str[1]);
        int b1 = hex_digit_val(str[2]);
        if (r1 < 0 || g1 < 0 || b1 < 0) return false;
        *r = (uint8_t)((r1 << 4) | r1);
        *g = (uint8_t)((g1 << 4) | g1);
        *b = (uint8_t)((b1 << 4) | b1);
        return true;
    }
    return false;
}

static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static const char *parse_json_str(const char *p, char *out, size_t max_len) {
    if (!p || *p != '"') return NULL;
    p++;
    size_t written = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) return NULL;
        }
        if (out && written + 1 < max_len) {
            out[written++] = *p;
        }
        p++;
    }
    if (*p != '"') return NULL;
    if (out && max_len > 0) {
        out[written] = '\0';
    }
    return p + 1;
}

static const char *skip_json_val(const char *p) {
    p = skip_ws(p);
    if (!*p) return p;
    if (*p == '"') {
        const char *next = parse_json_str(p, NULL, 0);
        return next ? next : (p + 1);
    }
    if (*p == '{') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                const char *next = parse_json_str(p, NULL, 0);
                p = next ? next : (p + 1);
                continue;
            }
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            p++;
        }
        return p;
    }
    if (*p == '[') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                const char *next = parse_json_str(p, NULL, 0);
                p = next ? next : (p + 1);
                continue;
            }
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        p++;
    }
    return p;
}

bool ciy_parse_profile_json(const char *json, const char *target_profile,
                            uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                            char *out_name, size_t max_name_len,
                            int *out_slots_count) {
    if (!json || !rgb) return false;
    memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    if (out_name && max_name_len > 0) out_name[0] = '\0';
    if (out_slots_count) *out_slots_count = 0;

    // UTF-8 BOM (\xEF\xBB\xBF) überspringen
    if ((unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB && (unsigned char)json[2] == 0xBF) {
        json += 3;
    }

    const char *p = skip_ws(json);
    bool in_array = (*p == '[');
    if (in_array) {
        p = skip_ws(p + 1);
    }

    int profile_index = 0;
    bool found_target = false;

    while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p != '{') break;
        p = skip_ws(p + 1);
        profile_index++;

        char prof_name[64] = {0};
        int valid_count = 0;
        char key[64];
        char slot_key[32];
        char hex_val[32];
        uint8_t temp_rgb[CIY_PLANAR_RGB_SIZE] = {0};

        while (*p && *p != '}') {
            p = skip_ws(p);
            if (*p == '}') break;

            p = parse_json_str(p, key, sizeof(key));
            if (!p) return false;
            p = skip_ws(p);
            if (*p != ':') return false;
            p = skip_ws(p + 1);

            if (strcmp(key, "name") == 0) {
                if (*p == '"') {
                    p = parse_json_str(p, prof_name, sizeof(prof_name));
                    if (!p) return false;
                } else {
                    p = skip_json_val(p);
                }
            } else if (strcmp(key, "slots") == 0) {
                if (*p != '{') {
                    p = skip_json_val(p);
                } else {
                    p = skip_ws(p + 1);
                    while (*p && *p != '}') {
                        p = skip_ws(p);
                        if (*p == '}') break;

                        p = parse_json_str(p, slot_key, sizeof(slot_key));
                        if (!p) return false;
                        p = skip_ws(p);
                        if (*p != ':') return false;
                        p = skip_ws(p + 1);

                        if (*p == '"') {
                            p = parse_json_str(p, hex_val, sizeof(hex_val));
                            if (!p) return false;

                            uint8_t r = 0, g = 0, b = 0;
                            if (ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                                char *endptr = NULL;
                                long slot = strtol(slot_key, &endptr, 10);
                                if (endptr != slot_key && *endptr == '\0' && slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
                                    int idx = (int)(slot + 1);
                                    temp_rgb[idx] = r;
                                    temp_rgb[CIY_LED_COUNT + idx] = g;
                                    temp_rgb[2 * CIY_LED_COUNT + idx] = b;
                                    valid_count++;
                                }
                            }
                        } else {
                            p = skip_json_val(p);
                        }

                        p = skip_ws(p);
                        if (*p == ',') p = skip_ws(p + 1);
                    }
                    if (*p == '}') p = skip_ws(p + 1);
                }
            } else if (key[0] >= '0' && key[0] <= '9') {
                if (*p == '"') {
                    p = parse_json_str(p, hex_val, sizeof(hex_val));
                    if (!p) return false;

                    uint8_t r = 0, g = 0, b = 0;
                    if (ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                        char *endptr = NULL;
                        long slot = strtol(key, &endptr, 10);
                        if (endptr != key && *endptr == '\0' && slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
                            int idx = (int)(slot + 1);
                            temp_rgb[idx] = r;
                            temp_rgb[CIY_LED_COUNT + idx] = g;
                            temp_rgb[2 * CIY_LED_COUNT + idx] = b;
                            valid_count++;
                        }
                    }
                } else {
                    p = skip_json_val(p);
                }
            } else {
                p = skip_json_val(p);
            }

            p = skip_ws(p);
            if (*p == ',') p = skip_ws(p + 1);
        }

        if (*p == '}') p = skip_ws(p + 1);

        bool match = false;
        if (!target_profile || target_profile[0] == '\0') {
            match = true;
        } else {
            if (prof_name[0] && strcasecmp(prof_name, target_profile) == 0) {
                match = true;
            } else {
                char *endptr = NULL;
                long req_idx = strtol(target_profile, &endptr, 10);
                if (endptr != target_profile && *endptr == '\0' && req_idx == profile_index) {
                    match = true;
                }
            }
        }

        if (match && valid_count > 0) {
            memcpy(rgb, temp_rgb, CIY_PLANAR_RGB_SIZE);
            if (out_name && max_name_len > 0) {
                strncpy(out_name, prof_name, max_name_len - 1);
                out_name[max_name_len - 1] = '\0';
            }
            if (out_slots_count) *out_slots_count = valid_count;
            found_target = true;
            break;
        }

        if (!in_array) break;

        p = skip_ws(p);
        if (*p == ',') p = skip_ws(p + 1);
    }

    return found_target;
}

bool ciy_load_profile_file(const char *path, const char *target_profile,
                           uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                           char *out_name, size_t max_name_len,
                           int *out_slots_count) {
    if (!path || !rgb) return false;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size <= 0 || st.st_size > 512 * 1024) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    char *buf = (char *)malloc((size_t)st.st_size + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    buf[read_bytes] = '\0';

    bool ok = ciy_parse_profile_json(buf, target_profile, rgb, out_name, max_name_len, out_slots_count);
    free(buf);
    return ok;
}

