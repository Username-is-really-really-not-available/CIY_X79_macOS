#import "CIYKeyboardView.h"
#import "../layouts/ciy_layout.h"

@interface CIYKeyboardView () {
    NSMutableDictionary<NSNumber *, NSColor *> *_keyColors;
    NSTrackingArea *_trackingArea;
    int _hoveredSlot;
}
@end

@implementation CIYKeyboardView

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
    [self invalidateSlot:slot];
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
    NSColor *rawGlow = _keyColors[@(surface->matrix_slot)];
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

@end
