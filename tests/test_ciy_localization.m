#import <Foundation/Foundation.h>
#import <assert.h>
#include <stdio.h>
#import "../src/app/CIYLocalization.h"

#if !defined(CIY_ENABLE_TEST_HOOKS)
#error "Tests must be compiled with -DCIY_ENABLE_TEST_HOOKS=1"
#endif

static NSString * const kTestSuiteName = @"com.juliengenau.ciy-x79-control.localization-tests";

static void test_language_codes(void) {
    assert([CIYLocalization languageFromCode:nil] == CIYLanguageEnglish);
    assert([CIYLocalization languageFromCode:@"en"] == CIYLanguageEnglish);
    assert([CIYLocalization languageFromCode:@"de"] == CIYLanguageGerman);
    assert([CIYLocalization languageFromCode:@"fr"] == CIYLanguageEnglish);
    assert([[CIYLocalization languageCodeForLanguage:CIYLanguageEnglish] isEqualToString:@"en"]);
    assert([[CIYLocalization languageCodeForLanguage:CIYLanguageGerman] isEqualToString:@"de"]);
}

static void test_language_resolution(void) {
    assert([CIYLocalization resolveLanguageFromStoredCode:nil hasStoredProfiles:NO] == CIYLanguageEnglish);
    assert([CIYLocalization resolveLanguageFromStoredCode:nil hasStoredProfiles:YES] == CIYLanguageGerman);
    assert([CIYLocalization resolveLanguageFromStoredCode:@"en" hasStoredProfiles:YES] == CIYLanguageEnglish);
    assert([CIYLocalization resolveLanguageFromStoredCode:@"de" hasStoredProfiles:NO] == CIYLanguageGerman);
    assert([CIYLocalization resolveLanguageFromStoredCode:@"fr" hasStoredProfiles:YES] == CIYLanguageEnglish);
}

static void test_persistence_and_lookup(void) {
    [CIYLocalization setPreferredLanguage:CIYLanguageEnglish];
    [CIYLocalization loadEffectiveLanguage];
    assert([CIYLocalization preferredLanguage] == CIYLanguageEnglish);
    assert([CIYLocalization effectiveLanguage] == CIYLanguageEnglish);
    assert([CIYL(@"Reconnect") isEqualToString:@"Reconnect"]);

    [CIYLocalization setPreferredLanguage:CIYLanguageGerman];
    assert([CIYLocalization preferredLanguage] == CIYLanguageGerman);
    assert([CIYLocalization effectiveLanguage] == CIYLanguageEnglish);
    [CIYLocalization loadEffectiveLanguage];
    assert([CIYLocalization effectiveLanguage] == CIYLanguageGerman);
    assert([CIYL(@"Reconnect") isEqualToString:@"Neu verbinden"]);
    assert([CIYL(@"Unknown localization key") isEqualToString:@"Unknown localization key"]);
    assert([CIYL(nil) isEqualToString:@""]);
    assert([CIYL(@"") isEqualToString:@""]);

    [CIYLocalization setPreferredLanguage:CIYLanguageEnglish];
    [CIYLocalization loadEffectiveLanguage];
    assert([CIYL(@"Reconnect") isEqualToString:@"Reconnect"]);
}

static void test_production_bundle_lookup(void) {
    // Testet den produktiven [NSBundle mainBundle]-Pfad ohne Test-Hook.
    // run_tests.sh kopiert resources/de.lproj neben das Test-Binary.
    [CIYLocalization setTestResourceDirectory:nil];
    [CIYLocalization setPreferredLanguage:CIYLanguageGerman];
    [CIYLocalization loadEffectiveLanguage];
    assert([CIYLocalization effectiveLanguage] == CIYLanguageGerman);
    assert([CIYL(@"Reconnect") isEqualToString:@"Neu verbinden"]);

    [CIYLocalization setPreferredLanguage:CIYLanguageEnglish];
    [CIYLocalization loadEffectiveLanguage];
    assert([CIYL(@"Reconnect") isEqualToString:@"Reconnect"]);
}

static NSArray<NSString *> *CIYFormatSpecifiers(NSString *text, BOOL *hasStrayPercent) {
    NSRegularExpression *regex = [NSRegularExpression
        regularExpressionWithPattern:@"%(?:%|[-+ 0#]*[0-9]*(?:\\.[0-9]+)?(?:hh|h|ll|l|q|z|t|j)?[@dDiuUxXoOfFeEgGcCsSpaA])"
                             options:0 error:nil];
    NSArray<NSTextCheckingResult *> *matches =
        [regex matchesInString:text options:0 range:NSMakeRange(0, text.length)];
    NSMutableArray<NSString *> *specs = [NSMutableArray arrayWithCapacity:matches.count];
    for (NSTextCheckingResult *m in matches) {
        [specs addObject:[text substringWithRange:m.range]];
    }
    if (hasStrayPercent) {
        NSMutableString *rest = [text mutableCopy];
        for (NSTextCheckingResult *m in [matches reverseObjectEnumerator]) {
            [rest replaceCharactersInRange:m.range withString:@""];
        }
        *hasStrayPercent = ([rest rangeOfString:@"%"].location != NSNotFound);
    }
    return specs;
}

static NSString *CIYUnescapeStringLiteral(NSString *raw) {
    NSMutableString *out = [NSMutableString stringWithCapacity:raw.length];
    NSUInteger i = 0;
    while (i < raw.length) {
        unichar c = [raw characterAtIndex:i];
        if (c != '\\' || i + 1 >= raw.length) {
            [out appendString:[raw substringWithRange:NSMakeRange(i, 1)]];
            i++;
            continue;
        }
        unichar next = [raw characterAtIndex:i + 1];
        switch (next) {
            case '"': [out appendString:@"\""]; i += 2; break;
            case '\\': [out appendString:@"\\"]; i += 2; break;
            case 'n': [out appendString:@"\n"]; i += 2; break;
            case 't': [out appendString:@"\t"]; i += 2; break;
            case 'r': [out appendString:@"\r"]; i += 2; break;
            default:
                [out appendString:@"\\"];
                [out appendString:[raw substringWithRange:NSMakeRange(i + 1, 1)]];
                i += 2;
                break;
        }
    }
    return out;
}

static NSDictionary<NSString *, NSString *> *CIYParseStringsFile(NSString *path,
                                                                 NSMutableArray<NSString *> *allKeys) {
    NSError *error = nil;
    NSString *content = [NSString stringWithContentsOfFile:path
                                                  encoding:NSUTF8StringEncoding
                                                     error:&error];
    assert(content != nil);
    NSRegularExpression *lineRegex = [NSRegularExpression
        regularExpressionWithPattern:@"^\"((?:[^\"\\\\]|\\\\.)*)\"[[:space:]]*=[[:space:]]*\"((?:[^\"\\\\]|\\\\.)*)\"[[:space:]]*;[[:space:]]*$"
                             options:0 error:nil];
    NSMutableDictionary<NSString *, NSString *> *result = [NSMutableDictionary dictionary];
    for (NSString *rawLine in [content componentsSeparatedByString:@"\n"]) {
        NSString *line = [rawLine stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (line.length == 0 || [line hasPrefix:@"//"] ||
            [line hasPrefix:@"/*"] || [line hasPrefix:@"*"] || [line hasPrefix:@"*/"]) {
            continue;
        }
        NSTextCheckingResult *m = [lineRegex firstMatchInString:line
                                                       options:0
                                                         range:NSMakeRange(0, line.length)];
        assert(m != nil);
        NSString *key = CIYUnescapeStringLiteral([line substringWithRange:[m rangeAtIndex:1]]);
        NSString *value = CIYUnescapeStringLiteral([line substringWithRange:[m rangeAtIndex:2]]);
        if (allKeys) {
            [allKeys addObject:key];
        }
        result[key] = value;
    }
    return result;
}

static BOOL CIYPrecededByStringWithFormat(NSString *source, NSUInteger location) {
    static NSString * const prefix = @"stringWithFormat:";
    NSUInteger p = location;
    while (p > 0) {
        unichar c = [source characterAtIndex:p - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p--;
            continue;
        }
        break;
    }
    if (p < prefix.length) {
        return NO;
    }
    return [[source substringWithRange:NSMakeRange(p - prefix.length, prefix.length)] isEqualToString:prefix];
}

// Erfasst ausschließlich einfache C-String-Literale in CIYL(@"...").
// Zusammengesetzte Literale (@"a" @"b"), Makros und dynamische Keys sind
// bewusst außerhalb des Prüfumfangs.
static void CIYScanSourceKeys(NSString *source,
                              NSMutableSet<NSString *> *allKeys,
                              NSMutableSet<NSString *> *formatKeys) {
    NSString *needle = @"CIYL(@\"";
    NSUInteger searchStart = 0;
    while (searchStart < source.length) {
        NSRange searchRange = NSMakeRange(searchStart, source.length - searchStart);
        NSRange found = [source rangeOfString:needle options:0 range:searchRange];
        if (found.location == NSNotFound) {
            break;
        }

        NSUInteger i = NSMaxRange(found);
        NSMutableString *raw = [NSMutableString string];
        BOOL escaped = NO;
        BOOL closed = NO;
        while (i < source.length) {
            unichar c = [source characterAtIndex:i];
            if (escaped) {
                [raw appendString:@"\\"];
                [raw appendString:[source substringWithRange:NSMakeRange(i, 1)]];
                escaped = NO;
                i++;
                continue;
            }
            if (c == '\\') {
                escaped = YES;
                i++;
                continue;
            }
            if (c == '"') {
                closed = YES;
                break;
            }
            [raw appendString:[source substringWithRange:NSMakeRange(i, 1)]];
            i++;
        }
        assert(closed);

        NSString *key = CIYUnescapeStringLiteral(raw);
        [allKeys addObject:key];
        if (CIYPrecededByStringWithFormat(source, found.location)) {
            [formatKeys addObject:key];
        }
        searchStart = i + 1;
    }
}

static void CIYValidateFormatSpecifierParity(NSSet<NSString *> *formatKeys,
                                             NSDictionary<NSString *, NSString *> *translations) {
    for (NSString *key in formatKeys) {
        NSString *value = translations[key];
        assert(value != nil);
        BOOL keyStray = NO, valueStray = NO;
        NSArray<NSString *> *keySpecs = CIYFormatSpecifiers(key, &keyStray);
        NSArray<NSString *> *valueSpecs = CIYFormatSpecifiers(value, &valueStray);
        if (![keySpecs isEqualToArray:valueSpecs] || keyStray || valueStray) {
            fprintf(stderr, "FEHLER: Format-Specifier-Mismatch: %s\n", key.UTF8String);
            fprintf(stderr, "  Key:   %s\n", [[keySpecs componentsJoinedByString:@" "] UTF8String]);
            fprintf(stderr, "  Value: %s\n", [[valueSpecs componentsJoinedByString:@" "] UTF8String]);
            assert(0);
        }
    }
}

static void CIYValidateLocalizationCoverage(NSString *appSourceDirectory, NSString *stringsPath) {
    NSMutableArray<NSString *> *definedKeys = [NSMutableArray array];
    NSDictionary<NSString *, NSString *> *translations = CIYParseStringsFile(stringsPath, definedKeys);
    assert(translations.count > 0);

    NSCountedSet *keyCounts = [[NSCountedSet alloc] initWithCapacity:definedKeys.count];
    for (NSString *key in definedKeys) {
        [keyCounts addObject:key];
    }
    for (NSString *key in keyCounts) {
        if ([keyCounts countForObject:key] > 1) {
            fprintf(stderr, "FEHLER: Doppelter Key in de.lproj: %s\n", key.UTF8String);
            assert(0);
        }
    }

    NSMutableSet<NSString *> *allKeys = [NSMutableSet set];
    NSMutableSet<NSString *> *formatKeys = [NSMutableSet set];
    NSError *error = nil;
    NSArray<NSString *> *files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:appSourceDirectory
                                                                                     error:&error];
    assert(files != nil);
    for (NSString *file in files) {
        if (![file hasSuffix:@".m"]) {
            continue;
        }
        NSString *sourcePath = [appSourceDirectory stringByAppendingPathComponent:file];
        NSString *source = [NSString stringWithContentsOfFile:sourcePath
                                                     encoding:NSUTF8StringEncoding
                                                        error:&error];
        assert(source != nil);
        CIYScanSourceKeys(source, allKeys, formatKeys);
    }

    assert(allKeys.count > 0);
    assert(formatKeys.count > 0);

    for (NSString *key in allKeys) {
        if (!translations[key]) {
            fprintf(stderr, "FEHLER: Key fehlt in de.lproj: %s\n", key.UTF8String);
            assert(0);
        }
    }

    for (NSString *key in translations) {
        if (![allKeys containsObject:key]) {
            fprintf(stderr, "WARNUNG: Ungenutzter Key in de.lproj: %s\n", key.UTF8String);
        }
    }

    CIYValidateFormatSpecifierParity(formatKeys, translations);
}

int main(void) {
    @autoreleasepool {
        NSUserDefaults *testDefaults = [[NSUserDefaults alloc] initWithSuiteName:kTestSuiteName];
        assert(testDefaults != nil);
        [testDefaults removePersistentDomainForName:kTestSuiteName];
        [CIYLocalization setUserDefaultsForTesting:testDefaults];

        NSString *cwd = [[NSFileManager defaultManager] currentDirectoryPath];
        NSString *resourcesDir = [cwd stringByAppendingPathComponent:@"resources"];
        [CIYLocalization setTestResourceDirectory:resourcesDir];

        test_language_codes();
        test_language_resolution();
        test_persistence_and_lookup();
        test_production_bundle_lookup();

        CIYValidateLocalizationCoverage([cwd stringByAppendingPathComponent:@"src/app"],
                                        [resourcesDir stringByAppendingPathComponent:@"de.lproj/Localizable.strings"]);

        [CIYLocalization setUserDefaultsForTesting:nil];
        [CIYLocalization setTestResourceDirectory:nil];
        [testDefaults removePersistentDomainForName:kTestSuiteName];
        puts("Lokalisierungs-Tests bestanden.");
    }
    return 0;
}
