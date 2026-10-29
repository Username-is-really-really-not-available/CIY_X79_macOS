#import "KeyboardBackend.h"
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <stdatomic.h>

NSNotificationName const KeyboardBackendHotplugNotification = @"KeyboardBackendHotplugNotification";

static BOOL sTestingMode = NO;
static NSData *sLastSentPlanarPayload = nil;

@interface KeyboardBackend () {
    CIYDevice _device;
    IONotificationPortRef _notifyPort;
    io_iterator_t _matchedIter;
    io_iterator_t _terminatedIter;
    _Atomic bool _cancelRequested;
    _Atomic bool _lastApplyCancelled;
}
@property (atomic, readwrite) BOOL isConnected;
@property (atomic, readwrite) KeyboardLightingState currentState;
@end

static bool KeyboardBackendCancelCheck(void *ctx) {
    return atomic_load_explicit((_Atomic bool *)ctx, memory_order_acquire);
}

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
        atomic_init(&_cancelRequested, false);
        atomic_init(&_lastApplyCancelled, false);
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
    if (!connected) {
        [[KeyboardBackend shared] requestCancel];
    }
    [[NSNotificationCenter defaultCenter] postNotificationName:KeyboardBackendHotplugNotification
                                                        object:[KeyboardBackend shared]
                                                      userInfo:@{@"connected": @(connected)}];
}

- (void)startMonitoring {
    @synchronized (self) {
        if (_notifyPort) return;

        IONotificationPortRef port = IONotificationPortCreate(kIOMainPortDefault);
        if (!port) return;
        IONotificationPortSetDispatchQueue(port, dispatch_get_main_queue());

        io_iterator_t matchedIter = 0;
        io_iterator_t terminatedIter = 0;
        BOOL ok = YES;

        for (int i = 0; i < 2; i++) {
            BOOL matched = (i == 0);
            // Jede Registrierung verbraucht ihr eigenes matchingDict.
            CFMutableDictionaryRef dict = IOServiceMatching(kIOUSBDeviceClassName);
            if (!dict) { ok = NO; break; }
            int vid = CIY_VENDOR_ID, pid = CIY_PRODUCT_ID;
            CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
            CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
            CFDictionarySetValue(dict, CFSTR(kUSBVendorID), vidRef);
            CFDictionarySetValue(dict, CFSTR(kUSBProductID), pidRef);
            CFRelease(vidRef);
            CFRelease(pidRef);

            io_iterator_t iter = 0;
            kern_return_t kr = IOServiceAddMatchingNotification(port,
                matched ? kIOFirstMatchNotification : kIOTerminatedNotification,
                dict, HotplugCallback, matched ? (void *)1 : NULL, &iter);
            if (kr != KERN_SUCCESS || !iter) { ok = NO; break; }

            // Vorhandene Geräte durchlaufen, um die Benachrichtigung zu "armen".
            io_object_t obj;
            while ((obj = IOIteratorNext(iter))) IOObjectRelease(obj);
            if (matched) {
                matchedIter = iter;
            } else {
                terminatedIter = iter;
            }
        }

        if (!ok) {
            if (matchedIter) IOObjectRelease(matchedIter);
            if (terminatedIter) IOObjectRelease(terminatedIter);
            IONotificationPortSetDispatchQueue(port, NULL);
            IONotificationPortDestroy(port);
            return;
        }

        _matchedIter = matchedIter;
        _terminatedIter = terminatedIter;
        _notifyPort = port;
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
        atomic_store(&_cancelRequested, false);
        atomic_store(&_lastApplyCancelled, false);
        if ([KeyboardBackend isTestingMode]) {
            self.isConnected = YES;
            return YES;
        }
        ciy_close(&_device);
        BOOL ok = ciy_open(&_device);
        if (ok) {
            KeyboardLightingState st;
            if (ciy_get_state(&_device, &st)) {
                self.currentState = st;
            } else {
                ciy_close(&_device);
                self.isConnected = NO;
                return NO;
            }
        }
        self.isConnected = ok;
        return ok;
    }
}

- (void)requestCancel {
    atomic_store_explicit(&_cancelRequested, true, memory_order_release);
    self.isConnected = NO;
}

- (void)invalidateDevice {
    @synchronized (self) {
        if (![KeyboardBackend isTestingMode]) {
            ciy_close(&_device);
        }
        self.isConnected = NO;
    }
}

- (void)disconnect {
    // Vor dem Lock setzen: ein laufendes applyProfile: bricht zwischen den Schritten ab,
    // damit dieser Aufruf nicht auf die volle 50-ms-Sequenz warten muss.
    atomic_store(&_cancelRequested, true);
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
        if (atomic_load(&_cancelRequested)) return NO;
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
        atomic_store(&_lastApplyCancelled, false);
        if (!rgb) return NO;
        if (atomic_load_explicit(&_cancelRequested, memory_order_acquire)) {
            atomic_store(&_lastApplyCancelled, true);
            return NO;
        }

        if ([KeyboardBackend isTestingMode]) {
            // Test-Hook: simuliert die 50-ms-Phase, damit In-Flight-Cancel testbar ist.
            const char *delayEnv = getenv("CIY_TESTING_APPLY_DELAY_MS");
            if (delayEnv) {
                int total = atoi(delayEnv);
                if (total > 5000) total = 5000;
                for (int waited = 0; waited < total; waited += 5) {
                    if (atomic_load_explicit(&_cancelRequested, memory_order_acquire)) {
                        atomic_store(&_lastApplyCancelled, true);
                        return NO;
                    }
                    usleep(5000);
                }
                if (atomic_load_explicit(&_cancelRequested, memory_order_acquire)) {
                    atomic_store(&_lastApplyCancelled, true);
                    return NO;
                }
            }
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

        if (_device.dev == NULL && ![self connect]) return NO;
        BOOL ok = ciy_apply_custom_profile_with_brightness_cancelable(
            &_device, rgb, CIY_PLANAR_RGB_SIZE, brightness,
            KeyboardBackendCancelCheck, &_cancelRequested);
        if (!ok) {
            if (atomic_load_explicit(&_cancelRequested, memory_order_acquire)) {
                atomic_store(&_lastApplyCancelled, true);
                return NO;
            }
            [self invalidateDevice];
            if ([self connect]) {
                ok = ciy_apply_custom_profile_with_brightness_cancelable(
                    &_device, rgb, CIY_PLANAR_RGB_SIZE, brightness,
                    KeyboardBackendCancelCheck, &_cancelRequested);
            }
            if (!ok) {
                if (atomic_load_explicit(&_cancelRequested, memory_order_acquire)) {
                    atomic_store(&_lastApplyCancelled, true);
                    return NO;
                }
                [self invalidateDevice];
                return NO;
            }
        }
        KeyboardLightingState st = self.currentState;
        st.mode = 19; st.color = 0; st.speed = 0; st.brightness = (brightness <= CIY_MAX_BRIGHTNESS) ? brightness : CIY_MAX_BRIGHTNESS;
        self.currentState = st;
        return YES;
    }
}

- (BOOL)lastOperationWasCancelled {
    return atomic_load_explicit(&_lastApplyCancelled, memory_order_acquire) ? YES : NO;
}

static inline uint8_t ClampColorChannel(CGFloat val) {
    if (val <= 0.0) return 0;
    if (val >= 1.0) return 255;
    return (uint8_t)round(val * 255.0);
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

    uint8_t defRu = ClampColorChannel(defR);
    uint8_t defGu = ClampColorChannel(defG);
    uint8_t defBu = ClampColorChannel(defB);

    for (int i = 0; i < CIY_LED_COUNT; i++) {
        plane0_r[i] = defRu;
        plane1_g[i] = defGu;
        plane2_b[i] = defBu;
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
                plane0_r[idx] = ClampColorChannel(r);
                plane1_g[idx] = ClampColorChannel(g);
                plane2_b[idx] = ClampColorChannel(b);
            }
        }
    }

    return [self applyProfile:rgb brightness:brightness];
}

@end
