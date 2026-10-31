// POSIX / Linux HIDAPI Backend für CIY X79
#include "ciy_hid.h"

#if defined(CIY_BACKEND_HIDAPI) || (!defined(__APPLE__) && !defined(CIY_BACKEND_IOKIT))

#if defined(__has_include)
  #if __has_include(<hidapi/hidapi.h>)
    #include <hidapi/hidapi.h>
  #elif __has_include("hidapi.h")
    #include "hidapi.h"
  #else
    #include <hidapi.h>
  #endif
#else
  #include <hidapi/hidapi.h>
#endif

static bool ciy_backend_open(CIYDevice *d) {
    if (hid_init() != 0) {
        return false;
    }

    struct hid_device_info *devs = hid_enumerate(CIY_VENDOR_ID, CIY_PRODUCT_ID);
    if (!devs) {
        return false;
    }

    const char *target_path = NULL;
    struct hid_device_info *cur = devs;
    while (cur) {
        if (cur->interface_number == CIY_INTERFACE_NUM) {
            target_path = cur->path;
            break;
        }
        cur = cur->next;
    }

    hid_device *handle = NULL;
    if (target_path) {
        handle = hid_open_path(target_path);
    }
    hid_free_enumeration(devs);

    if (!handle) {
        return false;
    }

    d->dev = handle;
    return true;
}

static void ciy_backend_close(CIYDevice *d) {
    if (d && d->dev) {
        hid_close(d->dev);
        d->dev = NULL;
    }
}

static bool ciy_backend_request(CIYDevice *d, bool out, uint8_t bRequest, uint8_t reportId,
                                uint8_t *buf, uint16_t len, uint32_t *done) {
    (void)bRequest;
    if (!d || !d->dev || !buf || len == 0) return false;

    if (out) {
        // SET_FEATURE: Erstes Byte muss Report ID sein
        buf[0] = reportId;
        int res = hid_send_feature_report(d->dev, buf, len);
        if (res < 0) return false;
        if (done) *done = (uint32_t)res;
        return true;
    } else {
        // GET_FEATURE: Erstes Byte enthält angeforderte Report ID
        buf[0] = reportId;
        int res = hid_get_feature_report(d->dev, buf, len);
        if (res < 0) return false;
        if (done) *done = (uint32_t)res;
        return true;
    }
}

#endif
