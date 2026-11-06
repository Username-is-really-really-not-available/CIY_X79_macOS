#import "CIYLocalization.h"
#import <dispatch/dispatch.h>

NSString * const CIYLanguagePreferenceKey = @"CIYX79Language_v1";

static CIYLanguage gEffectiveLanguage = CIYLanguageEnglish;

#ifdef CIY_ENABLE_TEST_HOOKS
static NSUserDefaults *gTestDefaults = nil;
static NSString *gTestResourceDirectory = nil;
static NSBundle *gTestGermanBundle = nil;
#endif

static NSUserDefaults *CIYLocalizationDefaults(void) {
#ifdef CIY_ENABLE_TEST_HOOKS
    if (gTestDefaults) {
        return gTestDefaults;
    }
#endif
    return [NSUserDefaults standardUserDefaults];
}

@implementation CIYLocalization

+ (CIYLanguage)languageFromCode:(NSString *)code {
    if ([code isEqualToString:@"de"]) {
        return CIYLanguageGerman;
    }
    return CIYLanguageEnglish;
}

+ (NSString *)languageCodeForLanguage:(CIYLanguage)language {
    return (language == CIYLanguageGerman) ? @"de" : @"en";
}

+ (CIYLanguage)preferredLanguage {
    NSString *code = [CIYLocalizationDefaults() stringForKey:CIYLanguagePreferenceKey];
    return [self languageFromCode:code];
}

+ (void)setPreferredLanguage:(CIYLanguage)language {
    CIYLanguage normalized = (language == CIYLanguageGerman) ? CIYLanguageGerman : CIYLanguageEnglish;
    [CIYLocalizationDefaults() setObject:[self languageCodeForLanguage:normalized]
                                  forKey:CIYLanguagePreferenceKey];
}

+ (void)loadEffectiveLanguage {
    gEffectiveLanguage = [self preferredLanguage];
}

+ (CIYLanguage)effectiveLanguage {
    return gEffectiveLanguage;
}

+ (CIYLanguage)resolveLanguageFromStoredCode:(NSString *)storedCode
                             hasStoredProfiles:(BOOL)hasStoredProfiles {
    if (storedCode != nil) {
        return [self languageFromCode:storedCode];
    }
    return hasStoredProfiles ? CIYLanguageGerman : CIYLanguageEnglish;
}

+ (NSBundle *)germanBundle {
#ifdef CIY_ENABLE_TEST_HOOKS
    if (gTestResourceDirectory.length > 0) {
        if (!gTestGermanBundle) {
            NSString *path = [gTestResourceDirectory stringByAppendingPathComponent:@"de.lproj"];
            NSBundle *bundle = [NSBundle bundleWithPath:path];
            if (!bundle || ![bundle pathForResource:@"Localizable" ofType:@"strings"]) {
                NSLog(@"CIY X79: Test localization resources (%@/Localizable.strings) are missing; falling back to English.",
                      path);
                bundle = nil;
            }
            gTestGermanBundle = bundle;
        }
        return gTestGermanBundle;
    }
#endif

    static dispatch_once_t onceToken;
    static NSBundle *productionBundle = nil;
    dispatch_once(&onceToken, ^{
        NSString *path = [[NSBundle mainBundle] pathForResource:@"de" ofType:@"lproj"];
        NSBundle *bundle = path ? [NSBundle bundleWithPath:path] : nil;
        if (!bundle || ![bundle pathForResource:@"Localizable" ofType:@"strings"]) {
            NSLog(@"CIY X79: German localization resources (de.lproj/Localizable.strings) are missing; falling back to English.");
            bundle = nil;
        }
        productionBundle = bundle;
    });
    return productionBundle;
}

+ (NSString *)localizedStringForKey:(NSString *)key {
    if (key.length == 0) {
        return @"";
    }
    if ([self effectiveLanguage] != CIYLanguageGerman) {
        return key;
    }

    NSBundle *bundle = [self germanBundle];
    if (!bundle) {
        return key;
    }
    return [bundle localizedStringForKey:key value:key table:@"Localizable"];
}

#ifdef CIY_ENABLE_TEST_HOOKS
+ (void)setTestResourceDirectory:(NSString *)path {
    gTestResourceDirectory = [path copy];
    gTestGermanBundle = nil;
}

+ (void)setUserDefaultsForTesting:(NSUserDefaults *)defaults {
    gTestDefaults = defaults;
}
#endif

@end
