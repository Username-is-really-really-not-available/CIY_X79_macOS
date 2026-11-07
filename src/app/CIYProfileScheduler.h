#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_OPTIONS(NSUInteger, CIYScheduleDaysMask) {
    CIYScheduleDayMonday    = 1 << 0, // 1
    CIYScheduleDayTuesday   = 1 << 1, // 2
    CIYScheduleDayWednesday = 1 << 2, // 4
    CIYScheduleDayThursday  = 1 << 3, // 8
    CIYScheduleDayFriday    = 1 << 4, // 16
    CIYScheduleDaySaturday  = 1 << 5, // 32
    CIYScheduleDaySunday    = 1 << 6, // 64
    CIYScheduleDaysWorkdays = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4), // 31
    CIYScheduleDaysWeekend  = (1 << 5) | (1 << 6),                                  // 96
    CIYScheduleDaysAll      = 0x7F                                                  // 127
};

FOUNDATION_EXPORT NSString * const kCIYScheduleEnabledDefaultsKey;
FOUNDATION_EXPORT NSString * const kCIYProfileScheduleEnabledKey;
FOUNDATION_EXPORT NSString * const kCIYProfileScheduleHourKey;
FOUNDATION_EXPORT NSString * const kCIYProfileScheduleMinuteKey;
FOUNDATION_EXPORT NSString * const kCIYProfileScheduleDaysMaskKey;

@class CIYProfileScheduler;

@protocol CIYProfileSchedulerDelegate <NSObject>
@optional
- (void)scheduler:(CIYProfileScheduler *)scheduler didDetermineTargetProfileId:(nullable NSString *)profileId;
@end

@interface CIYProfileScheduler : NSObject

@property (nonatomic, weak, nullable) id<CIYProfileSchedulerDelegate> delegate;
@property (nonatomic, assign) BOOL globalEnabled;
@property (nonatomic, copy, nullable) NSArray<NSDictionary *> * (^profilesProvider)(void);
@property (nonatomic, copy, nullable) NSDate * (^nowProvider)(void);
@property (nonatomic, strong, null_resettable) NSCalendar *calendar;
@property (nonatomic, readonly) BOOL isRunning;

// Sanitizing & Formatierung
+ (void)sanitizeScheduleInProfile:(NSMutableDictionary *)profile;
+ (NSString *)formattedTimeStringForHour:(NSInteger)hour minute:(NSInteger)minute;
+ (nullable NSString *)shortScheduleSummaryForProfile:(NSDictionary *)profile;

// Deterministische Berechnungsfunktionen (ohne Seiteneffekte / injizierbare Zeit)
- (nullable NSString *)activeScheduledProfileIdAtDate:(NSDate *)now;
- (nullable NSDate *)nextSwitchDateAfterDate:(NSDate *)now;

// Convenience-Methoden für aktuelle Zeit
- (nullable NSString *)activeScheduledProfileId;
- (nullable NSDate *)nextSwitchDate;

// Lebenszyklus
- (void)start;
- (void)stop;
- (void)reevaluate;
- (void)rescheduleTimerOnly;

// Test-Hook zum synchronen Auslösen des aktuellen Timers ohne RunLoop-Warten
- (void)fireTimerForTesting;

@end

NS_ASSUME_NONNULL_END
