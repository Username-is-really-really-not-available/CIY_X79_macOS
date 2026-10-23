#import "CIYKeyboardView.h"

typedef struct {
    char label[32];
    int slot;
    float width; // Key width in U
} KeyItem;

@interface CIYKeyboardView () {
    NSMutableDictionary<NSNumber *, NSColor *> *_keyColors;
    NSTrackingArea *_trackingArea;
    int _hoveredSlot;
}
@end

@implementation CIYKeyboardView

static const KeyItem ROW_0[] = {
    {"Esc", 1, 1.0f}, {"1", 7, 1.0f}, {"2", 13, 1.0f}, {"3", 19, 1.0f}, {"4", 25, 1.0f},
    {"5", 31, 1.0f}, {"6", 37, 1.0f}, {"7", 43, 1.0f}, {"8", 49, 1.0f}, {"9", 55, 1.0f},
    {"0", 61, 1.0f}, {"ß", 67, 1.0f}, {"´", 73, 1.0f}, {"⌫ Back", 79, 2.0f}, {"Entf", 86, 1.0f}
};

static const KeyItem ROW_1[] = {
    {"⇥ Tab", 2, 1.5f}, {"Q", 8, 1.0f}, {"W", 14, 1.0f}, {"E", 20, 1.0f}, {"R", 26, 1.0f},
    {"T", 32, 1.0f}, {"Z", 38, 1.0f}, {"U", 44, 1.0f}, {"I", 50, 1.0f}, {"O", 56, 1.0f},
    {"P", 62, 1.0f}, {"Ü", 68, 1.0f}, {"+ *", 74, 1.0f}, {"↵ Enter", 81, 1.5f}, {"Bild ⇡", 87, 1.0f}
};

static const KeyItem ROW_2[] = {
    {"⇪ Caps", 3, 1.75f}, {"A", 9, 1.0f}, {"S", 15, 1.0f}, {"D", 21, 1.0f}, {"F", 27, 1.0f},
    {"G", 33, 1.0f}, {"H", 39, 1.0f}, {"J", 45, 1.0f}, {"K", 51, 1.0f}, {"L", 57, 1.0f},
    {"Ö", 63, 1.0f}, {"Ä", 69, 1.0f}, {"# '", 75, 1.0f}, {"↵", 81, 1.25f}, {"Bild ⇣", 94, 1.0f}
};

static const KeyItem ROW_3[] = {
    {"⇧ Shift", 4, 1.25f}, {"< >", 76, 1.0f}, {"Y", 10, 1.0f}, {"X", 16, 1.0f}, {"C", 22, 1.0f},
    {"V", 28, 1.0f}, {"B", 34, 1.0f}, {"N", 40, 1.0f}, {"M", 46, 1.0f}, {", ;", 52, 1.0f},
    {". :", 58, 1.0f}, {"- _", 64, 1.0f}, {"⇧ Shift", 82, 1.75f}, {"▲", 88, 1.0f}, {"Ende", 91, 1.0f}
};

static const KeyItem ROW_4[] = {
    {"Strg", 5, 1.25f}, {"Win", 11, 1.25f}, {"Alt", 17, 1.25f},
    {"Leertaste (Space)", 35, 6.25f},
    {"AltGr", 53, 1.25f}, {"Fn", 77, 1.0f}, {"Strg", 80, 1.0f},
    {"◄", 83, 1.0f}, {"▼", 89, 1.0f}, {"►", 95, 1.0f}
};

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _keyColors = [NSMutableDictionary dictionary];
        _selectedSlot = -1;
        _hoveredSlot = -1;
        [self resetAllKeysToColor:[NSColor colorWithCalibratedRed:0.85 green:0.1 blue:0.1 alpha:1.0]]; // Red default
    }
    return self;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                 options:(NSTrackingMouseMoved |
                                                          NSTrackingMouseEnteredAndExited |
                                                          NSTrackingActiveInKeyWindow)
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)resetAllKeysToColor:(NSColor *)color {
    [_keyColors removeAllObjects];
    for (int i = 0; i < 126; i++) {
        _keyColors[@(i)] = color;
    }
    [self setNeedsDisplay:YES];
}

- (NSDictionary<NSNumber *, NSColor *> *)allKeyColors {
    return [_keyColors copy];
}

- (void)applyKeyColors:(NSDictionary<NSNumber *, NSColor *> *)colors {
    if (!colors) return;
    [_keyColors setDictionary:colors];
    [self setNeedsDisplay:YES];
}

- (void)setColor:(NSColor *)color forSlot:(int)slot {
    _keyColors[@(slot)] = color;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    NSRect bounds = self.bounds;

    // Draw keyboard case background (Dark metallic chassis)
    NSBezierPath *casePath = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:10 yRadius:10];
    [[NSColor colorWithCalibratedWhite:0.14 alpha:1.0] setFill];
    [casePath fill];

    [[NSColor colorWithCalibratedWhite:0.25 alpha:1.0] setStroke];
    [casePath setLineWidth:1.5];
    [casePath stroke];

    // Compute key dimensions
    // Max row units is ~16U (Row 0 has 13*1.0 + 2.0 + 1.0 = 16.0U)
    CGFloat margin = 12.0;
    CGFloat usableW = bounds.size.width - (margin * 2);
    CGFloat usableH = bounds.size.height - (margin * 2);

    CGFloat unitW = (usableW - 14 * 4.0) / 16.0; // gap = 4.0
    CGFloat keyH  = (usableH - 4 * 4.0) / 5.0;
    CGFloat gap = 4.0;

    const KeyItem *rows[] = {ROW_0, ROW_1, ROW_2, ROW_3, ROW_4};
    const size_t rowCounts[] = {
        sizeof(ROW_0)/sizeof(ROW_0[0]),
        sizeof(ROW_1)/sizeof(ROW_1[0]),
        sizeof(ROW_2)/sizeof(ROW_2[0]),
        sizeof(ROW_3)/sizeof(ROW_3[0]),
        sizeof(ROW_4)/sizeof(ROW_4[0])
    };

    for (int r = 0; r < 5; r++) {
        CGFloat y = bounds.size.height - margin - (r + 1) * keyH - r * gap;
        CGFloat x = margin;
        for (size_t c = 0; c < rowCounts[r]; c++) {
            KeyItem item = rows[r][c];
            CGFloat kw = item.width * unitW + (item.width - 1.0f) * gap;
            NSRect keyRect = NSMakeRect(x, y, kw, keyH);

            [self drawKey:item inRect:keyRect];
            x += kw + gap;
        }
    }
}

- (void)drawKey:(KeyItem)item inRect:(NSRect)rect {
    NSBezierPath *keyPath = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:5 yRadius:5];

    // Base key cap body
    NSColor *defaultColor = [NSColor colorWithCalibratedRed:0.2 green:0.2 blue:0.2 alpha:1.0];
    NSColor *rawGlow = _keyColors[@(item.slot)];
    if (!rawGlow) rawGlow = defaultColor;
    NSColor *glowColor = [rawGlow colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!glowColor) glowColor = defaultColor;

    // Key body color: subtly influenced by RGB glow color
    CGFloat r = 0.2, g = 0.2, b = 0.2, a = 1.0;
    [glowColor getRed:&r green:&g blue:&b alpha:&a];
    NSColor *capColor = [NSColor colorWithCalibratedRed:0.18 + r * 0.15
                                                  green:0.18 + g * 0.15
                                                   blue:0.18 + b * 0.15
                                                  alpha:1.0];

    [capColor setFill];
    [keyPath fill];

    // Key border / glow highlight
    if (item.slot == _selectedSlot) {
        [[NSColor whiteColor] setStroke];
        [keyPath setLineWidth:2.5];
        [keyPath stroke];
    } else if (item.slot == _hoveredSlot) {
        [[NSColor colorWithCalibratedWhite:0.7 alpha:1.0] setStroke];
        [keyPath setLineWidth:1.5];
        [keyPath stroke];
    } else {
        // Border glowing with the key's color
        [glowColor setStroke];
        [keyPath setLineWidth:1.2];
        [keyPath stroke];
    }

    // Key top inner bevel
    NSRect innerRect = NSInsetRect(rect, 2, 2);
    NSBezierPath *innerPath = [NSBezierPath bezierPathWithRoundedRect:innerRect xRadius:3 yRadius:3];
    [[NSColor colorWithCalibratedWhite:1.0 alpha:0.04] setFill];
    [innerPath fill];

    // Key label
    NSString *text = [NSString stringWithUTF8String:item.label];
    NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = NSTextAlignmentCenter;

    // Font size depends on label length
    CGFloat fontSize = (text.length > 3) ? 9.5 : 12.0;
    NSFont *font = [NSFont systemFontOfSize:fontSize weight:NSFontWeightMedium];

    // Text color is lit up by key's color
    NSColor *textColor = [NSColor colorWithCalibratedRed:0.7 + r * 0.3
                                                   green:0.7 + g * 0.3
                                                    blue:0.7 + b * 0.3
                                                   alpha:1.0];

    NSDictionary *attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: textColor,
        NSParagraphStyleAttributeName: style
    };

    NSSize textSize = [text sizeWithAttributes:attrs];
    NSRect textRect = NSMakeRect(rect.origin.x,
                                 rect.origin.y + (rect.size.height - textSize.height) / 2.0,
                                 rect.size.width,
                                 textSize.height);
    [text drawInRect:textRect withAttributes:attrs];
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)mouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    int hitSlot = [self slotAtPoint:loc];
    if (hitSlot >= 0) {
        _selectedSlot = hitSlot;
        [self setNeedsDisplay:YES];
        if ([self.delegate respondsToSelector:@selector(keyboardViewDidSelectKey:label:)]) {
            NSString *lbl = [self labelForSlot:hitSlot];
            [self.delegate keyboardViewDidSelectKey:hitSlot label:lbl];
        }
    }
}

- (void)keyDown:(NSEvent *)event {
    int targetSlot = -1;
    unsigned short code = event.keyCode;

    // 1. Spezielle Tasten über macOS Virtual Key Codes
    switch (code) {
        case 53:  targetSlot = 1;  break; // Esc
        case 48:  targetSlot = 2;  break; // Tab
        case 57:  targetSlot = 3;  break; // Caps
        case 56:  targetSlot = 4;  break; // Left Shift
        case 60:  targetSlot = 82; break; // Right Shift
        case 59:  targetSlot = 5;  break; // Left Ctrl
        case 58:  targetSlot = 17; break; // Left Alt
        case 55:  targetSlot = 11; break; // Left Cmd/Win
        case 61:  targetSlot = 53; break; // Right Alt / AltGr
        case 49:  targetSlot = 35; break; // Space
        case 36:  targetSlot = 81; break; // Return
        case 51:  targetSlot = 79; break; // Backspace
        case 117: targetSlot = 86; break; // Delete (Entf)
        case 116: targetSlot = 87; break; // Page Up (Bild auf)
        case 121: targetSlot = 94; break; // Page Down (Bild ab)
        case 119: targetSlot = 91; break; // End (Ende)
        case 126: targetSlot = 88; break; // Up Arrow
        case 123: targetSlot = 83; break; // Left Arrow
        case 125: targetSlot = 89; break; // Down Arrow
        case 124: targetSlot = 95; break; // Right Arrow
        default: break;
    }

    // 2. Reguläre Zeichen (Buchstaben, Ziffern, Satzzeichen)
    if (targetSlot < 0) {
        NSString *chars = [event.charactersIgnoringModifiers uppercaseString];
        if (chars.length > 0) {
            NSString *ch = [chars substringToIndex:1];
            const KeyItem *rows[] = {ROW_0, ROW_1, ROW_2, ROW_3, ROW_4};
            const size_t rowCounts[] = {
                sizeof(ROW_0)/sizeof(ROW_0[0]),
                sizeof(ROW_1)/sizeof(ROW_1[0]),
                sizeof(ROW_2)/sizeof(ROW_2[0]),
                sizeof(ROW_3)/sizeof(ROW_3[0]),
                sizeof(ROW_4)/sizeof(ROW_4[0])
            };
            // 1. Pass: Exakte Übereinstimmung (z. B. "B", "E", "A", "S", "1", ...)
            for (int r = 0; r < 5 && targetSlot < 0; r++) {
                for (size_t c = 0; c < rowCounts[r]; c++) {
                    NSString *itemLabel = [NSString stringWithUTF8String:rows[r][c].label];
                    if ([itemLabel isEqualToString:ch]) {
                        targetSlot = rows[r][c].slot;
                        break;
                    }
                }
            }
            // 2. Pass: Falls kein exakter Match, Mehrfach-Labels prüfen (z. B. "+ *", ", ;", "- _")
            if (targetSlot < 0) {
                for (int r = 0; r < 5 && targetSlot < 0; r++) {
                    for (size_t c = 0; c < rowCounts[r]; c++) {
                        NSString *itemLabel = [NSString stringWithUTF8String:rows[r][c].label];
                        if ([itemLabel hasPrefix:ch]) {
                            targetSlot = rows[r][c].slot;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (targetSlot >= 0) {
        _selectedSlot = targetSlot;
        [self setNeedsDisplay:YES];
        if ([self.delegate respondsToSelector:@selector(keyboardViewDidSelectKey:label:)]) {
            NSString *lbl = [self labelForSlot:targetSlot];
            [self.delegate keyboardViewDidSelectKey:targetSlot label:lbl];
        }
    } else {
        [super keyDown:event];
    }
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    int hitSlot = [self slotAtPoint:loc];
    if (hitSlot != _hoveredSlot) {
        _hoveredSlot = hitSlot;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseExited:(NSEvent *)event {
    (void)event;
    if (_hoveredSlot != -1) {
        _hoveredSlot = -1;
        [self setNeedsDisplay:YES];
    }
}

- (int)slotAtPoint:(NSPoint)pt {
    NSRect bounds = self.bounds;
    CGFloat margin = 12.0;
    CGFloat usableW = bounds.size.width - (margin * 2);
    CGFloat usableH = bounds.size.height - (margin * 2);
    CGFloat unitW = (usableW - 14 * 4.0) / 16.0;
    CGFloat keyH  = (usableH - 4 * 4.0) / 5.0;
    CGFloat gap = 4.0;

    const KeyItem *rows[] = {ROW_0, ROW_1, ROW_2, ROW_3, ROW_4};
    const size_t rowCounts[] = {
        sizeof(ROW_0)/sizeof(ROW_0[0]),
        sizeof(ROW_1)/sizeof(ROW_1[0]),
        sizeof(ROW_2)/sizeof(ROW_2[0]),
        sizeof(ROW_3)/sizeof(ROW_3[0]),
        sizeof(ROW_4)/sizeof(ROW_4[0])
    };

    for (int r = 0; r < 5; r++) {
        CGFloat y = bounds.size.height - margin - (r + 1) * keyH - r * gap;
        CGFloat x = margin;
        for (size_t c = 0; c < rowCounts[r]; c++) {
            KeyItem item = rows[r][c];
            CGFloat kw = item.width * unitW + (item.width - 1.0f) * gap;
            NSRect keyRect = NSMakeRect(x, y, kw, keyH);
            if (NSPointInRect(pt, keyRect)) {
                return item.slot;
            }
            x += kw + gap;
        }
    }
    return -1;
}

- (NSString *)labelForSlot:(int)slot {
    const KeyItem *rows[] = {ROW_0, ROW_1, ROW_2, ROW_3, ROW_4};
    const size_t rowCounts[] = {
        sizeof(ROW_0)/sizeof(ROW_0[0]),
        sizeof(ROW_1)/sizeof(ROW_1[0]),
        sizeof(ROW_2)/sizeof(ROW_2[0]),
        sizeof(ROW_3)/sizeof(ROW_3[0]),
        sizeof(ROW_4)/sizeof(ROW_4[0])
    };
    for (int r = 0; r < 5; r++) {
        for (size_t c = 0; c < rowCounts[r]; c++) {
            if (rows[r][c].slot == slot) {
                return [NSString stringWithUTF8String:rows[r][c].label];
            }
        }
    }
    return [NSString stringWithFormat:@"Slot %d", slot];
}

@end
