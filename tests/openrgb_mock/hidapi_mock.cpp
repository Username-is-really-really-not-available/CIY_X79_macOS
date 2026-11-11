#include "hidapi.h"
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

static struct hid_device_info s_mock_info = {
    (char*)"/dev/hidraw1",
    0x258A,
    0x0049,
    nullptr,
    0,
    nullptr,
    nullptr,
    0,
    0,
    1, // Interface 1
    nullptr
};

static bool s_mock_fail_get = false;
static bool s_mock_fail_send = false;
static bool s_mock_fail_init = false;
static bool s_mock_fail_open = false;
static bool s_mock_enumerate_null_path = false;
static std::vector<uint8_t> s_mock_get_resp = {
    0x0A, 19, 0, 2, 5,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0
}; // Valid 0x0A report (42 bytes: Mode 19, Brightness 5)
static std::vector<std::vector<uint8_t>> s_mock_sent_reports;

struct hid_device_ {
    int dummy;
};

static hid_device s_dummy_dev;

extern "C" {

int hid_init(void) {
    return s_mock_fail_init ? -1 : 0;
}

int hid_exit(void) {
    return 0;
}

struct hid_device_info *hid_enumerate(unsigned short vendor_id, unsigned short product_id) {
    if (!s_mock_enumerate_null_path && !s_mock_info.path) return nullptr;
    if (vendor_id != 0 && vendor_id != s_mock_info.vendor_id) return nullptr;
    if (product_id != 0 && product_id != s_mock_info.product_id) return nullptr;
    struct hid_device_info *info = (struct hid_device_info *)calloc(1, sizeof(struct hid_device_info));
    info->path = s_mock_enumerate_null_path ? nullptr : strdup(s_mock_info.path);
    info->vendor_id = s_mock_info.vendor_id;
    info->product_id = s_mock_info.product_id;
    info->interface_number = s_mock_info.interface_number;
    info->next = nullptr;
    return info;
}

void hid_free_enumeration(struct hid_device_info *devs) {
    while (devs) {
        struct hid_device_info *next = devs->next;
        free(devs->path);
        free(devs);
        devs = next;
    }
}

hid_device *hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number) {
    (void)vendor_id; (void)product_id; (void)serial_number;
    if (s_mock_fail_open) return nullptr;
    return &s_dummy_dev;
}

hid_device *hid_open_path(const char *path) {
    if (!path || s_mock_fail_open) return nullptr;
    return &s_dummy_dev;
}

int hid_send_feature_report(hid_device *dev, const unsigned char *data, size_t length) {
    (void)dev;
    if (s_mock_fail_send) return -1;
    if (!data || length == 0) return -1;
    s_mock_sent_reports.emplace_back(data, data + length);
    return (int)length;
}

int hid_get_feature_report(hid_device *dev, unsigned char *data, size_t length) {
    (void)dev;
    if (s_mock_fail_get) return -1;
    if (!data || length == 0) return -1;
    size_t copy_len = std::min(length, s_mock_get_resp.size());
    memcpy(data, s_mock_get_resp.data(), copy_len);
    return (int)copy_len;
}

void hid_close(hid_device *dev) {
    (void)dev;
}

void hid_mock_reset(void) {
    s_mock_fail_get = false;
    s_mock_fail_send = false;
    s_mock_fail_init = false;
    s_mock_fail_open = false;
    s_mock_enumerate_null_path = false;
    s_mock_get_resp = {
        0x0A, 19, 0, 2, 5,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0
    };
    s_mock_sent_reports.clear();
    s_mock_info.path = (char*)"/dev/hidraw1";
    s_mock_info.vendor_id = 0x258A;
    s_mock_info.product_id = 0x0049;
    s_mock_info.interface_number = 1;
}

void hid_mock_set_device(unsigned short vid, unsigned short pid, int interface_num, const char *path) {
    s_mock_info.vendor_id = vid;
    s_mock_info.product_id = pid;
    s_mock_info.interface_number = interface_num;
    if (path) s_mock_info.path = (char*)path;
}

void hid_mock_set_feature_report_response(const unsigned char *data, size_t len) {
    s_mock_get_resp.assign(data, data + len);
}

void hid_mock_set_fail_get(bool fail) {
    s_mock_fail_get = fail;
}

void hid_mock_set_fail_send(bool fail) {
    s_mock_fail_send = fail;
}

void hid_mock_set_init_fail(bool fail) {
    s_mock_fail_init = fail;
}

void hid_mock_set_open_fail(bool fail) {
    s_mock_fail_open = fail;
}

void hid_mock_set_enumerate_null_path(bool null_path) {
    s_mock_enumerate_null_path = null_path;
}

size_t hid_mock_get_sent_count(void) {
    return s_mock_sent_reports.size();
}

const unsigned char *hid_mock_get_last_sent(size_t *out_len) {
    if (s_mock_sent_reports.empty()) {
        if (out_len) *out_len = 0;
        return nullptr;
    }
    const auto& last = s_mock_sent_reports.back();
    if (out_len) *out_len = last.size();
    return last.data();
}

}
