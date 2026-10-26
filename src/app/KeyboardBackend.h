#import <Cocoa/Cocoa.h>
#import "../core/ciy_hid.h"

NS_ASSUME_NONNULL_BEGIN

typedef CIYLightingState KeyboardLightingState;

@interface KeyboardBackend : NSObject

/// Thread-sicher: alle Zugriffe auf das Gerät sind synchronisiert und dürfen von Hintergrund-Threads erfolgen.
@property (atomic, readonly) BOOL isConnected;
@property (atomic, readonly) KeyboardLightingState currentState;

/// YES, wenn der letzte Apply-Vorgang durch disconnect/Sleep abgebrochen wurde.
@property (atomic, readonly) BOOL lastOperationWasCancelled;

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

#pragma mark - Testing Mode Support

/// Aktiviert oder deaktiviert den hardwarefreien Testmodus.
/// Im Testmodus schlagen Verbindungen nicht fehl, Hardware-Schreibvorgänge werden simuliert
/// und der jeweils zuletzt gesendete 378-Byte Planar-RGB-Payload wird im Speicher gepuffert.
+ (void)setTestingMode:(BOOL)enabled;

/// Gibt zurück, ob der hardwarefreie Testmodus derzeit aktiv ist.
+ (BOOL)isTestingMode;

/// Liefert den zuletzt im Testmodus gesendeten 378-Byte Planar-RGB-Payload oder nil,
/// falls bisher kein Payload gesendet oder der Puffer zurückgesetzt wurde.
+ (nullable NSData *)lastSentPlanarPayload;

/// Setzt den gespeicherten Test-Payload zurück (löscht lastSentPlanarPayload).
+ (void)resetTestingPayload;

@end

NS_ASSUME_NONNULL_END
