#import <Cocoa/Cocoa.h>
#import "CIYKeyboardView.h"
#import "KeyboardBackend.h"

NS_ASSUME_NONNULL_BEGIN

@interface AppDelegate : NSObject <NSApplicationDelegate, CIYKeyboardViewDelegate>

@property (nonatomic, strong) NSWindow *window;

@end

#if defined(CIY_ENABLE_TEST_HOOKS)
@class CIYProfileScheduler;

@interface AppDelegate (Testing)
- (instancetype)initForTestingWithKeyboardView:(nullable CIYKeyboardView *)keyboardView;
- (void)ciy_triggerSystemWillSleep;
- (void)ciy_triggerSystemDidWake;
- (void)ciy_triggerAppWillTerminate;
- (BOOL)ciy_isSleeping;
- (BOOL)ciy_isWakingUp;
- (BOOL)ciy_isWakeSyncScheduled;
- (void)ciy_scheduleHardwareSyncWithDelay:(NSTimeInterval)delay;
- (void)ciy_cancelHardwareSync;
- (void)ciy_setProfilesForTesting:(NSArray<NSDictionary *> *)profiles activeId:(nullable NSString *)activeId;
- (NSArray<NSDictionary *> *)ciy_profiles;
- (NSUserDefaults *)ciy_userDefaults;
- (void)ciy_saveCurrentProfile;
- (void)ciy_waitForPendingHardwareOperations;
- (void)ciy_setBatteryMirrorCheckboxState:(NSControlStateValue)state;
- (CIYKeyboardView *)ciy_keyboardView;
- (CIYProfileScheduler *)ciy_scheduler;
@end
#endif

NS_ASSUME_NONNULL_END
