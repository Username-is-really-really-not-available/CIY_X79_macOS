#ifndef OPENRGB_MOCK_HIDAPI_H
#define OPENRGB_MOCK_HIDAPI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hid_device_;
typedef struct hid_device_ hid_device;

struct hid_device_info {
    char *path;
    unsigned short vendor_id;
    unsigned short product_id;
    wchar_t *serial_number;
    unsigned short release_number;
    wchar_t *manufacturer_string;
    wchar_t *product_string;
    unsigned short usage_page;
    unsigned short usage;
    int interface_number;
    struct hid_device_info *next;
};

int hid_init(void);
int hid_exit(void);
struct hid_device_info *hid_enumerate(unsigned short vendor_id, unsigned short product_id);
void hid_free_enumeration(struct hid_device_info *devs);
hid_device *hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number);
hid_device *hid_open_path(const char *path);
int hid_send_feature_report(hid_device *dev, const unsigned char *data, size_t length);
int hid_get_feature_report(hid_device *dev, unsigned char *data, size_t length);
void hid_close(hid_device *dev);

// Mock-Steuerung für Unit-Tests
void hid_mock_reset(void);
void hid_mock_set_device(unsigned short vid, unsigned short pid, int interface_num, const char *path);
void hid_mock_set_feature_report_response(const unsigned char *data, size_t len);
void hid_mock_set_fail_get(bool fail);
void hid_mock_set_fail_send(bool fail);
void hid_mock_set_init_fail(bool fail);
void hid_mock_set_open_fail(bool fail);
void hid_mock_set_enumerate_null_path(bool null_path);
size_t hid_mock_get_sent_count(void);
const unsigned char *hid_mock_get_last_sent(size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // OPENRGB_MOCK_HIDAPI_H
