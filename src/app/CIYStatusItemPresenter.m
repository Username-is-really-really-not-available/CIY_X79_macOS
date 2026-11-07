#import "CIYStatusItemPresenter.h"
#import "CIYLocalization.h"
#import <ctype.h>

static BOOL CIYParseHexRGB(NSString *hex, uint8_t *outR, uint8_t *outG, uint8_t *outB) {
    if (!hex || ![hex isKindOfClass:[NSString class]]) return NO;
    const char *str = [hex UTF8String];
    if (!str) return NO;
    if (*str == '#') str++;
    if (strlen(str) != 6) return NO;
    for (int i = 0; i < 6; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return NO;
        }
    }
    unsigned int val = 0;
    if (sscanf(str, "%x", &val) != 1) return NO;
    *outR = (val >> 16) & 0xFF;
    *outG = (val >> 8) & 0xFF;
    *outB = val & 0xFF;
    return YES;
}

@implementation CIYStatusItemPresenter

+ (NSString *)initialForProfileName:(nullable NSString *)name {
    if (!name || ![name isKindOfClass:[NSString class]] || name.length == 0) {
        return @"•";
    }

    NSCharacterSet *alnum = [NSCharacterSet alphanumericCharacterSet];
    NSUInteger len = name.length;
    for (NSUInteger i = 0; i < len; ) {
        NSRange range = [name rangeOfComposedCharacterSequenceAtIndex:i];
        NSString *cluster = [name substringWithRange:range];
        if ([cluster rangeOfCharacterFromSet:alnum].location != NSNotFound) {
            for (NSUInteger cIdx = 0; cIdx < cluster.length; cIdx++) {
                unichar ch = [cluster characterAtIndex:cIdx];
                if ([alnum characterIsMember:ch]) {
                    return [[NSString stringWithCharacters:&ch length:1] uppercaseString];
                }
            }
            return [cluster uppercaseString];
        }
        i = range.location + range.length;
    }

    return @"•";
}

+ (NSColor *)dominantColorFromSlots:(nullable NSDictionary *)slots {
    if (!slots || ![slots isKindOfClass:[NSDictionary class]] || slots.count == 0) {
        return [NSColor controlAccentColor];
    }

    NSMutableDictionary<NSNumber *, NSNumber *> *counts = [NSMutableDictionary dictionary];
    NSMutableDictionary<NSNumber *, NSNumber *> *minSlots = [NSMutableDictionary dictionary];

    for (id key in slots) {
        if (![key isKindOfClass:[NSString class]]) continue;
        NSString *slotStr = (NSString *)key;
        NSScanner *scanner = [NSScanner scannerWithString:slotStr];
        int slotNum = -1;
        if (![scanner scanInt:&slotNum] || !scanner.isAtEnd || slotNum < 0) {
            continue;
        }

        id val = slots[key];
        if (![val isKindOfClass:[NSString class]]) continue;
        uint8_t r = 0, g = 0, b = 0;
        if (!CIYParseHexRGB((NSString *)val, &r, &g, &b)) continue;

        if (r == 0 && g == 0 && b == 0) {
            continue;
        }

        uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        NSNumber *rgbKey = @(rgb);

        NSInteger count = [counts[rgbKey] integerValue] + 1;
        counts[rgbKey] = @(count);

        NSNumber *existingMin = minSlots[rgbKey];
        if (!existingMin || slotNum < [existingMin intValue]) {
            minSlots[rgbKey] = @(slotNum);
        }
    }

    if (counts.count == 0) {
        return [NSColor controlAccentColor];
    }

    NSNumber *bestRgbKey = nil;
    NSInteger bestCount = -1;
    int bestMinSlot = INT_MAX;

    for (NSNumber *rgbKey in counts) {
        NSInteger count = [counts[rgbKey] integerValue];
        int minSlot = [minSlots[rgbKey] intValue];

        if (count > bestCount) {
            bestCount = count;
            bestMinSlot = minSlot;
            bestRgbKey = rgbKey;
        } else if (count == bestCount) {
            if (minSlot < bestMinSlot) {
                bestMinSlot = minSlot;
                bestRgbKey = rgbKey;
            }
        }
    }

    if (!bestRgbKey) {
        return [NSColor controlAccentColor];
    }

    uint32_t bestRgb = [bestRgbKey unsignedIntValue];
    CGFloat red = (CGFloat)((bestRgb >> 16) & 0xFF) / 255.0;
    CGFloat green = (CGFloat)((bestRgb >> 8) & 0xFF) / 255.0;
    CGFloat blue = (CGFloat)(bestRgb & 0xFF) / 255.0;
    return [NSColor colorWithSRGBRed:red green:green blue:blue alpha:1.0];
}

+ (nullable NSString *)keyEquivalentForIndex:(NSInteger)index {
    if (index >= 0 && index < 7) {
        return [NSString stringWithFormat:@"%ld", (long)(index + 1)];
    }
    return @"";
}

+ (NSString *)toolTipForProfileName:(nullable NSString *)name connected:(BOOL)connected {
    NSString *cleanName = (name && name.length > 0) ? name : CIYL(@"Profile");
    if (connected) {
        return [NSString stringWithFormat:CIYL(@"CIY X79 Control – %@"), cleanName];
    } else {
        return [NSString stringWithFormat:CIYL(@"CIY X79 Control – %@ (disconnected)"), cleanName];
    }
}

+ (NSString *)accessibilityDescriptionForProfileName:(nullable NSString *)name connected:(BOOL)connected {
    NSString *cleanName = (name && name.length > 0) ? name : CIYL(@"Profile");
    if (connected) {
        return [NSString stringWithFormat:CIYL(@"CIY X79, profile “%@”, connected"), cleanName];
    } else {
        return [NSString stringWithFormat:CIYL(@"CIY X79, profile “%@”, disconnected"), cleanName];
    }
}

+ (NSImage *)iconForProfileName:(nullable NSString *)name
                   profileColor:(nullable NSColor *)color
                      connected:(BOOL)connected {
    NSString *safeName = name ? [name copy] : @"";
    NSColor *safeColor = color ? [color copy] : [NSColor controlAccentColor];

    NSImage *image = [NSImage imageWithSize:NSMakeSize(18.0, 18.0)
                                    flipped:NO
                             drawingHandler:^BOOL(NSRect dstRect) {
        (void)dstRect;

        // 1. Profil-Kreis mit Akzent-/Profilfarbe zeichnen
        NSRect circleRect = NSMakeRect(1.0, 2.0, 13.0, 13.0);
        NSBezierPath *circlePath = [NSBezierPath bezierPathWithOvalInRect:circleRect];
        [safeColor setFill];
        [circlePath fill];

        // Feine Kontur für Lesbarkeit in Light- und Dark-Mode
        [[NSColor colorWithWhite:0.5 alpha:0.35] setStroke];
        circlePath.lineWidth = 0.75;
        [circlePath stroke];

        // 2. Profil-Initiale zentriert zeichnen mit Luminanz-angepasster Textfarbe
        NSString *initial = [CIYStatusItemPresenter initialForProfileName:safeName];
        NSColor *srgbColor = [safeColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] ?: safeColor;
        CGFloat cr = 0, cg = 0, cb = 0, ca = 0;
        [srgbColor getRed:&cr green:&cg blue:&cb alpha:&ca];
        CGFloat luminance = 0.299 * cr + 0.587 * cg + 0.114 * cb;
        NSColor *textColor = (luminance > 0.55)
            ? [NSColor colorWithSRGBRed:0.08 green:0.08 blue:0.08 alpha:1.0]
            : [NSColor whiteColor];

        NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
        style.alignment = NSTextAlignmentCenter;
        NSDictionary *textAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:8.0 weight:NSFontWeightBold],
            NSForegroundColorAttributeName: textColor,
            NSParagraphStyleAttributeName: style
        };
        NSSize textSize = [initial sizeWithAttributes:textAttrs];
        NSRect textRect = NSMakeRect(circleRect.origin.x,
                                     circleRect.origin.y + (circleRect.size.height - textSize.height) / 2.0,
                                     circleRect.size.width,
                                     textSize.height);
        [initial drawInRect:textRect withAttributes:textAttrs];

        // 3. Verbindungsstatus-Badge (unten rechts)
        NSRect badgeRect = NSMakeRect(11.0, 1.0, 6.0, 6.0);

        // Transparente Aussparung hinter dem Badge für klaren Kontrast zum Profilkreis
        [NSGraphicsContext saveGraphicsState];
        [[NSGraphicsContext currentContext] setCompositingOperation:NSCompositingOperationClear];
        NSBezierPath *clearCutout = [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(badgeRect, -0.75, -0.75)];
        [clearCutout fill];
        [NSGraphicsContext restoreGraphicsState];

        if (connected) {
            // Verbunden: ausgefüllter grüner Punkt
            NSColor *green = [NSColor colorWithSRGBRed:0.18 green:0.82 blue:0.32 alpha:1.0];
            NSBezierPath *dot = [NSBezierPath bezierPathWithOvalInRect:badgeRect];
            [green setFill];
            [dot fill];
        } else {
            // Getrennt: hohler roter Ring (ohne Füllung, formkodiert)
            NSColor *red = [NSColor colorWithSRGBRed:0.95 green:0.22 blue:0.20 alpha:1.0];
            NSBezierPath *ring = [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(badgeRect, 0.6, 0.6)];
            ring.lineWidth = 1.3;
            [red setStroke];
            [ring stroke];
        }

        return YES;
    }];

    image.template = NO;
    image.accessibilityDescription = [self accessibilityDescriptionForProfileName:name connected:connected];
    return image;
}

@end
