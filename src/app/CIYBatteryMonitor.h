#import <Cocoa/Cocoa.h>
#import "../core/ciy_battery.h"

NS_ASSUME_NONNULL_BEGIN

typedef void (^CIYBatteryUpdateHandler)(CIYBatteryInfo info);

@interface CIYBatteryMonitor : NSObject

@property (nonatomic, readonly) BOOL isMonitoring;
@property (nonatomic, assign) CIYBatterySourceType preferredSource;
@property (nonatomic, copy, nullable) CIYBatteryUpdateHandler updateHandler;

+ (instancetype)sharedMonitor;

/// Startet die ereignisgesteuerte Akku-Überwachung via IOPSNotificationCreateRunLoopSource.
- (void)startMonitoring;

/// Beendet die Akku-Überwachung und entfernt die RunLoop-Quelle.
- (void)stopMonitoring;

/// Liest den aktuellen Zustand der ausgewählten Stromquelle aus.
- (CIYBatteryInfo)currentInfo;

@end

NS_ASSUME_NONNULL_END
