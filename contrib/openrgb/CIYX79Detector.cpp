#include "CIYController.h"
#include "RGBController_CIYX79.h"
#include <cstring>

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

void DetectCIYX79Controllers(hid_device_info* info, const std::string& name)
{
    (void)name;
    // 1. Strikte Vorfilterung auf VID, PID und Interface 1
    if (!info || !info->path || info->vendor_id != CIY_VENDOR_ID || info->product_id != CIY_PRODUCT_ID)
    {
        return;
    }
    if (info->interface_number != CIY_INTERFACE_NUM)
    {
        return;
    }

    // 2. HID-Handle öffnen
    hid_device* dev = hid_open_path(info->path);
    if (!dev)
    {
        return;
    }

    // 3. Nicht-destruktiver Handshake (Schutz vor Issue #2679 Sino Wealth Bricking):
    // Vor erfolgreicher Identifikation werden KEINERLEI Schreibzugriffe (SET_FEATURE/SET_REPORT) ausgeführt!
    // Wir fordern ausschließlich per GET_FEATURE Report 0x0A an.
    uint8_t buf[CIY_REPORT_0A_SIZE] = {CIY_REPORT_0A_ID};
    int res = hid_get_feature_report(dev, buf, sizeof(buf));

    // 4. Strikte Validierung der Firmware-Antwort gegen CIY X79 Spezifikationen:
    // Report-ID 0x0A, Modus 0..20, Farbpalette 0..10, Geschwindigkeit 0..4, Helligkeit 0..5
    if (res < 5 || buf[0] != CIY_REPORT_0A_ID ||
        buf[1] > CIY_MAX_MODE || buf[2] > CIY_MAX_COLOR ||
        buf[3] > CIY_MAX_SPEED || buf[4] > CIY_MAX_BRIGHTNESS)
    {
        // Unbekanntes Gerät oder inkompatible Sino Wealth Firmware -> sofort freigeben
        hid_close(dev);
        return;
    }

    // 5. Gerät verifiziert: Controller instanziieren und bei OpenRGB registrieren
    CIYController* controller = new CIYController(dev, info->path);
    RGBController_CIYX79* rgb_controller = new RGBController_CIYX79(controller);
    ResourceManager::get()->RegisterRGBController(rgb_controller);
}
