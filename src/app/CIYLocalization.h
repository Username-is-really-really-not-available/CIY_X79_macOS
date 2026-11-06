#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, CIYLanguage) {
    CIYLanguageEnglish = 0,
    CIYLanguageGerman  = 1
};

FOUNDATION_EXPORT NSString * const CIYLanguagePreferenceKey;

@interface CIYLocalization : NSObject
+ (CIYLanguage)effectiveLanguage;
+ (CIYLanguage)preferredLanguage;
+ (void)setPreferredLanguage:(CIYLanguage)language;
+ (void)loadEffectiveLanguage;
+ (NSString *)languageCodeForLanguage:(CIYLanguage)language;
+ (CIYLanguage)languageFromCode:(nullable NSString *)code;
+ (CIYLanguage)resolveLanguageFromStoredCode:(nullable NSString *)storedCode
                             hasStoredProfiles:(BOOL)hasStoredProfiles;
+ (NSString *)localizedStringForKey:(nullable NSString *)key;

#ifdef CIY_ENABLE_TEST_HOOKS
+ (void)setTestResourceDirectory:(nullable NSString *)path;
+ (void)setUserDefaultsForTesting:(nullable NSUserDefaults *)defaults;
#endif
@end

NS_ASSUME_NONNULL_END

#define CIYL(key) [CIYLocalization localizedStringForKey:(key)]
