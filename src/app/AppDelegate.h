#import <Cocoa/Cocoa.h>
#import "CIYKeyboardView.h"
#import "KeyboardBackend.h"

NS_ASSUME_NONNULL_BEGIN

@interface AppDelegate : NSObject <NSApplicationDelegate, CIYKeyboardViewDelegate>

@property (nonatomic, strong) NSWindow *window;

@end

NS_ASSUME_NONNULL_END
