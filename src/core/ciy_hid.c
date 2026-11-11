#include "ciy_hid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>

#if defined(CIY_ENABLE_TEST_HOOKS)
#define CIY_TESTING_MOCK_DEV ((void *)(uintptr_t)1)
#endif

#if defined(CIY_BACKEND_HIDAPI) || (!defined(__APPLE__) && !defined(CIY_BACKEND_IOKIT))
#include "ciy_hid_hidapi.c"
#else
#include "ciy_hid_iokit.c"
#endif

bool ciy_open(CIYDevice *d) {
    if (!d) return false;
    d->dev = NULL;

#if defined(CIY_ENABLE_TEST_HOOKS)
    if (getenv("CIY_FAIL_ON_HARDWARE_IO") != NULL) {
        fprintf(stderr, "SECURITY GUARD ASSERTION: ciy_open called while CIY_FAIL_ON_HARDWARE_IO is set!\n");
        abort();
    }

    if (getenv("CIY_TESTING") != NULL) {
        d->dev = (void *)CIY_TESTING_MOCK_DEV;
        return true;
    }
#endif

    return ciy_backend_open(d);
}

void ciy_close(CIYDevice *d) {
    if (d && d->dev) {
#if defined(CIY_ENABLE_TEST_HOOKS)
        if (d->dev != (void *)CIY_TESTING_MOCK_DEV) {
            ciy_backend_close(d);
        }
#else
        ciy_backend_close(d);
#endif
        d->dev = NULL;
    }
}

static void ciy_nanosleep_uninterrupted(long ns) {
    if (ns <= 0) return;
    struct timespec req = {
        .tv_sec = (time_t)(ns / 1000000000L),
        .tv_nsec = ns % 1000000000L
    };
    struct timespec rem;
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

static pthread_mutex_t s_bus_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static int s_bus_lock_fd = -1;
static _Atomic int s_bus_lock_depth = 0;
static pthread_t s_bus_lock_owner;
static _Atomic bool s_bus_has_owner = false;
#if defined(CIY_ENABLE_TEST_HOOKS)
static char s_test_lock_path[PATH_MAX > 1024 ? PATH_MAX : 1024] = {0};

static void ciy_cleanup_test_lockfile(void) {
    if (s_test_lock_path[0] != '\0') {
        struct stat st;
        if (lstat(s_test_lock_path, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid()) {
            (void)unlink(s_test_lock_path);
        }
        s_test_lock_path[0] = '\0';
    }
}
#endif

static void ciy_atfork_child(void) {
    // 1. Schließe vererbten Deskriptor OHNE flock(LOCK_UN) aufzurufen!
    if (s_bus_lock_fd >= 0) {
        close(s_bus_lock_fd);
        s_bus_lock_fd = -1;
    }
    // 2. Setze Reentrancy-Zustand zurück
    atomic_store_explicit(&s_bus_lock_depth, 0, memory_order_release);
    atomic_store_explicit(&s_bus_has_owner, false, memory_order_release);

#if defined(CIY_ENABLE_TEST_HOOKS)
    // 3. Test-Lockfile-Pfad im Kind leeren, damit atexit das Eltern-Lockfile nicht löscht
    s_test_lock_path[0] = '\0';
#endif

    // 4. Mutex für den Kindprozess sauber neu initialisieren
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) == 0) {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&s_bus_mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }
}

static pthread_once_t s_bus_lock_init_once = PTHREAD_ONCE_INIT;

static void ciy_bus_lock_init_handlers(void) {
    pthread_atfork(NULL, NULL, ciy_atfork_child);
#if defined(CIY_ENABLE_TEST_HOOKS)
    if (getenv("CIY_TESTING") != NULL) {
        atexit(ciy_cleanup_test_lockfile);
    }
#endif
}

const char *ciy_bus_lock_path(char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) return NULL;
    int r = -1;
#if defined(CIY_ENABLE_TEST_HOOKS)
    if (getenv("CIY_TESTING") != NULL) {
        r = snprintf(buf, bufsize, "/tmp/ciy_x79_bus_%u_%d.lock", (unsigned)getuid(), (int)getpid());
        if (r > 0 && (size_t)r < bufsize) return buf;
        buf[0] = '\0';
        return NULL;
    }
#endif
#if defined(__APPLE__) && defined(_CS_DARWIN_USER_TEMP_DIR)
    char temp_dir[1024];
    size_t n = confstr(_CS_DARWIN_USER_TEMP_DIR, temp_dir, sizeof(temp_dir));
    if (n > 1 && n < sizeof(temp_dir) && temp_dir[0] == '/') {
        struct stat st;
        if (stat(temp_dir, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == getuid()) {
            size_t len = strlen(temp_dir);
            if (len > 0 && temp_dir[len - 1] == '/') {
                r = snprintf(buf, bufsize, "%sciy_x79_bus_%u.lock", temp_dir, (unsigned)getuid());
            } else {
                r = snprintf(buf, bufsize, "%s/ciy_x79_bus_%u.lock", temp_dir, (unsigned)getuid());
            }
            if (r > 0 && (size_t)r < bufsize) return buf;
            buf[0] = '\0';
            return NULL;
        }
    }
#endif
    const char *tmp = getenv("TMPDIR");
    if (tmp && tmp[0] == '/') {
        struct stat st;
        if (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == getuid()) {
            size_t len = strlen(tmp);
            if (len > 0 && tmp[len - 1] == '/') {
                r = snprintf(buf, bufsize, "%sciy_x79_bus_%u.lock", tmp, (unsigned)getuid());
            } else {
                r = snprintf(buf, bufsize, "%s/ciy_x79_bus_%u.lock", tmp, (unsigned)getuid());
            }
            if (r > 0 && (size_t)r < bufsize) return buf;
            buf[0] = '\0';
            return NULL;
        }
    }
    r = snprintf(buf, bufsize, "/tmp/ciy_x79_bus_%u.lock", (unsigned)getuid());
    if (r > 0 && (size_t)r < bufsize) return buf;
    buf[0] = '\0';
    return NULL;
}

static int ciy_open_lock_file(void) {
    char user_path[PATH_MAX > 1024 ? PATH_MAX : 1024];
    if (!ciy_bus_lock_path(user_path, sizeof(user_path))) {
        return -1;
    }
    int fd = open(user_path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) {
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_uid != getuid() || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }
    fchmod(fd, 0600);
#if defined(CIY_ENABLE_TEST_HOOKS)
    if (getenv("CIY_TESTING") != NULL) {
        strncpy(s_test_lock_path, user_path, sizeof(s_test_lock_path) - 1);
        s_test_lock_path[sizeof(s_test_lock_path) - 1] = '\0';
    }
#endif
    return fd;
}

bool ciy_bus_lock(void) {
#if defined(CIY_ENABLE_TEST_HOOKS)
    if (getenv("CIY_FAIL_ON_HARDWARE_IO") != NULL) {
        fprintf(stderr, "SECURITY GUARD ASSERTION: ciy_bus_lock called while CIY_FAIL_ON_HARDWARE_IO is set!\n");
        abort();
    }
#endif
    pthread_once(&s_bus_lock_init_once, ciy_bus_lock_init_handlers);
    pthread_mutex_lock(&s_bus_mutex);
    if (atomic_load_explicit(&s_bus_lock_depth, memory_order_acquire) == 0) {
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
                atomic_store_explicit(&s_bus_lock_depth, 0, memory_order_release);
                pthread_mutex_unlock(&s_bus_mutex);
                return false;
            }
            ciy_nanosleep_uninterrupted(5000000L); // 5 ms Pause, guarantees full duration under EINTR
        }
#if defined(CIY_ENABLE_TEST_HOOKS)
        if (getenv("CIY_TESTING") == NULL)
#endif
        {
            struct timespec last_ts = {0, 0}, now_ts = {0, 0};
            if (pread(s_bus_lock_fd, &last_ts, sizeof(last_ts), 0) == (ssize_t)sizeof(last_ts)) {
                clock_gettime(CLOCK_MONOTONIC, &now_ts);
                if (now_ts.tv_sec >= last_ts.tv_sec && (now_ts.tv_sec - last_ts.tv_sec) <= 1) {
                    long diff_ns = (now_ts.tv_sec - last_ts.tv_sec) * 1000000000L + (now_ts.tv_nsec - last_ts.tv_nsec);
                    if (diff_ns > 0 && diff_ns < 50000000L) {
                        ciy_nanosleep_uninterrupted(50000000L - diff_ns);
                    }
                }
            }
        }
        s_bus_lock_owner = pthread_self();
        atomic_store_explicit(&s_bus_has_owner, true, memory_order_release);
    }
    atomic_fetch_add_explicit(&s_bus_lock_depth, 1, memory_order_acq_rel);
    return true;
}

void ciy_bus_unlock(void) {
    if (!atomic_load_explicit(&s_bus_has_owner, memory_order_acquire) ||
        !pthread_equal(s_bus_lock_owner, pthread_self()) ||
        atomic_load_explicit(&s_bus_lock_depth, memory_order_acquire) <= 0) {
        return; // unbalancierter Aufruf oder fremder Thread: Mutex nicht über-entsperren
    }
    if (atomic_fetch_sub_explicit(&s_bus_lock_depth, 1, memory_order_acq_rel) == 1) {
        atomic_store_explicit(&s_bus_has_owner, false, memory_order_release);
        if (s_bus_lock_fd >= 0) {
            struct timespec end_ts = {0, 0};
            clock_gettime(CLOCK_MONOTONIC, &end_ts);
            (void)pwrite(s_bus_lock_fd, &end_ts, sizeof(end_ts), 0);
            flock(s_bus_lock_fd, LOCK_UN);
            close(s_bus_lock_fd);
            s_bus_lock_fd = -1;
        }
    }
    pthread_mutex_unlock(&s_bus_mutex);
}

static bool ciy_request(CIYDevice *d, bool out, uint8_t bRequest, uint8_t reportId, uint8_t *buf, uint16_t len,
                        uint32_t *done) {
#if defined(CIY_ENABLE_TEST_HOOKS)
    if (getenv("CIY_FAIL_ON_HARDWARE_IO") != NULL) {
        fprintf(stderr, "SECURITY GUARD ASSERTION: ciy_request called while CIY_FAIL_ON_HARDWARE_IO is set!\n");
        abort();
    }
    if (!d || !d->dev) return false;
    if (d->dev == (void *)CIY_TESTING_MOCK_DEV) {
        if (!out && getenv("CIY_TESTING_FAIL_GET_REPORTS") != NULL) return false;
        if (!out && reportId == 0x0A && buf && len >= CIY_REPORT_0A_SIZE) {
            memset(buf, 0, len);
            buf[0] = 0x0A;
            buf[1] = 19;
            buf[2] = 0;
            buf[3] = 0;
            buf[4] = 5;
        }
        if (done) *done = len;
        return true;
    }
#else
    if (!d || !d->dev) return false;
#endif
    return ciy_backend_request(d, out, bRequest, reportId, buf, len, done);
}

bool ciy_get_state(CIYDevice *d, CIYLightingState *out) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!d) return false;
    uint8_t buf[CIY_REPORT_0A_SIZE] = {CIY_REPORT_0A_ID};
    uint32_t done = 0;
    if (!ciy_bus_lock()) return false;
    bool ok = ciy_request(d, false, 0x01 /* GET_REPORT */, CIY_REPORT_0A_ID, buf, sizeof(buf), &done);
    ciy_bus_unlock();
    if (!ok) return false;
    return ciy_protocol_decode_0a(buf, (size_t)done, out);
}

bool ciy_set_state(CIYDevice *d, CIYLightingState st) {
    if (!d) return false;
    uint8_t buf[CIY_REPORT_0A_SIZE];
    if (!ciy_protocol_encode_0a(st, buf)) {
        return false;
    }
    if (!ciy_bus_lock()) return false;
    bool ok = ciy_request(d, true, 0x09 /* SET_REPORT */, CIY_REPORT_0A_ID, buf, sizeof(buf), NULL);
    ciy_bus_unlock();
    return ok;
}

bool ciy_send_planar_rgb(CIYDevice *d, const uint8_t *rgb, size_t length) {
    if (!d) return false;
    uint8_t buf[CIY_REPORT_0B_SIZE];
    if (!ciy_protocol_encode_0b(rgb, length, buf)) {
        return false;
    }
    if (!ciy_bus_lock()) return false;
    bool ok = ciy_request(d, true, 0x09, CIY_REPORT_0B_ID, buf, sizeof(buf), NULL);
    ciy_bus_unlock();
    return ok;
}

bool ciy_execute_transfer_plan(CIYDevice *d, const CIYTransferPlan *plan,
                               CIYCancelCheck cancel_check, void *cancel_ctx) {
    if (!d || !d->dev || !plan || plan->step_count == 0 || plan->step_count > CIY_MAX_PLAN_STEPS) return false;
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
            uint64_t chunks = ((uint64_t)ms + 4ULL) / 5ULL;
            for (uint64_t c = 0; c < chunks; c++) {
                ciy_nanosleep_uninterrupted(5 * 1000 * 1000L);
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
    if (!rgb) return;
    memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    ciy_build_profile_apply(rgb, indices, count, red, green, baseBlue);
}

static void ciy_build_profile_apply(uint8_t rgb[CIY_PLANAR_RGB_SIZE], const int *indices, int count,
                                    bool red, bool green, bool baseBlue) {
    if (!rgb) return;
    uint8_t *r = &rgb[0], *g = &rgb[CIY_LED_COUNT], *b = &rgb[2 * CIY_LED_COUNT];
    if (baseBlue) memset(b, 0xFF, CIY_LED_COUNT);
    if (!indices || count <= 0) return;
    for (int k = 0; k < count; k++) {
        int idx = indices[k];
        if (idx < 0 || idx >= CIY_LED_COUNT) continue;
        b[idx] = 0x00;
        if (red)   r[idx] = 0xFF;
        if (green) g[idx] = 0xFF;
    }
}

void ciy_build_wasd_and_arrows_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], bool baseBlue) {
    if (!rgb) return;
    memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    const int wasd_idx[] = {15, 10, 16, 22};
    const int arrow_idx[] = {89, 84, 90, 96};
    ciy_build_profile_apply(rgb, wasd_idx, 4, true, false, baseBlue);
    ciy_build_profile_apply(rgb, arrow_idx, 4, true, true, false);
}

void ciy_build_solid_color_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE], uint8_t r, uint8_t g, uint8_t b) {
    if (!rgb) return;
    memset(&rgb[0], r, CIY_LED_COUNT);
    memset(&rgb[CIY_LED_COUNT], g, CIY_LED_COUNT);
    memset(&rgb[2 * CIY_LED_COUNT], b, CIY_LED_COUNT);
}

void ciy_build_all_red_profile(uint8_t rgb[CIY_PLANAR_RGB_SIZE]) {
    if (!rgb) return;
    ciy_build_solid_color_profile(rgb, 0xFF, 0x00, 0x00);
}

#include "ciy_json.c"

char *ciy_read_text_file_with_result(const char *path, size_t *out_size, CIYValidationResult *out_res) {
    if (out_size) *out_size = 0;
    if (out_res) {
        memset(out_res, 0, sizeof(*out_res));
    }
    if (!path) {
        if (out_res) {
            out_res->read_error = CIYFileReadErrorNullPath;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Dateipfad ist NULL");
        }
        return NULL;
    }

    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (out_res) {
            out_res->read_error = (errno == ELOOP) ? CIYFileReadErrorSymlinkRejected
                                                   : CIYFileReadErrorOpenFailed;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' existiert nicht oder konnte nicht geöffnet werden", path);
        }
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        if (out_res) {
            out_res->read_error = CIYFileReadErrorNotRegularFile;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' ist keine reguläre Datei", path);
        }
        return NULL;
    }
    if (st.st_size <= 0) {
        close(fd);
        if (out_res) {
            out_res->read_error = CIYFileReadErrorEmptyFile;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' ist leer (0 Bytes)", path);
        }
        return NULL;
    }
    if (st.st_size > 512 * 1024) {
        close(fd);
        if (out_res) {
            out_res->read_error = CIYFileReadErrorTooLarge;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' überschreitet maximale Größe (512 KB)", path);
        }
        return NULL;
    }

    FILE *f = fdopen(fd, "rb");
    if (!f) {
        close(fd);
        if (out_res) {
            out_res->read_error = CIYFileReadErrorStreamFailed;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "file");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei '%s' konnte nicht geöffnet werden", path);
        }
        return NULL;
    }

    size_t expected_size = (size_t)st.st_size;
    char *buf = (char *)malloc(expected_size + 2);
    if (!buf) {
        fclose(f);
        if (out_res) {
            out_res->read_error = CIYFileReadErrorOutOfMemory;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "memory");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Speicherallokationsfehler");
        }
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, expected_size + 1, f);
    bool io_err = (ferror(f) != 0);
    struct stat st_after;
    bool size_changed = (fstat(fd, &st_after) != 0 || st_after.st_size != st.st_size);
    fclose(f);
    if (io_err || size_changed || read_bytes != expected_size || memchr(buf, '\0', read_bytes) != NULL) {
        free(buf);
        if (out_res) {
            out_res->read_error = CIYFileReadErrorReadFailed;
            snprintf(out_res->error_field, sizeof(out_res->error_field), "syntax");
            snprintf(out_res->error_message, sizeof(out_res->error_message), "Datei enthält ungültige Null-Bytes oder Lesefehler");
        }
        return NULL;
    }
    buf[read_bytes] = '\0';
    if (out_size) *out_size = read_bytes;
    return buf;
}

char *ciy_read_text_file(const char *path, size_t *out_size) {
    return ciy_read_text_file_with_result(path, out_size, NULL);
}

bool ciy_validate_profile_file(const char *path, CIYValidationResult *out_res) {
    if (out_res) {
        memset(out_res, 0, sizeof(*out_res));
    }
    char *buf = ciy_read_text_file_with_result(path, NULL, out_res);
    if (!buf) return false;

    bool ok = ciy_validate_profile_json(buf, out_res);
    free(buf);
    return ok;
}

bool ciy_load_profile_file_with_brightness(const char *path, const char *target_profile,
                                           uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                                           char *out_name, size_t max_name_len,
                                           int *out_slots_count,
                                           uint8_t *out_brightness) {
    if (rgb) memset(rgb, 0, CIY_PLANAR_RGB_SIZE);
    if (out_name && max_name_len > 0) out_name[0] = '\0';
    if (out_slots_count) *out_slots_count = 0;
    if (out_brightness) *out_brightness = CIY_MAX_BRIGHTNESS;
    if (!path || !rgb) return false;

    char *buf = ciy_read_text_file(path, NULL);
    if (!buf) return false;

    if (!ciy_validate_profile_json(buf, NULL)) {
        free(buf);
        return false;
    }

    bool ok = ciy_parse_profile_json_with_brightness(buf, target_profile, rgb, out_name, max_name_len, out_slots_count, out_brightness);
    free(buf);
    return ok;
}

bool ciy_load_profile_file(const char *path, const char *target_profile,
                           uint8_t rgb[CIY_PLANAR_RGB_SIZE],
                           char *out_name, size_t max_name_len,
                           int *out_slots_count) {
    return ciy_load_profile_file_with_brightness(path, target_profile, rgb, out_name, max_name_len, out_slots_count, NULL);
}

#if defined(CIY_ENABLE_TEST_HOOKS)
void ciy_test_nanosleep_uninterrupted(long ns) {
    ciy_nanosleep_uninterrupted(ns);
}
#endif
