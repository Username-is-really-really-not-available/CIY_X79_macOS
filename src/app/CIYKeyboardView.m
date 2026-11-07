#import "CIYKeyboardView.h"
#import "../core/ciy_hid.h"
#import "../layouts/ciy_layout.h"

@interface CIYColorAction : NSObject
@property (nonatomic, copy) NSDictionary<NSNumber *, NSColor *> *previousColors;
@property (nonatomic, copy) NSDictionary<NSNumber *, NSColor *> *newColors;
@property (nonatomic, assign) CFAbsoluteTime timestamp;
@property (nonatomic, assign) BOOL isContinuous;
- (NSDictionary<NSNumber *, NSColor *> *)newColors __attribute__((objc_method_family(none)));
@end

@implementation CIYColorAction
@end

static const NSUInteger kMaxUndoSteps = 50;

static int CIYColorChannel8(CGFloat value) {
    int channel = (int)(value * 255.0 + 0.5);
    if (channel < 0) return 0;
    if (channel > 255) return 255;
    return channel;
}

static BOOL CIYColorEqualsRGB(NSColor *a, NSColor *b) {
    if (a == b) return YES;
    if (!a || !b) return NO;

    NSColor *srgbA = [a colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    NSColor *srgbB = [b colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!srgbA || !srgbB) return NO;

    CGFloat ar = 0, ag = 0, ab = 0, aa = 0;
    CGFloat br = 0, bg = 0, bb = 0, ba = 0;
    [srgbA getRed:&ar green:&ag blue:&ab alpha:&aa];
    [srgbB getRed:&br green:&bg blue:&bb alpha:&ba];

    return CIYColorChannel8(ar) == CIYColorChannel8(br) &&
           CIYColorChannel8(ag) == CIYColorChannel8(bg) &&
           CIYColorChannel8(ab) == CIYColorChannel8(bb);
}

static BOOL CIYColorDictionariesEqual(NSDictionary<NSNumber *, NSColor *> *a,
                                      NSDictionary<NSNumber *, NSColor *> *b) {
    if (a == b) return YES;
    if (!a || !b || a.count != b.count) return NO;

    for (NSNumber *key in a) {
        NSColor *colorB = b[key];
        if (!colorB || !CIYColorEqualsRGB(a[key], colorB)) return NO;
    }
    return YES;
}

@interface CIYKeyboardView () {
    NSMutableDictionary<NSNumber *, NSColor *> *_keyColors;
    NSMutableDictionary<NSNumber *, NSColor *> *_overlayColors;
    NSMutableArray<CIYColorAction *> *_undoStack;
    NSMutableArray<CIYColorAction *> *_redoStack;
    NSTrackingArea *_trackingArea;
    int _hoveredSlot;
    BOOL _coalescingSuspended;
}
@end

@implementation CIYKeyboardView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _keyColors = [NSMutableDictionary dictionary];
        _overlayColors = [NSMutableDictionary dictionary];
        _undoStack = [NSMutableArray array];
        _redoStack = [NSMutableArray array];
        _selectedSlot = -1;
        _hoveredSlot = -1;
        [self resetAllKeysToColor:[NSColor colorWithCalibratedRed:0.85 green:0.1 blue:0.1 alpha:1.0]]; // Red default
        [self clearUndoHistory]; // Initialzustand nicht als Undo-Aktion werten
    }
    return self;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                 options:(NSTrackingMouseMoved |
                                                          NSTrackingMouseEnteredAndExited |
                                                          NSTrackingActiveInKeyWindow |
                                                          NSTrackingInVisibleRect)
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)invalidateSlot:(int)slot {
    if (slot < 0) return;

    NSRect bounds = self.bounds;
    CGFloat margin = 12.0;
    CGFloat usableW = bounds.size.width - (margin * 2);
    CGFloat usableH = bounds.size.height - (margin * 2);
    CGFloat unitW = (usableW - 14 * 4.0) / 16.0;
    CGFloat keyH  = (usableH - 4 * 4.0) / 5.0;
    CGFloat gap = 4.0;

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    int currentRow = -1;
    CGFloat x = margin;
    CGFloat y = 0.0;

    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *surf = &layout->surfaces[i];
        if (surf->row != currentRow) {
            currentRow = surf->row;
            x = margin;
            y = bounds.size.height - margin - (currentRow + 1) * keyH - currentRow * gap;
        }
        CGFloat kw = surf->width_u * unitW + (surf->width_u - 1.0f) * gap;
        NSRect keyRect = NSMakeRect(x, y, kw, keyH);
        if (surf->matrix_slot == slot) {
            [self setNeedsDisplayInRect:NSInsetRect(keyRect, -4.0, -4.0)];
        }
        x += kw + gap;
    }
}

- (BOOL)resetAllKeysToColor:(NSColor *)color {
    if (!color) return NO;

    NSMutableDictionary<NSNumber *, NSColor *> *newColors = [NSMutableDictionary dictionaryWithCapacity:CIY_LED_COUNT];
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        newColors[@(i)] = color;
    }

    BOOL anyChanged = (_keyColors.count != newColors.count);
    if (!anyChanged) {
        for (int i = 0; i < CIY_LED_COUNT - 1; i++) {
            NSColor *prev = _keyColors[@(i)];
            if (!prev || !CIYColorEqualsRGB(prev, color)) {
                anyChanged = YES;
                break;
            }
        }
    }
    if (!anyChanged) return NO;

    if (_keyColors.count > 0) {
        [self recordColorChangeFrom:[_keyColors copy] to:newColors continuous:NO];
    }
    [_keyColors removeAllObjects];
    [_keyColors addEntriesFromDictionary:newColors];
    [self setNeedsDisplay:YES];
    return YES;
}

- (NSDictionary<NSNumber *, NSColor *> *)allKeyColors {
    return [_keyColors copy];
}

- (BOOL)applyKeyColors:(NSDictionary<NSNumber *, NSColor *> *)colors {
    return [self applyKeyColors:colors registerUndo:NO];
}

- (BOOL)applyKeyColors:(NSDictionary<NSNumber *, NSColor *> *)colors registerUndo:(BOOL)registerUndo {
    if (!colors || colors.count == 0) return NO;

    NSMutableDictionary<NSNumber *, NSColor *> *oldColors = [NSMutableDictionary dictionaryWithCapacity:colors.count];
    BOOL anyChanged = NO;
    for (NSNumber *slotNum in colors) {
        NSColor *prev = _keyColors[slotNum];
        NSColor *next = colors[slotNum];
        if (!prev) {
            anyChanged = YES;
            prev = [NSColor colorWithCalibratedRed:0.2 green:0.2 blue:0.2 alpha:1.0];
        } else if (!CIYColorEqualsRGB(prev, next)) {
            anyChanged = YES;
        }
        if (registerUndo) {
            oldColors[slotNum] = prev;
        }
    }
    if (registerUndo && anyChanged) {
        [self recordColorChangeFrom:oldColors to:colors continuous:NO];
    }
    [_keyColors addEntriesFromDictionary:colors];
    [self setNeedsDisplay:YES];
    return anyChanged;
}

- (BOOL)setColor:(NSColor *)color forSlot:(int)slot {
    return [self setColor:color forSlot:slot continuous:NO];
}

- (BOOL)setColor:(NSColor *)color forSlot:(int)slot continuous:(BOOL)continuous {
    if (slot < 0 || !color) return NO;

    NSColor *prev = _keyColors[@(slot)];
    if (prev && CIYColorEqualsRGB(prev, color)) {
        return NO;
    }
    if (!prev) {
        prev = [NSColor colorWithCalibratedRed:0.2 green:0.2 blue:0.2 alpha:1.0];
    }

    [self recordColorChangeFrom:@{@(slot): prev}
                             to:@{@(slot): color}
                     continuous:continuous];
    _keyColors[@(slot)] = color;
    [self invalidateSlot:slot];
    return YES;
}

- (void)setOverlayColors:(nullable NSDictionary<NSNumber *, NSColor *> *)overlayColors {
    if (overlayColors) {
        [_overlayColors setDictionary:overlayColors];
    } else {
        [_overlayColors removeAllObjects];
    }
    [self setNeedsDisplay:YES];
}

- (void)clearOverlayColors {
    if (_overlayColors.count > 0) {
        [_overlayColors removeAllObjects];
        [self setNeedsDisplay:YES];
    }
}

- (nullable NSDictionary<NSNumber *, NSColor *> *)overlayColors {
    return _overlayColors.count > 0 ? [_overlayColors copy] : nil;
}

- (BOOL)applyColor:(NSColor *)color toSlots:(NSArray<NSNumber *> *)slots {
    if (!color || !slots || slots.count == 0) return NO;
    NSMutableDictionary<NSNumber *, NSColor *> *oldColors = [NSMutableDictionary dictionaryWithCapacity:slots.count];
    NSMutableDictionary<NSNumber *, NSColor *> *newColors = [NSMutableDictionary dictionaryWithCapacity:slots.count];
    BOOL anyChanged = NO;
    for (NSNumber *slotNum in slots) {
        int slot = [slotNum intValue];
        NSColor *prev = _keyColors[@(slot)];
        if (!prev) {
            anyChanged = YES;
            prev = [NSColor colorWithCalibratedRed:0.2 green:0.2 blue:0.2 alpha:1.0];
        } else if (!CIYColorEqualsRGB(prev, color)) {
            anyChanged = YES;
        }
        oldColors[@(slot)] = prev;
        newColors[@(slot)] = color;
        _keyColors[@(slot)] = color;
        [self invalidateSlot:slot];
    }
    if (anyChanged) {
        [self recordColorChangeFrom:oldColors to:newColors continuous:NO];
    }
    return anyChanged;
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

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    int currentRow = -1;
    CGFloat x = margin;
    CGFloat y = 0.0;

    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *surf = &layout->surfaces[i];
        if (surf->row != currentRow) {
            currentRow = surf->row;
            x = margin;
            y = bounds.size.height - margin - (currentRow + 1) * keyH - currentRow * gap;
        }
        CGFloat kw = surf->width_u * unitW + (surf->width_u - 1.0f) * gap;
        NSRect keyRect = NSMakeRect(x, y, kw, keyH);

        if (NSIntersectsRect(NSInsetRect(keyRect, -3.0, -3.0), dirtyRect)) {
            [self drawSurface:surf inRect:keyRect];
        }
        x += kw + gap;
    }
}

- (void)drawSurface:(const CIYKeySurface *)surface inRect:(NSRect)rect {
    NSBezierPath *keyPath = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:5 yRadius:5];

    // Base key cap body
    NSColor *defaultColor = [NSColor colorWithCalibratedRed:0.2 green:0.2 blue:0.2 alpha:1.0];
    NSColor *rawGlow = _overlayColors[@(surface->matrix_slot)] ?: _keyColors[@(surface->matrix_slot)];
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
    if (surface->matrix_slot == _selectedSlot) {
        [[NSColor whiteColor] setStroke];
        [keyPath setLineWidth:2.5];
        [keyPath stroke];
    } else if (surface->matrix_slot == _hoveredSlot) {
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
    NSString *text = [NSString stringWithUTF8String:surface->label];
    static NSParagraphStyle *cachedStyle = nil;
    static NSFont *cachedSmallFont = nil;
    static NSFont *cachedRegularFont = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
        style.alignment = NSTextAlignmentCenter;
        cachedStyle = [style copy];
        cachedSmallFont = [NSFont systemFontOfSize:9.5 weight:NSFontWeightMedium];
        cachedRegularFont = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    });

    // Font size depends on label length
    NSFont *font = (text.length > 3) ? cachedSmallFont : cachedRegularFont;

    // Text color is lit up by key's color
    NSColor *textColor = [NSColor colorWithCalibratedRed:0.7 + r * 0.3
                                                   green:0.7 + g * 0.3
                                                    blue:0.7 + b * 0.3
                                                   alpha:1.0];

    NSDictionary *attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: textColor,
        NSParagraphStyleAttributeName: cachedStyle
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
        int oldSlot = _selectedSlot;
        _selectedSlot = hitSlot;
        if (oldSlot != _selectedSlot) {
            [self invalidateSlot:oldSlot];
        }
        [self invalidateSlot:_selectedSlot];
        if ([self.delegate respondsToSelector:@selector(keyboardViewDidSelectKey:label:)]) {
            NSString *lbl = [self labelForSlot:hitSlot];
            [self.delegate keyboardViewDidSelectKey:hitSlot label:lbl];
        }
    }
}

- (void)keyDown:(NSEvent *)event {
    if (event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) {
        [super keyDown:event];
        return;
    }

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
        // Deutsche ISO-Sonder- und Umlauttasten
        case 50:  targetSlot = 76; break; // < > | (ISO Section Key)
        case 42:  targetSlot = 75; break; // # '
        case 24:  targetSlot = 74; break; // + *
        case 27:  targetSlot = 67; break; // ß ?
        case 30:  targetSlot = 73; break; // ´ `
        case 33:  targetSlot = 68; break; // Ü
        case 41:  targetSlot = 63; break; // Ö
        case 39:  targetSlot = 69; break; // Ä
        case 44:  targetSlot = 64; break; // - _
        case 43:  targetSlot = 52; break; // , ;
        case 47:  targetSlot = 58; break; // . :
        default: break;
    }

    // 2. Reguläre Zeichen (Buchstaben, Ziffern, Satzzeichen)
    if (targetSlot < 0) {
        NSString *chars = [event.charactersIgnoringModifiers uppercaseString];
        if (chars.length > 0) {
            NSString *ch = [chars substringToIndex:1];
            const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
            // 1. Pass: Exakte Übereinstimmung (z. B. "B", "E", "A", "S", "1", ...)
            for (size_t i = 0; i < layout->surface_count; i++) {
                NSString *itemLabel = [NSString stringWithUTF8String:layout->surfaces[i].label];
                if ([itemLabel isEqualToString:ch]) {
                    targetSlot = layout->surfaces[i].matrix_slot;
                    break;
                }
            }
            // 2. Pass: Falls kein exakter Match, Mehrfach-Labels prüfen (z. B. "+ *", ", ;", "- _")
            if (targetSlot < 0) {
                for (size_t i = 0; i < layout->surface_count; i++) {
                    NSString *itemLabel = [NSString stringWithUTF8String:layout->surfaces[i].label];
                    if ([itemLabel hasPrefix:ch]) {
                        targetSlot = layout->surfaces[i].matrix_slot;
                        break;
                    }
                }
            }
        }
    }

    if (targetSlot >= 0) {
        int oldSlot = _selectedSlot;
        _selectedSlot = targetSlot;
        if (oldSlot != _selectedSlot) {
            [self invalidateSlot:oldSlot];
        }
        [self invalidateSlot:_selectedSlot];
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
        int oldSlot = _hoveredSlot;
        _hoveredSlot = hitSlot;
        [self invalidateSlot:oldSlot];
        [self invalidateSlot:_hoveredSlot];
    }
}

- (void)mouseExited:(NSEvent *)event {
    (void)event;
    if (_hoveredSlot != -1) {
        int oldSlot = _hoveredSlot;
        _hoveredSlot = -1;
        [self invalidateSlot:oldSlot];
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

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    int currentRow = -1;
    CGFloat x = margin;
    CGFloat y = 0.0;

    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *surf = &layout->surfaces[i];
        if (surf->row != currentRow) {
            currentRow = surf->row;
            x = margin;
            y = bounds.size.height - margin - (currentRow + 1) * keyH - currentRow * gap;
        }
        CGFloat kw = surf->width_u * unitW + (surf->width_u - 1.0f) * gap;
        NSRect keyRect = NSMakeRect(x, y, kw, keyH);
        if (NSPointInRect(pt, keyRect)) {
            return surf->matrix_slot;
        }
        x += kw + gap;
    }
    return -1;
}

- (NSString *)labelForSlot:(int)slot {
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    const CIYLogicalKey *key = ciy_layout_find_key_by_slot(layout, slot);
    if (key && key->name) {
        return [NSString stringWithUTF8String:key->name];
    }
    return [NSString stringWithFormat:@"Slot %d", slot];
}

- (BOOL)matchesProfileSlots:(NSDictionary *)savedSlots {
    if (![savedSlots isKindOfClass:[NSDictionary class]] || savedSlots.count == 0) {
        return NO;
    }

    uint8_t expectedR[CIY_LED_COUNT - 1] = {0};
    uint8_t expectedG[CIY_LED_COUNT - 1] = {0};
    uint8_t expectedB[CIY_LED_COUNT - 1] = {0};
    NSUInteger validSlotCount = 0;

    for (id slotKey in savedSlots) {
        if (![slotKey isKindOfClass:[NSString class]]) return NO;

        NSScanner *scanner = [NSScanner scannerWithString:(NSString *)slotKey];
        int slot = -1;
        if (![scanner scanInt:&slot] || !scanner.isAtEnd) return NO;
        if (slot < 0 || slot >= CIY_LED_COUNT - 1) continue;

        id rawHex = savedSlots[(NSString *)slotKey];
        if (![rawHex isKindOfClass:[NSString class]]) return NO;

        uint8_t r = 0, g = 0, b = 0;
        if (!ciy_parse_hex_color([(NSString *)rawHex UTF8String], &r, &g, &b)) {
            return NO;
        }
        expectedR[slot] = r;
        expectedG[slot] = g;
        expectedB[slot] = b;
        validSlotCount++;
    }

    if (validSlotCount == 0) return NO;

    for (int slot = 0; slot < CIY_LED_COUNT - 1; slot++) {
        NSColor *current = _keyColors[@(slot)];
        if (!current) return NO;
        NSColor *srgb = [current colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
        if (!srgb) return NO;

        CGFloat r = 0, g = 0, b = 0, a = 0;
        [srgb getRed:&r green:&g blue:&b alpha:&a];
        if (CIYColorChannel8(r) != expectedR[slot] ||
            CIYColorChannel8(g) != expectedG[slot] ||
            CIYColorChannel8(b) != expectedB[slot]) {
            return NO;
        }
    }
    return YES;
}

#pragma mark - Undo / Redo

- (void)recordColorChangeFrom:(NSDictionary<NSNumber *, NSColor *> *)oldColors
                           to:(NSDictionary<NSNumber *, NSColor *> *)newColors
                   continuous:(BOOL)continuous {
    if (!oldColors || !newColors || oldColors.count == 0 || newColors.count == 0) {
        return;
    }
    if (CIYColorDictionariesEqual(oldColors, newColors)) {
        return;
    }

    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();

    if (continuous && !_coalescingSuspended && _redoStack.count == 0 && _undoStack.count > 0) {
        CIYColorAction *lastAction = _undoStack.lastObject;
        if (lastAction.isContinuous &&
            (now - lastAction.timestamp) < 0.8 &&
            [[NSSet setWithArray:lastAction.newColors.allKeys] isEqualToSet:[NSSet setWithArray:newColors.allKeys]]) {
            // Coalescing: previousColors bleibt unverändert, newColors wird aktualisiert.
            lastAction.newColors = [newColors copy];
            lastAction.timestamp = now;
            // Eine Serie, die exakt zur Ausgangsfarbe zurückkehrt, darf keine
            // wirkungslose Undo-Aktion hinterlassen.
            if (CIYColorDictionariesEqual(lastAction.previousColors, lastAction.newColors)) {
                [_undoStack removeLastObject];
                _coalescingSuspended = YES;
            }
            return;
        }
    }

    CIYColorAction *action = [[CIYColorAction alloc] init];
    action.previousColors = [oldColors copy];
    action.newColors = [newColors copy];
    action.timestamp = now;
    action.isContinuous = continuous;

    [_undoStack addObject:action];
    if (_undoStack.count > kMaxUndoSteps) {
        [_undoStack removeObjectAtIndex:0];
    }
    [_redoStack removeAllObjects];
    _coalescingSuspended = NO;
}

- (IBAction)undo:(nullable id)sender {
    (void)sender;
    if (![self canUndo]) return;
    CIYColorAction *action = _undoStack.lastObject;
    [_undoStack removeLastObject];

    for (NSNumber *slotNum in action.previousColors) {
        _keyColors[slotNum] = action.previousColors[slotNum];
        [self invalidateSlot:slotNum.intValue];
    }
    [_redoStack addObject:action];
    _coalescingSuspended = YES;

    if ([self.delegate respondsToSelector:@selector(keyboardViewDidUndo:)]) {
        [self.delegate keyboardViewDidUndo:self];
    }
}

- (IBAction)redo:(nullable id)sender {
    (void)sender;
    if (![self canRedo]) return;
    CIYColorAction *action = _redoStack.lastObject;
    [_redoStack removeLastObject];

    for (NSNumber *slotNum in action.newColors) {
        _keyColors[slotNum] = action.newColors[slotNum];
        [self invalidateSlot:slotNum.intValue];
    }
    [_undoStack addObject:action];
    _coalescingSuspended = YES;

    if ([self.delegate respondsToSelector:@selector(keyboardViewDidRedo:)]) {
        [self.delegate keyboardViewDidRedo:self];
    }
}

- (BOOL)canUndo {
    return _undoStack.count > 0;
}

- (BOOL)canRedo {
    return _redoStack.count > 0;
}

- (void)clearUndoHistory {
    [_undoStack removeAllObjects];
    [_redoStack removeAllObjects];
    _coalescingSuspended = NO;
}

- (NSUInteger)undoCount {
    return _undoStack.count;
}

- (NSUInteger)redoCount {
    return _redoStack.count;
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    if (menuItem.action == @selector(undo:)) {
        return [self canUndo];
    }
    if (menuItem.action == @selector(redo:)) {
        return [self canRedo];
    }
    return YES;
}

@end
