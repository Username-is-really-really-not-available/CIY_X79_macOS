#import "KeyboardBackend.h"
#include <stdlib.h>

NSNotificationName const KeyboardBackendHotplugNotification = @"KeyboardBackendHotplugNotification";

static BOOL sTestingMode = NO;
static NSData *sLastSentPlanarPayload = nil;

@interface KeyboardBackend () {
    CIYDevice _device;
    IONotificationPortRef _notifyPort;
    io_iterator_t _matchedIter;
    io_iterator_t _terminatedIter;
}
@property (atomic, readwrite) BOOL isConnected;
@property (atomic, readwrite) KeyboardLightingState currentState;
@end

@implementation KeyboardBackend

+ (instancetype)shared {
    static KeyboardBackend *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[KeyboardBackend alloc] init];
    });
    return instance;
}

#pragma mark - Testing Mode Support

+ (void)setTestingMode:(BOOL)enabled {
    @synchronized ([KeyboardBackend class]) {
        sTestingMode = enabled;
    }
}

+ (BOOL)isTestingMode {
    @synchronized ([KeyboardBackend class]) {
        if (sTestingMode) {
            return YES;
        }
        return (getenv("CIY_TESTING") != NULL);
    }
}

+ (nullable NSData *)lastSentPlanarPayload {
    @synchronized ([KeyboardBackend class]) {
        return [sLastSentPlanarPayload copy];
    }
}

+ (void)resetTestingPayload {
    @synchronized ([KeyboardBackend class]) {
        sLastSentPlanarPayload = nil;
    }
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _device.dev = NULL;
        _isConnected = NO;
        _currentState = (KeyboardLightingState){19, 0, 1, 5}; // Default: Mode 19 (Customize), Farbe Rot
    }
    return self;
}

- (void)dealloc {
    ciy_close(&_device);
    if (_matchedIter) IOObjectRelease(_matchedIter);
    if (_terminatedIter) IOObjectRelease(_terminatedIter);
    if (_notifyPort) {
        IONotificationPortSetDispatchQueue(_notifyPort, NULL);
        IONotificationPortDestroy(_notifyPort);
    }
}

#pragma mark - Hotplug

static void HotplugCallback(void *refCon, io_iterator_t iterator) {
    BOOL any = NO;
    io_object_t obj;
    while ((obj = IOIteratorNext(iterator))) { // Iterator leeren, sonst kommen keine weiteren Events
        IOObjectRelease(obj);
        any = YES;
    }
    if (!any) return;
    // Die Callbacks unterscheiden sich über refCon: NULL = abgesteckt, nicht NULL = angesteckt.
    BOOL connected = (refCon != NULL);
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:KeyboardBackendHotplugNotification
                                                            object:[KeyboardBackend shared]
                                                          userInfo:@{@"connected": @(connected)}];
    });
}

- (void)startMonitoring {
    @synchronized (self) {
        if (_notifyPort) return;
        _notifyPort = IONotificationPortCreate(kIOMainPortDefault);
        if (!_notifyPort) return;
        IONotificationPortSetDispatchQueue(_notifyPort, dispatch_get_main_queue());

        for (int i = 0; i < 2; i++) {
            BOOL matched = (i == 0);
            // Jede Registrierung verbraucht ihr eigenes matchingDict.
            CFMutableDictionaryRef dict = IOServiceMatching(kIOUSBDeviceClassName);
            if (!dict) continue;
            int vid = CIY_VENDOR_ID, pid = CIY_PRODUCT_ID;
            CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
            CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
            CFDictionarySetValue(dict, CFSTR(kUSBVendorID), vidRef);
            CFDictionarySetValue(dict, CFSTR(kUSBProductID), pidRef);
            CFRelease(vidRef);
            CFRelease(pidRef);

            io_iterator_t iter = 0;
            kern_return_t kr = IOServiceAddMatchingNotification(_notifyPort,
                matched ? kIOFirstMatchNotification : kIOTerminatedNotification,
                dict, HotplugCallback, matched ? (void *)1 : NULL, &iter);
            if (kr == KERN_SUCCESS && iter) {
                // Vorhandene Geräte durchlaufen, um die Benachrichtigung zu "armen".
                io_object_t obj;
                while ((obj = IOIteratorNext(iter))) IOObjectRelease(obj);
                if (matched) {
                    _matchedIter = iter;
                } else {
                    _terminatedIter = iter;
                }
            }
        }
    }
}

- (void)stopMonitoring {
    @synchronized (self) {
        if (_matchedIter) {
            IOObjectRelease(_matchedIter);
            _matchedIter = 0;
        }
        if (_terminatedIter) {
            IOObjectRelease(_terminatedIter);
            _terminatedIter = 0;
        }
        if (_notifyPort) {
            IONotificationPortSetDispatchQueue(_notifyPort, NULL);
            IONotificationPortDestroy(_notifyPort);
            _notifyPort = NULL;
        }
    }
}

// Alle öffentlichen Methoden synchronisieren auf self (rekursiv), damit sie aus
// Hintergrund-Threads aufgerufen werden können, ohne dass sich USB-Requests überlagern.

- (BOOL)connect {
    @synchronized (self) {
        if ([KeyboardBackend isTestingMode]) {
            self.isConnected = YES;
            return YES;
        }
        ciy_close(&_device);
        BOOL ok = ciy_open(&_device);
        self.isConnected = ok;
        if (ok) {
            KeyboardLightingState st;
            if (ciy_get_state(&_device, &st)) {
                self.currentState = st;
            }
        }
        return ok;
    }
}

- (void)disconnect {
    @synchronized (self) {
        if ([KeyboardBackend isTestingMode]) {
            self.isConnected = NO;
            return;
        }
        ciy_close(&_device);
        self.isConnected = NO;
    }
}

- (BOOL)readCurrentState {
    @synchronized (self) {
        if ([KeyboardBackend isTestingMode]) {
            return YES;
        }
        if (!_device.dev && ![self connect]) return NO;
        KeyboardLightingState st;
        if (!ciy_get_state(&_device, &st)) {
            return NO;
        }
        self.currentState = st;
        return YES;
    }
}

/// Report 0x0B, 50 ms Pause, danach Modus 19 (siehe README 2.B).
- (BOOL)applyProfile:(const uint8_t *)rgb brightness:(uint8_t)brightness {
    @synchronized (self) {
        if ([KeyboardBackend isTestingMode]) {
            @synchronized ([KeyboardBackend class]) {
                sLastSentPlanarPayload = [NSData dataWithBytes:rgb length:CIY_PLANAR_RGB_SIZE];
            }
            KeyboardLightingState st = self.currentState;
            st.mode = 19;
            st.color = 0;
            st.speed = 0;
            st.brightness = (brightness <= CIY_MAX_BRIGHTNESS) ? brightness : CIY_MAX_BRIGHTNESS;
            self.currentState = st;
            return YES;
        }

        if (!_device.dev && ![self connect]) return NO;
        if (!ciy_apply_custom_profile_with_brightness(&_device, rgb, CIY_PLANAR_RGB_SIZE, brightness)) {
            [self disconnect];
            return NO;
        }
        KeyboardLightingState st = self.currentState;
        st.mode = 19; st.color = 0; st.speed = 0; st.brightness = (brightness <= CIY_MAX_BRIGHTNESS) ? brightness : CIY_MAX_BRIGHTNESS;
        self.currentState = st;
        return YES;
    }
}

- (BOOL)sendKeyColors:(NSDictionary<NSNumber *, NSColor *> *)keyColors defaultColor:(nullable NSColor *)defaultColor {
    return [self sendKeyColors:keyColors brightness:CIY_MAX_BRIGHTNESS defaultColor:defaultColor];
}

- (BOOL)sendKeyColors:(NSDictionary<NSNumber *, NSColor *> *)keyColors brightness:(uint8_t)brightness defaultColor:(nullable NSColor *)defaultColor {
    uint8_t rgb[3 * CIY_LED_COUNT] = {0};
    uint8_t *plane0_r = &rgb[0];
    uint8_t *plane1_g = &rgb[CIY_LED_COUNT];
    uint8_t *plane2_b = &rgb[2 * CIY_LED_COUNT];

    CGFloat defR = 0, defG = 0, defB = 0, defA = 0;
    if (defaultColor) {
        NSColor *srgbDef = [defaultColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
        if (srgbDef) {
            [srgbDef getRed:&defR green:&defG blue:&defB alpha:&defA];
        }
    }

    for (int i = 0; i < CIY_LED_COUNT; i++) {
        plane0_r[i] = (uint8_t)(defR * 255.0 + 0.5);
        plane1_g[i] = (uint8_t)(defG * 255.0 + 0.5);
        plane2_b[i] = (uint8_t)(defB * 255.0 + 0.5);
    }

    for (NSNumber *slotNum in keyColors) {
        int slot = [slotNum intValue];
        // Gültige Slots: 0..CIY_LED_COUNT-2 (0..124), da der USB-Index (Slot + 1) höchstens CIY_LED_COUNT-1 sein darf.
        // Überlaufsichere Prüfung ohne vorzeitige Addition gegen Integer-Overflow bei INT_MAX
        if (slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
            int idx = slot + 1; // +1 offset (siehe README 2.C)
            NSColor *c = keyColors[slotNum];
            NSColor *srgb = [c colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
            if (srgb) {
                CGFloat r = 0, g = 0, b = 0, a = 0;
                [srgb getRed:&r green:&g blue:&b alpha:&a];
                plane0_r[idx] = (uint8_t)(r * 255.0 + 0.5);
                plane1_g[idx] = (uint8_t)(g * 255.0 + 0.5);
                plane2_b[idx] = (uint8_t)(b * 255.0 + 0.5);
            }
        }
    }

    return [self applyProfile:rgb brightness:brightness];
}

@end
