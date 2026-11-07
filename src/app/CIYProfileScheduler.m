#import "CIYProfileScheduler.h"

NSString * const kCIYScheduleEnabledDefaultsKey  = @"CIYScheduleEnabled";
NSString * const kCIYProfileScheduleEnabledKey   = @"scheduleEnabled";
NSString * const kCIYProfileScheduleHourKey      = @"scheduleHour";
NSString * const kCIYProfileScheduleMinuteKey    = @"scheduleMinute";
NSString * const kCIYProfileScheduleDaysMaskKey  = @"scheduleDaysMask";

static inline NSUInteger CIYDayBitForWeekday(NSInteger weekday) {
    // NSCalendar Gregorian: 1 = Sonntag, 2 = Montag, ..., 7 = Samstag
    if (weekday == 1) return 6; // Sonntag -> Bit 6
    if (weekday >= 2 && weekday <= 7) return (NSUInteger)(weekday - 2); // Mo (2) -> 0 ... Sa (7) -> 5
    return 0;
}

@interface CIYProfileScheduler () {
    NSTimer *_timer;
    BOOL _isRunning;
    uint64_t _generation;
    NSCalendar *_customCalendar;
}
- (void)scheduleNextTimerAfterDate:(NSDate *)now generation:(uint64_t)currentGen;
@end

@implementation CIYProfileScheduler

@synthesize globalEnabled = _globalEnabled;

- (instancetype)init {
    self = [super init];
    if (self) {
        _globalEnabled = YES;
        _isRunning = NO;
        _generation = 0;
    }
    return self;
}

- (void)dealloc {
    [self stop];
}

- (void)setGlobalEnabled:(BOOL)globalEnabled {
    if (_globalEnabled == globalEnabled) return;
    _globalEnabled = globalEnabled;
    if (_isRunning) {
        [self rescheduleTimerOnly];
    }
}

- (BOOL)isRunning {
    return _isRunning;
}

- (NSCalendar *)calendar {
    return _customCalendar ?: [NSCalendar currentCalendar];
}

- (void)setCalendar:(NSCalendar *)calendar {
    _customCalendar = calendar;
}

- (NSDate *)currentDate {
    if (self.nowProvider) {
        return self.nowProvider();
    }
    return [NSDate date];
}

#pragma mark - Statische Hilfsmethoden

+ (void)sanitizeScheduleInProfile:(NSMutableDictionary *)profile {
    if (!profile || ![profile isKindOfClass:[NSMutableDictionary class]]) return;

    id enabledObj = profile[kCIYProfileScheduleEnabledKey];
    BOOL enabled = NO;
    if ([enabledObj isKindOfClass:[NSNumber class]]) {
        enabled = [enabledObj boolValue];
    }

    id hourObj = profile[kCIYProfileScheduleHourKey];
    NSInteger hour = 0;
    if ([hourObj respondsToSelector:@selector(integerValue)]) {
        hour = [hourObj integerValue];
    }
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;

    id minuteObj = profile[kCIYProfileScheduleMinuteKey];
    NSInteger minute = 0;
    if ([minuteObj respondsToSelector:@selector(integerValue)]) {
        minute = [minuteObj integerValue];
    }
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;

    id maskObj = profile[kCIYProfileScheduleDaysMaskKey];
    NSUInteger mask = CIYScheduleDaysAll;
    if ([maskObj respondsToSelector:@selector(unsignedIntegerValue)]) {
        mask = [maskObj unsignedIntegerValue] & CIYScheduleDaysAll;
    }
    if (mask == 0) {
        enabled = NO;
    }

    profile[kCIYProfileScheduleEnabledKey] = @(enabled);
    profile[kCIYProfileScheduleHourKey] = @(hour);
    profile[kCIYProfileScheduleMinuteKey] = @(minute);
    profile[kCIYProfileScheduleDaysMaskKey] = @(mask);
}

+ (NSString *)formattedTimeStringForHour:(NSInteger)hour minute:(NSInteger)minute {
    if (hour < 0) hour = 0; else if (hour > 23) hour = 23;
    if (minute < 0) minute = 0; else if (minute > 59) minute = 59;
    return [NSString stringWithFormat:@"%02ld:%02ld", (long)hour, (long)minute];
}

+ (nullable NSString *)shortScheduleSummaryForProfile:(NSDictionary *)profile {
    if (!profile || ![profile[kCIYProfileScheduleEnabledKey] boolValue]) return nil;
    NSInteger hour = [profile[kCIYProfileScheduleHourKey] integerValue];
    NSInteger min = [profile[kCIYProfileScheduleMinuteKey] integerValue];
    return [self formattedTimeStringForHour:hour minute:min];
}

#pragma mark - Deterministische Berechnung

- (nullable NSString *)activeScheduledProfileIdAtDate:(NSDate *)now {
    if (!_globalEnabled || !now) return nil;
    NSArray<NSDictionary *> *profiles = self.profilesProvider ? self.profilesProvider() : nil;
    if (!profiles || profiles.count == 0) return nil;

    NSCalendar *cal = self.calendar;
    NSDate *latestTriggerDate = nil;
    NSString *bestProfileId = nil;

    for (NSUInteger idx = 0; idx < profiles.count; idx++) {
        NSDictionary *prof = profiles[idx];
        if (![prof[kCIYProfileScheduleEnabledKey] boolValue]) continue;

        NSUInteger mask = [prof[kCIYProfileScheduleDaysMaskKey] unsignedIntegerValue] & CIYScheduleDaysAll;
        if (mask == 0) continue;

        NSInteger hour = [prof[kCIYProfileScheduleHourKey] integerValue];
        if (hour < 0) hour = 0; else if (hour > 23) hour = 23;

        NSInteger minute = [prof[kCIYProfileScheduleMinuteKey] integerValue];
        if (minute < 0) minute = 0; else if (minute > 59) minute = 59;

        // Bis zu 7 Tage in die Vergangenheit nach dem letzten fälligen Schaltzeitpunkt suchen
        NSDate *mostRecentForProfile = nil;
        for (NSInteger d = 0; d >= -7; d--) {
            NSDate *dayDate = (d == 0) ? now : [cal dateByAddingUnit:NSCalendarUnitDay value:d toDate:now options:0];
            if (!dayDate) continue;

            NSDateComponents *comp = [cal components:NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitWeekday fromDate:dayDate];
            NSUInteger bit = CIYDayBitForWeekday(comp.weekday);
            if ((mask & (1 << bit)) == 0) continue;

            comp.hour = hour;
            comp.minute = minute;
            comp.second = 0;
            comp.nanosecond = 0;
            NSDate *candidate = [cal dateFromComponents:comp];
            if (!candidate) continue;

            if ([candidate compare:now] != NSOrderedDescending) {
                // candidate <= now
                mostRecentForProfile = candidate;
                break;
            }
        }

        if (mostRecentForProfile) {
            if (!latestTriggerDate || [mostRecentForProfile compare:latestTriggerDate] == NSOrderedDescending) {
                latestTriggerDate = mostRecentForProfile;
                bestProfileId = prof[@"id"];
            }
            // Bei Gleichstand bleibt das frühere Profil im Array erhalten (niedrigere Profil-Reihenfolge)
        }
    }

    return bestProfileId;
}

- (nullable NSDate *)nextSwitchDateAfterDate:(NSDate *)now {
    if (!_globalEnabled || !now) return nil;
    NSArray<NSDictionary *> *profiles = self.profilesProvider ? self.profilesProvider() : nil;
    if (!profiles || profiles.count == 0) return nil;

    NSCalendar *cal = self.calendar;
    NSDate *earliestNext = nil;

    for (NSUInteger idx = 0; idx < profiles.count; idx++) {
        NSDictionary *prof = profiles[idx];
        if (![prof[kCIYProfileScheduleEnabledKey] boolValue]) continue;

        NSUInteger mask = [prof[kCIYProfileScheduleDaysMaskKey] unsignedIntegerValue] & CIYScheduleDaysAll;
        if (mask == 0) continue;

        NSInteger hour = [prof[kCIYProfileScheduleHourKey] integerValue];
        if (hour < 0) hour = 0; else if (hour > 23) hour = 23;

        NSInteger minute = [prof[kCIYProfileScheduleMinuteKey] integerValue];
        if (minute < 0) minute = 0; else if (minute > 59) minute = 59;

        // Bis zu 8 Tage in die Zukunft nach dem nächsten Schaltzeitpunkt suchen
        for (NSInteger d = 0; d <= 8; d++) {
            NSDate *dayDate = (d == 0) ? now : [cal dateByAddingUnit:NSCalendarUnitDay value:d toDate:now options:0];
            if (!dayDate) continue;

            NSDateComponents *comp = [cal components:NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitWeekday fromDate:dayDate];
            NSUInteger bit = CIYDayBitForWeekday(comp.weekday);
            if ((mask & (1 << bit)) == 0) continue;

            comp.hour = hour;
            comp.minute = minute;
            comp.second = 0;
            comp.nanosecond = 0;
            NSDate *candidate = [cal dateFromComponents:comp];
            if (!candidate) continue;

            // Strikt in der Zukunft (> now)
            if ([candidate compare:now] == NSOrderedDescending) {
                if (!earliestNext || [candidate compare:earliestNext] == NSOrderedAscending) {
                    earliestNext = candidate;
                }
                break;
            }
        }
    }

    return earliestNext;
}

- (nullable NSString *)activeScheduledProfileId {
    return [self activeScheduledProfileIdAtDate:[self currentDate]];
}

- (nullable NSDate *)nextSwitchDate {
    return [self nextSwitchDateAfterDate:[self currentDate]];
}

#pragma mark - Lebenszyklus & Timer

- (void)start {
    if (_isRunning) return;
    _isRunning = YES;

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(systemClockOrDayChanged:)
                                                 name:NSSystemClockDidChangeNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(systemClockOrDayChanged:)
                                                 name:NSCalendarDayChangedNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(systemClockOrDayChanged:)
                                                 name:NSSystemTimeZoneDidChangeNotification
                                               object:nil];

    [self reevaluate];
}

- (void)stop {
    _isRunning = NO;
    _generation++;
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSSystemClockDidChangeNotification
                                                  object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSCalendarDayChangedNotification
                                                  object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSSystemTimeZoneDidChangeNotification
                                                  object:nil];
}

- (void)systemClockOrDayChanged:(NSNotification *)note {
    (void)note;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_isRunning) {
            if (!self->_customCalendar) {
                [NSTimeZone resetSystemTimeZone];
            }
            [self reevaluate];
        }
    });
}

- (void)fireTimerForTesting {
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
    if (_isRunning) {
        [self reevaluate];
    }
}

- (void)scheduleNextTimerAfterDate:(NSDate *)now generation:(uint64_t)currentGen {
    if (!_isRunning || currentGen != _generation) {
        return;
    }

    if (!_globalEnabled) {
        return;
    }

    NSDate *nextDate = [self nextSwitchDateAfterDate:now];
    if (!nextDate) {
        return;
    }

    NSTimeInterval interval = [nextDate timeIntervalSinceDate:now];
    if (interval <= 0.0) {
        interval = 0.05;
    }

    __weak typeof(self) weakSelf = self;
    uint64_t expectedGen = currentGen;
    _timer = [NSTimer timerWithTimeInterval:interval
                                    repeats:NO
                                      block:^(NSTimer * _Nonnull timer) {
        (void)timer;
        __strong typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf || !strongSelf->_isRunning || expectedGen != strongSelf->_generation) {
            return;
        }
        [strongSelf reevaluate];
    }];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)reevaluate {
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
    if (!_isRunning) return;

    uint64_t currentGen = ++_generation;
    NSDate *now = [self currentDate];

    NSString *activeId = [self activeScheduledProfileIdAtDate:now];
    if ([self.delegate respondsToSelector:@selector(scheduler:didDetermineTargetProfileId:)]) {
        [self.delegate scheduler:self didDetermineTargetProfileId:activeId];
    }

    [self scheduleNextTimerAfterDate:now generation:currentGen];
}

- (void)rescheduleTimerOnly {
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
    if (!_isRunning) return;

    uint64_t currentGen = ++_generation;
    NSDate *now = [self currentDate];

    [self scheduleNextTimerAfterDate:now generation:currentGen];
}

@end
