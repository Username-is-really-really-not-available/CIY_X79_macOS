#import <Cocoa/Cocoa.h>
#import "../core/ciy_hid.h"

NS_ASSUME_NONNULL_BEGIN

typedef CIYLightingState KeyboardLightingState;

@interface KeyboardBackend : NSObject

/// Thread-sicher: alle Zugriffe auf das Gerät sind synchronisiert und dürfen von Hintergrund-Threads erfolgen.
@property (atomic, readonly) BOOL isConnected;
@property (atomic, readonly) KeyboardLightingState currentState;

/// Wird auf dem Main-Thread gepostet, wenn die Tastatur an- oder abgesteckt wurde.
/// userInfo[@"connected"] ist ein NSNumber (BOOL).
extern NSNotificationName const KeyboardBackendHotplugNotification;

+ (instancetype)shared;

/// Startet die Hotplug-Überwachung (IOKit-Matching-Notifications). Mehrfache Aufrufe sind harmlos.
- (void)startMonitoring;

/// Beendet die Hotplug-Überwachung und gibt alle IOKit-Ports und Iteratoren frei.
- (void)stopMonitoring;

- (BOOL)connect;
- (void)disconnect;
- (BOOL)readCurrentState;
- (BOOL)sendKeyColors:(NSDictionary<NSNumber *, NSColor *> *)keyColors defaultColor:(nullable NSColor *)defaultColor;
- (BOOL)sendKeyColors:(NSDictionary<NSNumber *, NSColor *> *)keyColors brightness:(uint8_t)brightness defaultColor:(nullable NSColor *)defaultColor;

@end

NS_ASSUME_NONNULL_END
