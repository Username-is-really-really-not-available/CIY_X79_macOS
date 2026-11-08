#import "CIYProfileTags.h"
#import "CIYLocalization.h"

NSString * const kCIYProfileTagsKey = @"tags";
const NSUInteger kCIYMaxTagsPerProfile = 5;
const NSUInteger kCIYMaxTagLength = 20;

static NSString *TruncateTag(NSString *tag, NSUInteger maxUnits) {
    if (!tag || tag.length <= maxUnits) return tag ?: @"";
    NSRange r = [tag rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, maxUnits)];
    NSString *s = [tag substringWithRange:r];
    while (s.length > maxUnits) {
        NSRange last = [s rangeOfComposedCharacterSequenceAtIndex:s.length - 1];
        if (last.location == 0) break;
        s = [s substringToIndex:last.location];
    }
    return s;
}

static NSString *NormalizeWhitespace(NSString *str) {
    if (!str || ![str isKindOfClass:[NSString class]]) return @"";
    NSArray<NSString *> *words = [str componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSMutableArray<NSString *> *nonEmpty = [NSMutableArray arrayWithCapacity:words.count];
    NSCharacterSet *ctrl = [NSCharacterSet controlCharacterSet];
    for (NSString *w in words) {
        NSString *cleanedWord = [[w componentsSeparatedByCharactersInSet:ctrl] componentsJoinedByString:@""];
        if (cleanedWord.length > 0) {
            [nonEmpty addObject:cleanedWord];
        }
    }
    return [nonEmpty componentsJoinedByString:@" "];
}

@implementation CIYProfileTags

+ (NSArray<NSString *> *)suggestedTags {
    return @[CIYL(@"Gaming"), CIYL(@"Office"), CIYL(@"Coding"), CIYL(@"Night")];
}

+ (NSString *)normalizedTagKey:(nullable NSString *)tag {
    if (!tag || ![tag isKindOfClass:[NSString class]]) return @"";
    NSString *normalized = NormalizeWhitespace(tag);
    NSString *lower = [[normalized lowercaseString] precomposedStringWithCanonicalMapping];
    if (lower.length == 0) return @"";

    static NSDictionary<NSString *, NSString *> *sTagAliases = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sTagAliases = @{
            @"büro":  @"office",
            @"buero": @"office",
            @"nacht": @"night"
        };
    });

    NSString *canonical = sTagAliases[lower];
    return canonical ?: lower;
}

+ (nullable NSString *)sanitizeTag:(nullable id)rawTag {
    if (!rawTag || ![rawTag isKindOfClass:[NSString class]]) return nil;
    NSString *normalized = NormalizeWhitespace((NSString *)rawTag);
    if (normalized.length == 0) return nil;

    if (normalized.length > kCIYMaxTagLength) {
        normalized = TruncateTag(normalized, kCIYMaxTagLength);
        normalized = [normalized stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    }
    return (normalized.length > 0) ? normalized : nil;
}

+ (NSArray<NSString *> *)sanitizeTags:(nullable id)rawTags {
    if (!rawTags || ![rawTags isKindOfClass:[NSArray class]]) return @[];
    NSArray *rawList = (NSArray *)rawTags;
    NSMutableArray<NSString *> *result = [NSMutableArray arrayWithCapacity:MIN(rawList.count, kCIYMaxTagsPerProfile)];
    NSMutableSet<NSString *> *seenKeys = [NSMutableSet setWithCapacity:rawList.count];

    for (id item in rawList) {
        if (result.count >= kCIYMaxTagsPerProfile) break;
        NSString *tag = [self sanitizeTag:item];
        if (!tag) continue;
        NSString *key = [self normalizedTagKey:tag];
        if (key.length == 0 || [seenKeys containsObject:key]) continue;
        [seenKeys addObject:key];
        [result addObject:tag];
    }
    return [result copy];
}

+ (void)sanitizeTagsInProfile:(NSMutableDictionary *)profile {
    if (!profile || ![profile isKindOfClass:[NSMutableDictionary class]]) return;
    id raw = profile[kCIYProfileTagsKey];
    NSArray<NSString *> *cleaned = [self sanitizeTags:raw];
    if (cleaned.count > 0) {
        profile[kCIYProfileTagsKey] = cleaned;
    } else {
        [profile removeObjectForKey:kCIYProfileTagsKey];
    }
}

+ (NSArray<NSString *> *)allTagsInProfiles:(NSArray<NSDictionary *> *)profiles {
    if (!profiles || ![profiles isKindOfClass:[NSArray class]]) return @[];
    NSMutableArray<NSString *> *allTags = [NSMutableArray array];
    NSMutableSet<NSString *> *seenKeys = [NSMutableSet set];

    for (NSDictionary *prof in profiles) {
        if (![prof isKindOfClass:[NSDictionary class]]) continue;
        id raw = prof[kCIYProfileTagsKey];
        if (![raw isKindOfClass:[NSArray class]]) continue;
        for (id item in (NSArray *)raw) {
            NSString *tag = [self sanitizeTag:item];
            if (!tag) continue;
            NSString *key = [self normalizedTagKey:tag];
            if (key.length == 0 || [seenKeys containsObject:key]) continue;
            [seenKeys addObject:key];
            [allTags addObject:tag];
        }
    }

    [allTags sortUsingComparator:^NSComparisonResult(NSString *s1, NSString *s2) {
        return [s1 localizedStandardCompare:s2];
    }];
    return [allTags copy];
}

+ (BOOL)profile:(NSDictionary *)profile matchesFilterTag:(nullable NSString *)filterTag {
    if (![profile isKindOfClass:[NSDictionary class]]) return NO;
    if (!filterTag || filterTag.length == 0) return YES;
    NSString *filterKey = [self normalizedTagKey:filterTag];
    if (filterKey.length == 0) return YES;
    if ([filterKey isEqualToString:@"all"] || [filterKey isEqualToString:@"alle"]) return YES;

    id raw = profile[kCIYProfileTagsKey];
    if (![raw isKindOfClass:[NSArray class]]) return NO;
    for (id item in (NSArray *)raw) {
        if (![item isKindOfClass:[NSString class]]) continue;
        if ([[self normalizedTagKey:(NSString *)item] isEqualToString:filterKey]) {
            return YES;
        }
    }
    return NO;
}

+ (NSArray<NSNumber *> *)filteredProfileIndicesForProfiles:(NSArray<NSDictionary *> *)profiles
                                                 filterTag:(nullable NSString *)filterTag {
    if (!profiles || ![profiles isKindOfClass:[NSArray class]]) return @[];
    NSMutableArray<NSNumber *> *indices = [NSMutableArray arrayWithCapacity:profiles.count];
    for (NSUInteger i = 0; i < profiles.count; i++) {
        if ([self profile:profiles[i] matchesFilterTag:filterTag]) {
            [indices addObject:@(i)];
        }
    }
    return [indices copy];
}

+ (NSArray<NSNumber *> *)popUpIndicesForProfiles:(NSArray<NSDictionary *> *)profiles
                                       filterTag:(nullable NSString *)filterTag
                              activeProfileIndex:(NSInteger)activeProfileIndex
                               activeIsPrepended:(nullable BOOL *)outActiveIsPrepended {
    if (outActiveIsPrepended) *outActiveIsPrepended = NO;
    if (!profiles || ![profiles isKindOfClass:[NSArray class]]) return @[];

    NSArray<NSNumber *> *filtered = [self filteredProfileIndicesForProfiles:profiles filterTag:filterTag];
    if (activeProfileIndex < 0 || (NSUInteger)activeProfileIndex >= profiles.count) {
        return filtered;
    }

    NSNumber *activeNum = @(activeProfileIndex);
    if ([filtered containsObject:activeNum]) {
        return filtered;
    }

    if (outActiveIsPrepended) *outActiveIsPrepended = YES;
    NSMutableArray<NSNumber *> *result = [NSMutableArray arrayWithCapacity:filtered.count + 1];
    [result addObject:activeNum];
    [result addObjectsFromArray:filtered];
    return [result copy];
}

+ (NSInteger)profileIndexForRow:(NSInteger)row inPopUpIndices:(NSArray<NSNumber *> *)popUpIndices {
    if (row < 0 || !popUpIndices || (NSUInteger)row >= popUpIndices.count) return -1;
    return popUpIndices[(NSUInteger)row].integerValue;
}

+ (NSInteger)rowForProfileIndex:(NSInteger)profileIndex inPopUpIndices:(NSArray<NSNumber *> *)popUpIndices {
    if (profileIndex < 0 || !popUpIndices) return -1;
    NSNumber *target = @(profileIndex);
    NSUInteger idx = [popUpIndices indexOfObject:target];
    return (idx != NSNotFound) ? (NSInteger)idx : -1;
}

+ (nullable NSDictionary *)profileForRow:(NSInteger)row
                          inPopUpIndices:(nullable NSArray<NSNumber *> *)popUpIndices
                                profiles:(nullable NSArray<NSDictionary *> *)profiles {
    if (![profiles isKindOfClass:[NSArray class]]) return nil;
    NSInteger profIdx = [self profileIndexForRow:row inPopUpIndices:popUpIndices];
    if (profIdx < 0 || (NSUInteger)profIdx >= profiles.count) return nil;
    NSDictionary *prof = profiles[(NSUInteger)profIdx];
    return [prof isKindOfClass:[NSDictionary class]] ? prof : nil;
}

@end
