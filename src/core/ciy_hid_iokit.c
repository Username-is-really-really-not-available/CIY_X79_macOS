// IOKit USB Backend für CIY X79 auf macOS
#include "ciy_hid.h"

#if defined(__APPLE__) && !defined(CIY_BACKEND_HIDAPI)

static bool ciy_backend_open(CIYDevice *d) {
    CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchingDict) return false;
    int vid = CIY_VENDOR_ID, pid = CIY_PRODUCT_ID;
    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    if (!vidRef || !pidRef) {
        if (vidRef) CFRelease(vidRef);
        if (pidRef) CFRelease(pidRef);
        CFRelease(matchingDict);
        return false;
    }
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

static void ciy_backend_close(CIYDevice *d) {
    if (d && d->dev) {
        (*d->dev)->Release(d->dev);
        d->dev = NULL;
    }
}

static bool ciy_backend_request(CIYDevice *d, bool out, uint8_t bRequest, uint8_t reportId,
                                uint8_t *buf, uint16_t len, uint32_t *done) {
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

#endif
