#import "CIYBatteryMonitor.h"
#import <IOKit/ps/IOPowerSources.h>

@interface CIYBatteryMonitor () {
    CFRunLoopSourceRef _runLoopSource;
    NSTimer *_debounceTimer;
    int _lastReportedPercent;
    BOOL _lastReportedCharging;
    BOOL _hasReportedInitial;
    BOOL _isMonitoring;
}
- (void)scheduleDebouncedUpdate;
- (void)handleDebouncedUpdate;
@end

static void PowerSourceCallback(void *context) {
    CIYBatteryMonitor *monitor = (__bridge CIYBatteryMonitor *)context;
    [monitor scheduleDebouncedUpdate];
}

@implementation CIYBatteryMonitor

+ (instancetype)sharedMonitor {
    static CIYBatteryMonitor *s_shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        s_shared = [[CIYBatteryMonitor alloc] init];
    });
    return s_shared;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _preferredSource = CIY_BATTERY_SOURCE_INTERNAL;
        _lastReportedPercent = -999;
        _lastReportedCharging = NO;
        _hasReportedInitial = NO;
        _isMonitoring = NO;
    }
    return self;
}

- (BOOL)isMonitoring {
    return _isMonitoring;
}

- (CIYBatteryInfo)currentInfo {
    CIYBatteryInfo info;
    ciy_battery_get_info(_preferredSource, &info);
    return info;
}

- (void)startMonitoring {
    if (_isMonitoring) return;
    _isMonitoring = YES;

    _lastReportedPercent = -999;
    _lastReportedCharging = NO;
    _hasReportedInitial = NO;

#if defined(__APPLE__) && !defined(CIY_NO_IOKIT)
    _runLoopSource = IOPSNotificationCreateRunLoopSource(PowerSourceCallback, (__bridge void *)self);
    if (_runLoopSource) {
        CFRunLoopAddSource(CFRunLoopGetMain(), _runLoopSource, kCFRunLoopCommonModes);
    }
#endif

    // Sofortige erste Prüfung durchführen
    [self handleDebouncedUpdate];
}

- (void)stopMonitoring {
    _isMonitoring = NO;

    if (_debounceTimer) {
        [_debounceTimer invalidate];
        _debounceTimer = nil;
    }

#if defined(__APPLE__) && !defined(CIY_NO_IOKIT)
    if (_runLoopSource) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), _runLoopSource, kCFRunLoopCommonModes);
        CFRelease(_runLoopSource);
        _runLoopSource = NULL;
    }
#endif
}

- (void)scheduleDebouncedUpdate {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!self.isMonitoring) return;
        [self->_debounceTimer invalidate];
        self->_debounceTimer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                                target:self
                                                              selector:@selector(handleDebouncedUpdate)
                                                              userInfo:nil
                                                               repeats:NO];
    });
}

- (void)handleDebouncedUpdate {
    if (_debounceTimer) {
        [_debounceTimer invalidate];
        _debounceTimer = nil;
    }
    if (!self.isMonitoring) return;

    CIYBatteryInfo info = [self currentInfo];

    // Delta-Filter: Nur auslösen, wenn sich der Prozentwert oder Ladezustand tatsächlich geändert hat
    if (_hasReportedInitial &&
        info.percent == _lastReportedPercent &&
        info.is_charging == _lastReportedCharging) {
        return;
    }

    _lastReportedPercent = info.percent;
    _lastReportedCharging = info.is_charging;
    _hasReportedInitial = YES;

    if (self.updateHandler) {
        self.updateHandler(info);
    }
}

- (void)dealloc {
    [self stopMonitoring];
}

@end
