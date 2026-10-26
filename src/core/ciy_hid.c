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

#define CIY_TESTING_MOCK_DEV ((IOUSBDeviceInterface **)0x1)

bool ciy_open(CIYDevice *d) {
    if (!d) return false;
    d->dev = NULL;

    if (getenv("CIY_FAIL_ON_HARDWARE_IO") != NULL) {
        fprintf(stderr, "SECURITY GUARD ASSERTION: ciy_open called while CIY_FAIL_ON_HARDWARE_IO is set!\n");
        abort();
    }

    if (getenv("CIY_TESTING") != NULL) {
        d->dev = CIY_TESTING_MOCK_DEV;
        return true;
    }

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
        if (d->dev != CIY_TESTING_MOCK_DEV) {
            (*d->dev)->Release(d->dev);
        }
        d->dev = NULL;
    }
}

static pthread_mutex_t s_bus_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static int s_bus_lock_fd = -1;
static int s_bus_lock_depth = 0;

const char *ciy_bus_lock_path(char *buf, size_t bufsize) {
    if (getenv("CIY_TESTING") != NULL) {
        snprintf(buf, bufsize, "/tmp/ciy_x79_bus_%u_%d.lock", (unsigned)getuid(), (int)getpid());
    } else {
        snprintf(buf, bufsize, "/tmp/ciy_x79_bus_%u.lock", (unsigned)getuid());
    }
    return buf;
}

static int ciy_open_lock_file(void) {
    char user_path[128];
    ciy_bus_lock_path(user_path, sizeof(user_path));
    int fd = open(user_path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        fchmod(fd, 0600);
    }
    return fd;
}

bool ciy_bus_lock(void) {
    if (getenv("CIY_FAIL_ON_HARDWARE_IO") != NULL) {
        fprintf(stderr, "SECURITY GUARD ASSERTION: ciy_bus_lock called while CIY_FAIL_ON_HARDWARE_IO is set!\n");
        abort();
    }
    pthread_mutex_lock(&s_bus_mutex);
    if (s_bus_lock_depth == 0) {
        s_bus_lock_fd = ciy_open_lock_file();
        if (s_bus_lock_fd < 0) {
            pthread_mutex_unlock(&s_bus_mutex);
            return false;
        }
        // Bis zu 2.0s Timeout für Inter-Process-Locking gegen Race Conditions
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        while (true) {
            int ret = flock(s_bus_lock_fd, LOCK_EX | LOCK_NB);
            if (ret == 0) break; // Lock acquired
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
                close(s_bus_lock_fd);
                s_bus_lock_fd = -1;
                pthread_mutex_unlock(&s_bus_mutex);
                return false;
            }
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) / 1e9;
            if (elapsed > 2.0) {
                // Timeout erreicht: Keinen blockierenden Fallback erzwingen, sondern sauber mit false abbrechen
                close(s_bus_lock_fd);
                s_bus_lock_fd = -1;
                s_bus_lock_depth = 0;
                pthread_mutex_unlock(&s_bus_mutex);
                return false;
            }
            struct timespec req = {0, 5000000}, rem;
            nanosleep(&req, &rem); // 5 ms Pause, handles EINTR via rem
        }
    }
    s_bus_lock_depth++;
    return true;
}

void ciy_bus_unlock(void) {
    if (s_bus_lock_depth <= 0) return; // unbalancierter Aufruf: Mutex nicht über-entsperren
    s_bus_lock_depth--;
    if (s_bus_lock_depth == 0) {
        if (s_bus_lock_fd >= 0) {
            flock(s_bus_lock_fd, LOCK_UN);
            close(s_bus_lock_fd);
            s_bus_lock_fd = -1;
        }
    }
    pthread_mutex_unlock(&s_bus_mutex);
}

static bool ciy_request(CIYDevice *d, bool out, uint8_t bRequest, uint8_t reportId, uint8_t *buf, uint16_t len,
                        UInt32 *done) {
    if (getenv("CIY_FAIL_ON_HARDWARE_IO") != NULL) {
        fprintf(stderr, "SECURITY GUARD ASSERTION: ciy_request called while CIY_FAIL_ON_HARDWARE_IO is set!\n");
        abort();
    }
    if (!d || !d->dev) return false;
    if (d->dev == CIY_TESTING_MOCK_DEV) {
        if (!out && getenv("CIY_TESTING_FAIL_GET_REPORTS") != NULL) return false;
        if (!out && reportId == 0x0A && buf && len >= 5) {
            buf[0] = 0x0A;
            buf[1] = 19;
            buf[2] = 0;
            buf[3] = 0;
            buf[4] = 5;
        }
        if (done) *done = len;
        return true;
    }
    IOUSBDevRequestTO req = {
        .bmRequestType = (UInt8)USBmakebmRequestType(out ? kUSBOut : kUSBIn, kUSBClass, kUSBInterface),
        .bRequest = bRequest,
        .wValue = (uint16_t)((0x03 << 8) | reportId),
        .wIndex = CIY_INTERFACE_NUM,
        .wLength = len,
        .pData = buf,
        .completionTimeout = 1000, // 1000 ms Timeout
        .noDataTimeout = 1000      // 1000 ms Timeout
    };
    IOReturn kr = (*d->dev)->DeviceRequestTO(d->dev, &req);
    if (done) *done = req.wLenDone;
    return kr == kIOReturnSuccess;
}

bool ciy_get_state(CIYDevice *d, CIYLightingState *out) {
    if (!d) return false;
    uint8_t buf[42] = {0x0A};
    UInt32 done = 0;
    if (!ciy_bus_lock()) return false;
    bool ok = ciy_request(d, false, 0x01 /* GET_REPORT */, 0x0A, buf, sizeof(buf), &done);
    ciy_bus_unlock();
    if (!ok || done < 5 || buf[0] != 0x0A) return false;
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
    if (!ciy_bus_lock()) return false;
    bool ok = ciy_request(d, true, 0x09 /* SET_REPORT */, 0x0A, buf, sizeof(buf), NULL);
    ciy_bus_unlock();
    return ok;
}

bool ciy_send_planar_rgb(CIYDevice *d, const uint8_t *rgb, size_t length) {
    if (!d || !rgb || length != CIY_PLANAR_RGB_SIZE) return false;
    uint8_t buf[1 + CIY_PLANAR_RGB_SIZE];
    buf[0] = 0x0B;
    memcpy(&buf[1], rgb, CIY_PLANAR_RGB_SIZE);
    if (!ciy_bus_lock()) return false;
    bool ok = ciy_request(d, true, 0x09, 0x0B, buf, sizeof(buf), NULL);
    ciy_bus_unlock();
    return ok;
}

bool ciy_build_transfer_plan(CIYTransferPlan *out_plan,
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
    out_plan->steps[0].report_id = 0x0B;
    out_plan->steps[0].length = 1 + CIY_PLANAR_RGB_SIZE; // 379
    out_plan->steps[0].data[0] = 0x0B;
    memcpy(&out_plan->steps[0].data[1], rgb, CIY_PLANAR_RGB_SIZE);
    snprintf(out_plan->steps[0].description, sizeof(out_plan->steps[0].description),
             "Report 0x0B (379 bytes)");

    // Schritt 2: 50 ms Pause
    out_plan->steps[1].type = CIY_PLAN_STEP_DELAY;
    out_plan->steps[1].delay_ms = 50;
    snprintf(out_plan->steps[1].description, sizeof(out_plan->steps[1].description),
             "wait 50 ms");

    // Schritt 3: Report 0x0A (42 Bytes, Modus 19, gewählte Helligkeit)
    out_plan->steps[2].type = CIY_PLAN_STEP_REPORT;
    out_plan->steps[2].report_id = 0x0A;
    out_plan->steps[2].length = 42;
    out_plan->steps[2].mode = 19;
    out_plan->steps[2].brightness = out_plan->brightness;
    out_plan->steps[2].data[0] = 0x0A;
    out_plan->steps[2].data[1] = 19; // Modus 19 (Customize)
    out_plan->steps[2].data[2] = 0;  // Farbe 0
    out_plan->steps[2].data[3] = CIY_MAX_SPEED - 0; // Speed default
    out_plan->steps[2].data[4] = out_plan->brightness;
    snprintf(out_plan->steps[2].description, sizeof(out_plan->steps[2].description),
             "Report 0x0A (42 bytes, mode 19)");

    return true;
}

bool ciy_execute_transfer_plan(CIYDevice *d, const CIYTransferPlan *plan,
                               CIYCancelCheck cancel_check, void *cancel_ctx) {
    if (!d || !d->dev || !plan || plan->step_count == 0) return false;
    if (cancel_check && cancel_check(cancel_ctx)) return false;
    if (!ciy_bus_lock()) return false;

    bool ok = true;
    for (size_t i = 0; i < plan->step_count; i++) {
        if (cancel_check && cancel_check(cancel_ctx)) {
            ok = false;
            break;
        }

        const CIYPlanStep *step = &plan->steps[i];
        if (step->type == CIY_PLAN_STEP_REPORT) {
            uint8_t buf[1 + CIY_PLANAR_RGB_SIZE];
            if (step->length == 0 || step->length > sizeof(buf) || step->length > sizeof(step->data)) {
                ok = false;
                break;
            }
            memcpy(buf, step->data, step->length);
            ok = ciy_request(d, true, 0x09 /* SET_REPORT */, step->report_id, buf, step->length, NULL);
            if (!ok) break;
        } else if (step->type == CIY_PLAN_STEP_DELAY) {
            // In 5-ms-Schritten schlafen, damit cancel_check schnell greift
            uint32_t ms = step->delay_ms;
            uint32_t chunks = (ms + 4) / 5;
            for (uint32_t c = 0; c < chunks; c++) {
                struct timespec req = {0, 5 * 1000 * 1000};
                nanosleep(&req, NULL);
                if (cancel_check && cancel_check(cancel_ctx)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
        }
    }

    ciy_bus_unlock();
    return ok;
}

bool ciy_apply_custom_profile_with_brightness_cancelable(
    CIYDevice *d, const uint8_t *rgb, size_t length, uint8_t brightness,
    CIYCancelCheck cancel_check, void *cancel_ctx) {
    CIYTransferPlan plan;
    if (!ciy_build_transfer_plan(&plan, rgb, length, brightness, NULL, 0)) {
        return false;
    }
    return ciy_execute_transfer_plan(d, &plan, cancel_check, cancel_ctx);
}

bool ciy_apply_custom_profile_with_brightness(CIYDevice *d, const uint8_t *rgb, size_t length, uint8_t brightness) {
    return ciy_apply_custom_profile_with_brightness_cancelable(d, rgb, length, brightness, NULL, NULL);
}

bool ciy_apply_custom_profile(CIYDevice *d, const uint8_t *rgb, size_t length) {
    return ciy_apply_custom_profile_with_brightness(d, rgb, length, CIY_MAX_BRIGHTNESS);
}

static void ciy_build_profile_apply(uint8_t rgb[CIY_PLANAR_RGB_SIZE], const int *indices, int count,
                                    bool red, bool green, bool baseBlue);

void ciy_build_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], const int *indices, int count,
                       bool red, bool green, bool baseBlue) {
    memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    ciy_build_profile_apply(rgb, indices, count, red, green, baseBlue);
}

static void ciy_build_profile_apply(uint8_t rgb[CIY_PLANAR_RGB_SIZE], const int *indices, int count,
                                    bool red, bool green, bool baseBlue) {
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
    const int wasd_idx[] = {15, 10, 16, 22};
    const int arrow_idx[] = {89, 84, 90, 96};
    ciy_build_profile_apply(rgb, wasd_idx, 4, true, false, baseBlue);
    ciy_build_profile_apply(rgb, arrow_idx, 4, true, true, false);
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

static inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const char *parse_json_str(const char *p, char *out, size_t max_len) {
    if (!p || *p != '"') return NULL;
    if (out && max_len == 0) return NULL;
    p++;
    size_t limit = (out != NULL) ? max_len : ((max_len > 0) ? max_len : CIY_JSON_MAX_STRLEN);
    size_t written = 0;

    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) return NULL;
            if (*p == 'u') {
                if (!p[1] || !p[2] || !p[3] || !p[4]) return NULL;
                int h0 = hex_digit(p[1]);
                int h1 = hex_digit(p[2]);
                int h2 = hex_digit(p[3]);
                int h3 = hex_digit(p[4]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return NULL;
                uint32_t cp = (uint32_t)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);

                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // High surrogate: must be followed by \u and a valid low surrogate
                    if (p[5] != '\\' || p[6] != 'u') return NULL;
                    if (!p[7] || !p[8] || !p[9] || !p[10]) return NULL;
                    int l0 = hex_digit(p[7]);
                    int l1 = hex_digit(p[8]);
                    int l2 = hex_digit(p[9]);
                    int l3 = hex_digit(p[10]);
                    if (l0 < 0 || l1 < 0 || l2 < 0 || l3 < 0) return NULL;
                    uint32_t low = (uint32_t)((l0 << 12) | (l1 << 8) | (l2 << 4) | l3);
                    if (low < 0xDC00 || low > 0xDFFF) return NULL;

                    uint32_t full_cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    if (out) {
                        if (written + 4 < limit) {
                            out[written++] = (char)(0xF0 | (full_cp >> 18));
                            out[written++] = (char)(0x80 | ((full_cp >> 12) & 0x3F));
                            out[written++] = (char)(0x80 | ((full_cp >> 6) & 0x3F));
                            out[written++] = (char)(0x80 | (full_cp & 0x3F));
                        }
                    } else {
                        if (written + 4 < limit) written += 4;
                    }
                    p += 11;
                    continue;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    // Isolated low surrogate: invalid
                    return NULL;
                } else {
                    // Valid BMP codepoint
                    if (cp < 0x80) {
                        if (out) {
                            if (written + 1 < limit) out[written++] = (char)cp;
                        } else {
                            if (written + 1 < limit) written++;
                        }
                    } else if (cp < 0x800) {
                        if (out) {
                            if (written + 2 < limit) {
                                out[written++] = (char)(0xC0 | (cp >> 6));
                                out[written++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else {
                            if (written + 2 < limit) written += 2;
                        }
                    } else {
                        if (out) {
                            if (written + 3 < limit) {
                                out[written++] = (char)(0xE0 | (cp >> 12));
                                out[written++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                out[written++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else {
                            if (written + 3 < limit) written += 3;
                        }
                    }
                    p += 5;
                    continue;
                }
            } else {
                /* Einfache Einzeichen-Escapes: \n \r \t \b \f \" \\ \/ */
                static const struct { char esc; char rep; } escape_tab[] = {
                    {'n', '\n'}, {'r', '\r'}, {'t', '\t'}, {'b', '\b'},
                    {'f', '\f'}, {'"', '"'}, {'\\', '\\'}, {'/', '/'},
                };
                char rep = 0;
                for (size_t e = 0; e < sizeof(escape_tab) / sizeof(escape_tab[0]); e++) {
                    if (escape_tab[e].esc == *p) { rep = escape_tab[e].rep; break; }
                }
                if (rep) {
                    if (out) {
                        if (written + 1 < limit) out[written++] = rep;
                    } else {
                        if (written + 1 < limit) written++;
                    }
                    p++;
                    continue;
                }
                return NULL; // Unbekanntes Escape
            }
        }
        if (out) {
            if (written + 1 < limit) out[written++] = *p;
        } else {
            if (written + 1 < limit) written++;
        }
        p++;
    }
    if (*p != '"') return NULL;
    if (out) {
        out[written] = '\0';
    }
    return p + 1;
}

static const char *skip_json_string(const char *p) {
    const char *next = parse_json_str(p, NULL, CIY_JSON_MAX_STRLEN);
    if (!next) return NULL;
    return next;
}

static const char *skip_json_val(const char *p) {
    p = skip_ws(p);
    if (!p || !*p) return NULL;
    if (*p == '"') {
        return parse_json_str(p, NULL, CIY_JSON_MAX_STRLEN);
    }
    if (*p == '{') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p = skip_json_string(p);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            p++;
        }
        if (depth > 0) return NULL;
        return p;
    }
    if (*p == '[') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p = skip_json_string(p);
                if (!p) return NULL;
                continue;
            }
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            p++;
        }
        if (depth > 0) return NULL;
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        p++;
    }
    return p;
}

bool ciy_validate_profile_json(const char *json, CIYValidationResult *out_res) {
    if (out_res) {
        memset(out_res, 0, sizeof(*out_res));
    }
    if (!json) {
        if (out_res) {
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Profil-JSON ist NULL oder leer");
        }
        return false;
    }

    // UTF-8 BOM (\xEF\xBB\xBF) überspringen
    if ((unsigned char)json[0] == 0xEF && (unsigned char)json[1] == 0xBB && (unsigned char)json[2] == 0xBF) {
        json += 3;
    }

    const char *p = skip_ws(json);
    if (!p || !*p) {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Leere Profildatei");
        }
        return false;
    }

    bool in_array = (*p == '[');
    if (in_array) {
        p = skip_ws(p + 1);
        if (*p == ']') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "profiles");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "JSON-Array enthält keine Profile");
            }
            return false;
        }
    } else if (*p != '{') {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "JSON-Wurzel muss ein Objekt oder Array sein");
        }
        return false;
    }

    int profile_count = 0;
    int total_keys = 0;

    while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p != '{') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwartetes Profil-Objekt '{'");
            }
            return false;
        }
        p = skip_ws(p + 1);
        profile_count++;

        int profile_keys_count = 0;
        bool has_slots = false;
        bool seen[CIY_LED_COUNT] = {false};

        while (*p && *p != '}') {
            p = skip_ws(p);
            if (*p == '}') break;

            char key[64];
            const char *next = parse_json_str(p, key, sizeof(key));
            if (!next) {
                if (out_res) {
                    snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                    snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger oder nicht terminierter String im JSON");
                }
                return false;
            }
            p = skip_ws(next);
            if (*p != ':') {
                if (out_res) {
                    snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                    snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwarteter Doppelpunkt nach Schlüssel '%s'", key);
                }
                return false;
            }
            p = skip_ws(p + 1);

            if (strcmp(key, "name") == 0) {
                if (*p != '"') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "name");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Feld 'name' muss ein String sein");
                    }
                    return false;
                }
                char val[64];
                p = parse_json_str(p, val, sizeof(val));
                if (!p) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "name");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Profilname (String-Syntaxfehler)");
                    }
                    return false;
                }
            } else if (strcmp(key, "brightness") == 0) {
                char *endptr = NULL;
                long b = strtol(p, &endptr, 10);
                if (endptr == p || b < 0 || b > CIY_MAX_BRIGHTNESS) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "brightness");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültige Helligkeit (erlaubt: 0..%d)", CIY_MAX_BRIGHTNESS);
                    }
                    return false;
                }
                p = skip_json_val(p);
                if (!p) return false;
            } else if (strcmp(key, "slots") == 0) {
                has_slots = true;
                if (*p != '{') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Feld 'slots' muss ein Objekt sein");
                    }
                    return false;
                }
                p = skip_ws(p + 1);
                while (*p && *p != '}') {
                    p = skip_ws(p);
                    if (*p == '}') break;

                    char slot_key[32];
                    next = parse_json_str(p, slot_key, sizeof(slot_key));
                    if (!next) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Slot-Schlüssel (String-Syntaxfehler)");
                        }
                        return false;
                    }
                    p = skip_ws(next);
                    if (*p != ':') {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Erwarteter Doppelpunkt nach Slot '%s'", slot_key);
                        }
                        return false;
                    }
                    p = skip_ws(p + 1);

                    char *endptr = NULL;
                    long slot = strtol(slot_key, &endptr, 10);
                    if (endptr == slot_key || *endptr != '\0' || slot < 0 || slot >= (CIY_LED_COUNT - 1)) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Matrix-Slot '%s' (erlaubt: 0..%d)", slot_key, CIY_LED_COUNT - 2);
                        }
                        return false;
                    }

                    if (*p != '"') {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Farbwert für Slot '%s' muss ein Hex-Farbstring sein", slot_key);
                        }
                        return false;
                    }
                    char hex_val[32];
                    p = parse_json_str(p, hex_val, sizeof(hex_val));
                    if (!p) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Syntaxfehler im Farbstring für Slot '%s'", slot_key);
                        }
                        return false;
                    }

                    uint8_t r = 0, g = 0, b = 0;
                    if (!ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                        if (out_res) {
                            snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", slot_key);
                            snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiges Hex-Farbformat '%s' für Slot '%s' (erwartet: #RRGGBB oder #RGB)", hex_val, slot_key);
                        }
                        return false;
                    }

                    int idx = (int)(slot + 1);
                    if (!seen[idx]) {
                        seen[idx] = true;
                        profile_keys_count++;
                    }

                    p = skip_ws(p);
                    if (*p == ',') p = skip_ws(p + 1);
                }
                if (*p != '}') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Objekt 'slots' nicht mit '}' geschlossen");
                    }
                    return false;
                }
                p = skip_ws(p + 1);
            } else if (key[0] >= '0' && key[0] <= '9') {
                has_slots = true;
                char *endptr = NULL;
                long slot = strtol(key, &endptr, 10);
                if (endptr == key || *endptr != '\0' || slot < 0 || slot >= (CIY_LED_COUNT - 1)) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiger Matrix-Slot '%s' (erlaubt: 0..%d)", key, CIY_LED_COUNT - 2);
                    }
                    return false;
                }
                if (*p != '"') {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Farbwert für Slot '%s' muss ein Hex-Farbstring sein", key);
                    }
                    return false;
                }
                char hex_val[32];
                p = parse_json_str(p, hex_val, sizeof(hex_val));
                if (!p) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Syntaxfehler im Farbstring für Slot '%s'", key);
                    }
                    return false;
                }
                uint8_t r = 0, g = 0, b = 0;
                if (!ciy_parse_hex_color(hex_val, &r, &g, &b)) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Ungültiges Hex-Farbformat '%s' für Slot '%s' (erwartet: #RRGGBB oder #RGB)", hex_val, key);
                    }
                    return false;
                }
                int idx = (int)(slot + 1);
                if (!seen[idx]) {
                    seen[idx] = true;
                    profile_keys_count++;
                }
            } else {
                p = skip_json_val(p);
                if (!p) {
                    if (out_res) {
                        snprintf(out_res->error_field, sizeof(out_res->error_field), "%s", key);
                        snprintf(out_res->error_message, sizeof(out_res->error_message), "Syntaxfehler im Wert von Feld '%s'", key);
                    }
                    return false;
                }
            }

            p = skip_ws(p);
            if (*p == ',') p = skip_ws(p + 1);
        }

        if (*p != '}') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "Profil-Objekt nicht mit '}' geschlossen");
            }
            return false;
        }
        p = skip_ws(p + 1);

        if (!has_slots || profile_keys_count == 0) {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "slots");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "Profil enthält keine gültigen Tasten-Slots");
            }
            return false;
        }

        total_keys += profile_keys_count;

        if (!in_array) break;

        p = skip_ws(p);
        if (*p == ',') p = skip_ws(p + 1);
    }

    p = skip_ws(p);
    if (in_array) {
        if (*p != ']') {
            if (out_res) {
                snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
                snprintf(out_res->error_message, sizeof(out_res->error_message), "JSON-Array nicht mit ']' geschlossen");
            }
            return false;
        }
        p = skip_ws(p + 1);
    }

    p = skip_ws(p);
    if (*p != '\0') {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Unerwartete Zeichen nach dem JSON-Wurzelelement");
        }
        return false;
    }

    if (profile_count == 0) {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "profiles");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Keine Profile in JSON gefunden");
        }
        return false;
    }

    if (out_res) {
        out_res->is_valid = true;
        out_res->profile_count = profile_count;
        out_res->total_keys_count = total_keys;
    }
    return true;
}

bool ciy_validate_profile_file(const char *path, CIYValidationResult *out_res) {
    if (out_res) {
        memset(out_res, 0, sizeof(*out_res));
    }
    if (!path) {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Dateipfad ist NULL");
        }
        return false;
    }

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' existiert nicht oder konnte nicht geöffnet werden", path);
        }
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' ist keine reguläre Datei", path);
        }
        return false;
    }
    if (st.st_size <= 0) {
        close(fd);
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' ist leer (0 Bytes)", path);
        }
        return false;
    }
    if (st.st_size > 512 * 1024) {
        close(fd);
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' überschreitet maximale Größe (512 KB)", path);
        }
        return false;
    }

    FILE *f = fdopen(fd, "rb");
    if (!f) {
        close(fd);
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' konnte nicht geöffnet werden", path);
        }
        return false;
    }

    char *buf = (char *)malloc((size_t)st.st_size + 1);
    if (!buf) {
        fclose(f);
        if (out_res) {
            snprintf(out_res->error_field, sizeof(out_res->error_field), "memory");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Speicherallokationsfehler");
        }
        return false;
    }

    size_t read_bytes = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    buf[read_bytes] = '\0';

    bool ok = ciy_validate_profile_json(buf, out_res);
    free(buf);
    return ok;
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
        bool seen[CIY_LED_COUNT] = {false};

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
                    if (!p) return false;
                }
            } else if (strcmp(key, "slots") == 0) {
                if (*p != '{') {
                    p = skip_json_val(p);
                    if (!p) return false;
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
                                    if (!seen[idx]) {
                                        seen[idx] = true;
                                        valid_count++;
                                    }
                                }
                            }
                        } else {
                            p = skip_json_val(p);
                            if (!p) return false;
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
                            if (!seen[idx]) {
                                seen[idx] = true;
                                valid_count++;
                            }
                        }
                    }
                } else {
                    p = skip_json_val(p);
                    if (!p) return false;
                }
            } else {
                p = skip_json_val(p);
                if (!p) return false;
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

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return false;
    }
    if (st.st_size <= 0 || st.st_size > 512 * 1024) {
        close(fd);
        return false;
    }

    FILE *f = fdopen(fd, "rb");
    if (!f) {
        close(fd);
        return false;
    }

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

