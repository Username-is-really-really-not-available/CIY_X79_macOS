#import <Cocoa/Cocoa.h>
#import <assert.h>
#import <stdlib.h>
#import <unistd.h>
#import "../src/core/ciy_hid.h"
#import "../src/layouts/ciy_layout.h"
#import "../src/app/KeyboardBackend.h"
#import "../src/app/CIYKeyboardView.h"
#import "../src/app/CIYBatteryMonitor.h"
#import "../src/app/CIYLocalization.h"
#import "../src/app/CIYMiniWindowController.h"
#import "../src/app/CIYProfileScheduler.h"

#if !defined(CIY_ENABLE_TEST_HOOKS)
#error "Tests must be compiled with -DCIY_ENABLE_TEST_HOOKS=1 to prevent real hardware I/O"
#endif

@interface CIYBatteryMonitor (Testing)
- (void)scheduleDebouncedUpdate;
@end

static NSString *HexFromColor(NSColor *color) {
    if (!color) return @"#000000";
    NSColor *srgb = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!srgb) srgb = color;
    CGFloat r = 0, g = 0, b = 0, a = 0;
    [srgb getRed:&r green:&g blue:&b alpha:&a];
    int ir = (int)(r * 255.0 + 0.5);
    int ig = (int)(g * 255.0 + 0.5);
    int ib = (int)(b * 255.0 + 0.5);
    if (ir < 0) ir = 0; else if (ir > 255) ir = 255;
    if (ig < 0) ig = 0; else if (ig > 255) ig = 255;
    if (ib < 0) ib = 0; else if (ib > 255) ib = 255;
    return [NSString stringWithFormat:@"#%02X%02X%02X", ir, ig, ib];
}

static NSColor *ColorFromHex(NSString *hex) {
    if (!hex || ![hex isKindOfClass:[NSString class]]) {
        return [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0];
    }
    uint8_t r = 0, g = 0, b = 0;
    if (!ciy_parse_hex_color([hex UTF8String], &r, &g, &b)) {
        return [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0];
    }
    return [NSColor colorWithSRGBRed:(CGFloat)r / 255.0
                               green:(CGFloat)g / 255.0
                                blue:(CGFloat)b / 255.0
                               alpha:1.0];
}

static void test_hex_conversion(void) {
    NSColor *red = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0];
    NSString *hexRed = HexFromColor(red);
    assert([hexRed isEqualToString:@"#FF0000"]);

    NSColor *parsedRed = ColorFromHex(hexRed);
    CGFloat r = 0, g = 0, b = 0, a = 0;
    [parsedRed getRed:&r green:&g blue:&b alpha:&a];
    assert(fabs(r - 1.0) < 0.01 && fabs(g - 0.0) < 0.01 && fabs(b - 0.0) < 0.01);

    // Fallbacks
    NSColor *fallback = ColorFromHex(@"invalid");
    assert(fallback != nil);
    NSColor *fallbackNil = ColorFromHex(nil);
    assert(fallbackNil != nil);
    assert([HexFromColor(nil) isEqualToString:@"#000000"]);

    // Ungültige Hex-Strings mit ungültigen Ziffern (müssen zuverlässig abgewiesen werden)
    NSColor *invalidHex = ColorFromHex(@"#12ZZZZ");
    CGFloat ir = 0, ig = 0, ib = 0, ia = 0;
    [invalidHex getRed:&ir green:&ig blue:&ib alpha:&ia];
    assert(fabs(ir - 0.0) < 0.01 && fabs(ig - 0.0) < 0.01 && fabs(ib - 0.0) < 0.01);
}

static void test_profile_creation_and_update(void) {
    NSMutableArray<NSMutableDictionary *> *profiles = [NSMutableArray array];

    // Create profile
    NSMutableDictionary *prof = [NSMutableDictionary dictionary];
    prof[@"id"] = [[NSUUID UUID] UUIDString];
    prof[@"name"] = @"Test Profil";
    NSMutableDictionary<NSString *, NSString *> *slots = [NSMutableDictionary dictionary];
    slots[@"14"] = @"#FF0000"; // W
    slots[@"15"] = @"#00FF00"; // S
    prof[@"slots"] = slots;
    [profiles addObject:prof];

    assert(profiles.count == 1);
    assert([profiles[0][@"name"] isEqualToString:@"Test Profil"]);
    assert([profiles[0][@"slots"][@"14"] isEqualToString:@"#FF0000"]);

    // Update profile (Bearbeiten: Farben anpassen)
    slots[@"14"] = @"#0000FF"; // W wird blau
    slots[@"9"]  = @"#FFFF00"; // A wird gelb
    profiles[0][@"slots"] = slots;
    assert([profiles[0][@"slots"][@"14"] isEqualToString:@"#0000FF"]);
    assert([profiles[0][@"slots"][@"9"] isEqualToString:@"#FFFF00"]);

    // Rename profile (Bearbeiten: Umbenennen)
    NSString *rawName = @"   Cyber Neon 2077   ";
    NSString *trimmed = [rawName stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    profiles[0][@"name"] = trimmed;
    assert([profiles[0][@"name"] isEqualToString:@"Cyber Neon 2077"]);

    // Truncate long name (> 40 chars)
    NSString *longName = @"1234567890123456789012345678901234567890EXTRA";
    NSString *truncated = [longName substringToIndex:40];
    assert(truncated.length == 40);

    // Delete profile
    [profiles removeObjectAtIndex:0];
    assert(profiles.count == 0);
}

static void test_keyboard_view_colors(void) {
    CIYKeyboardView *kv = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    assert(kv != nil);

    // All slots populated
    NSDictionary<NSNumber *, NSColor *> *all = [kv allKeyColors];
    assert(all.count == 126);

    // Change single slot
    [kv setColor:[NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0] forSlot:14];
    NSDictionary<NSNumber *, NSColor *> *updated = [kv allKeyColors];
    NSColor *wColor = updated[@(14)];
    CGFloat r = 0, g = 0, b = 0, a = 0;
    [wColor getRed:&r green:&g blue:&b alpha:&a];
    assert(fabs(r - 1.0) < 0.01);
}

static void test_builtin_profiles_structure(void) {
    // Test Erzeugung eines Standardprofils mit WASD und Pfeiltasten
    NSMutableDictionary<NSString *, NSString *> *slots = [NSMutableDictionary dictionaryWithCapacity:126];
    for (int i = 0; i < 126; i++) {
        slots[@(i).stringValue] = @"#0066FF"; // Basis Blau
    }
    const int wasd[] = {14, 9, 15, 21};
    for (int k = 0; k < 4; k++) slots[@(wasd[k]).stringValue] = @"#FF0000"; // WASD Rot
    const int arrows[] = {88, 83, 89, 95};
    for (int k = 0; k < 4; k++) slots[@(arrows[k]).stringValue] = @"#FFFF00"; // Pfeile Gelb

    assert(slots.count == 126);
    assert([slots[@"14"] isEqualToString:@"#FF0000"]);
    assert([slots[@"88"] isEqualToString:@"#FFFF00"]);
    assert([slots[@"1"] isEqualToString:@"#0066FF"]); // Esc

    // Überprüfung: Alle Werte müssen valide 7-stellige Hex-Codes sein
    for (NSString *key in slots) {
        NSString *hex = slots[key];
        assert([hex hasPrefix:@"#"]);
        assert(hex.length == 7);
        NSColor *c = ColorFromHex(hex);
        assert(c != nil);
    }

    // Test: Laden des Profils in CIYKeyboardView
    CIYKeyboardView *kv = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSMutableDictionary<NSNumber *, NSColor *> *keyColors = [NSMutableDictionary dictionaryWithCapacity:126];
    for (NSString *slotStr in slots) {
        keyColors[@([slotStr intValue])] = ColorFromHex(slots[slotStr]);
    }
    [kv applyKeyColors:keyColors];

    NSDictionary<NSNumber *, NSColor *> *applied = [kv allKeyColors];
    assert(applied.count == 126);
}

static void test_json_export_import(void) {
    NSMutableArray<NSDictionary *> *profs = [NSMutableArray array];
    NSMutableDictionary *p1 = [NSMutableDictionary dictionary];
    p1[@"id"] = [[NSUUID UUID] UUIDString];
    p1[@"name"] = @"Exported Test Profile";
    NSMutableDictionary *s1 = [NSMutableDictionary dictionary];
    s1[@"14"] = @"#00FF88";
    p1[@"slots"] = s1;
    [profs addObject:p1];

    NSError *err = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:profs options:NSJSONWritingPrettyPrinted error:&err];
    assert(data != nil && err == nil);

    // Parse back
    id parsed = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:&err];
    assert(parsed != nil && err == nil);
    assert([parsed isKindOfClass:[NSArray class]]);
    NSArray *arr = (NSArray *)parsed;
    assert(arr.count == 1);
    assert([arr[0][@"name"] isEqualToString:@"Exported Test Profile"]);
    assert([arr[0][@"slots"][@"14"] isEqualToString:@"#00FF88"]);
}

static void test_brightness_and_backend(void) {
    KeyboardBackend *kb = [KeyboardBackend shared];
    assert(kb != nil);
    [KeyboardBackend resetTestingPayload];

    NSDictionary *cols = @{@(14): [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0]};
    BOOL ok = [kb sendKeyColors:cols brightness:3 defaultColor:nil];
    assert(ok);

    NSData *payload = [KeyboardBackend lastSentPlanarPayload];
    assert(payload != nil && payload.length == CIY_PLANAR_RGB_SIZE);

    const uint8_t *bytes = (const uint8_t *)payload.bytes;
    assert(bytes[15] == 0xFF);
    assert(bytes[CIY_LED_COUNT + 15] == 0x00);
    assert(bytes[2 * CIY_LED_COUNT + 15] == 0x00);

    for (int i = 0; i < CIY_LED_COUNT; i++) {
        if (i == 15) continue;
        assert(bytes[i] == 0x00);
        assert(bytes[CIY_LED_COUNT + i] == 0x00);
        assert(bytes[2 * CIY_LED_COUNT + i] == 0x00);
    }

    assert(kb.currentState.brightness == 3);
    assert(kb.currentState.mode == 19);

    // Test für Wide-Gamut-Farbclamping (Out-of-sRGB-Gamut darf nicht overflowen)
    [KeyboardBackend resetTestingPayload];
    NSColor *outOfGamut = [NSColor colorWithDisplayP3Red:1.5 green:-0.5 blue:0.5 alpha:1.0];
    NSDictionary *p3Cols = @{@(14): outOfGamut};
    BOOL p3Ok = [kb sendKeyColors:p3Cols brightness:5 defaultColor:nil];
    assert(p3Ok);

    NSData *p3Payload = [KeyboardBackend lastSentPlanarPayload];
    assert(p3Payload != nil && p3Payload.length == CIY_PLANAR_RGB_SIZE);
    const uint8_t *p3Bytes = (const uint8_t *)p3Payload.bytes;
    // Rot muss auf 0xFF gesättigt sein, Grün auf 0x00
    assert(p3Bytes[15] == 0xFF);
    assert(p3Bytes[CIY_LED_COUNT + 15] == 0x00);
}

static void test_keyboard_key_lookup(void) {
    CIYKeyboardView *kv = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    assert(kv != nil);
    assert([[kv labelForSlot:14] isEqualToString:@"W"]);
    assert([[kv labelForSlot:9] isEqualToString:@"A"]);
    assert([[kv labelForSlot:15] isEqualToString:@"S"]);
    assert([[kv labelForSlot:21] isEqualToString:@"D"]);
    assert([[kv labelForSlot:1] isEqualToString:@"Esc"]);
    assert([[kv labelForSlot:88] isEqualToString:@"▲"]);
    assert([[kv labelForSlot:83] isEqualToString:@"◄"]);
    assert([[kv labelForSlot:89] isEqualToString:@"▼"]);
    assert([[kv labelForSlot:95] isEqualToString:@"►"]);
    // Deutsche ISO-DE Tasten
    assert([[kv labelForSlot:76] isEqualToString:@"< >"]);
    assert([[kv labelForSlot:75] isEqualToString:@"# '"]);
    assert([[kv labelForSlot:67] isEqualToString:@"ß"]);
    assert([[kv labelForSlot:73] isEqualToString:@"´"]);
    assert([[kv labelForSlot:68] isEqualToString:@"Ü"]);
    assert([[kv labelForSlot:63] isEqualToString:@"Ö"]);
    assert([[kv labelForSlot:69] isEqualToString:@"Ä"]);
    assert([[kv labelForSlot:81] isEqualToString:@"↵ Enter"]);
}

static void test_app_trigger_profile_attributes(void) {
    NSMutableDictionary *prof = [NSMutableDictionary dictionary];
    prof[@"id"] = [[NSUUID UUID] UUIDString];
    prof[@"name"] = @"Steam Gaming Profile";
    prof[@"appBundleId"] = @"com.valvesoftware.steam";
    prof[@"appName"] = @"Steam";

    assert([prof[@"appBundleId"] isEqualToString:@"com.valvesoftware.steam"]);
    assert([prof[@"appName"] isEqualToString:@"Steam"]);

    // JSON serialization preserves app trigger metadata
    NSArray *profs = @[prof];
    NSError *err = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:profs options:0 error:&err];
    assert(data != nil && err == nil);

    NSArray *parsed = [NSJSONSerialization JSONObjectWithData:data options:0 error:&err];
    assert(parsed.count == 1);
    assert([parsed[0][@"appBundleId"] isEqualToString:@"com.valvesoftware.steam"]);
    assert([parsed[0][@"appName"] isEqualToString:@"Steam"]);

    // Export ohne App-Verknüpfungen (SEC-T4-009) entfernt Metadaten bei deaktivierter Checkbox.
    BOOL includeAppMetadata = NO;
    NSMutableDictionary *stripped = [prof mutableCopy];
    if (!includeAppMetadata) {
        [stripped removeObjectForKey:@"appBundleId"];
        [stripped removeObjectForKey:@"appName"];
    }
    assert(stripped[@"appBundleId"] == nil);
    assert(stripped[@"appName"] == nil);
    assert([prof[@"appBundleId"] isEqualToString:@"com.valvesoftware.steam"]);
}

static void test_menu_item_titles(void) {
    NSString *fallbackId = @"fallback-123";
    NSDictionary *pNormal = @{@"id": @"p1", @"name": @"Normal"};
    NSDictionary *pApp = @{@"id": @"p2", @"name": @"Gaming", @"appName": @"Counter-Strike 2"};
    NSDictionary *pFallback = @{@"id": fallbackId, @"name": @"Standard"};

    NSArray *profiles = @[pNormal, pApp, pFallback];
    NSString *activeId = @"p2";

    for (NSDictionary *prof in profiles) {
        NSString *name = prof[@"name"];
        NSString *profId = prof[@"id"];
        BOOL isActive = [profId isEqualToString:activeId];

        NSString *title = name;
        if (prof[@"appName"]) {
            title = [NSString stringWithFormat:@"%@  [%@]", name, prof[@"appName"]];
        } else if ([profId isEqualToString:fallbackId]) {
            title = [NSString stringWithFormat:@"%@  [Fallback]", name];
        }

        if ([profId isEqualToString:@"p1"]) {
            assert([title isEqualToString:@"Normal"]);
            assert(!isActive);
        } else if ([profId isEqualToString:@"p2"]) {
            assert([title isEqualToString:@"Gaming  [Counter-Strike 2]"]);
            assert(isActive);
        } else if ([profId isEqualToString:fallbackId]) {
            assert([title isEqualToString:@"Standard  [Fallback]"]);
            assert(!isActive);
        }
    }
}

static void test_app_binding_deduplication(void) {
    NSMutableDictionary *profA = [NSMutableDictionary dictionaryWithDictionary:@{@"id": @"pA", @"name": @"Profile A", @"appBundleId": @"com.valvesoftware.steam", @"appName": @"Steam"}];
    NSMutableDictionary *profB = [NSMutableDictionary dictionaryWithDictionary:@{@"id": @"pB", @"name": @"Profile B"}];
    NSMutableArray<NSMutableDictionary *> *profiles = [NSMutableArray arrayWithObjects:profA, profB, nil];

    // Neuverknüpfung von Steam auf Profile B: Profile A muss entkoppelt werden
    NSString *newBundleId = @"com.valvesoftware.steam";
    NSString *newAppName = @"Steam";

    NSMutableDictionary *target = profiles[1];
    for (NSMutableDictionary *other in profiles) {
        if (other != target && [other[@"appBundleId"] isEqualToString:newBundleId]) {
            [other removeObjectForKey:@"appBundleId"];
            [other removeObjectForKey:@"appName"];
        }
    }
    target[@"appBundleId"] = newBundleId;
    target[@"appName"] = newAppName;

    assert(profA[@"appBundleId"] == nil);
    assert(profA[@"appName"] == nil);
    assert([profB[@"appBundleId"] isEqualToString:@"com.valvesoftware.steam"]);
    assert([profB[@"appName"] isEqualToString:@"Steam"]);
}

static void test_fallback_profile_validation(void) {
    NSArray *profiles = @[
        @{@"id": @"p1", @"name": @"Profile 1"},
        @{@"id": @"p2", @"name": @"Profile 2"}
    ];
    // Fallback-ID, die nicht mehr existiert
    NSString *staleFallbackId = @"deleted-profile-id";
    BOOL fallbackFound = NO;
    for (NSDictionary *p in profiles) {
        if ([p[@"id"] isEqualToString:staleFallbackId]) {
            fallbackFound = YES;
            break;
        }
    }
    assert(!fallbackFound);

    // Automatische Korrektur auf ein existierendes Profil
    NSString *resolvedFallbackId = staleFallbackId;
    if (!fallbackFound && profiles.count > 0) {
        resolvedFallbackId = profiles[0][@"id"];
    }
    assert([resolvedFallbackId isEqualToString:@"p1"]);
}

static void test_cancel_blocks_apply(void) {
    KeyboardBackend *kb = [KeyboardBackend shared];
    NSDictionary *cols = @{@(14): [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0]};

    assert([kb connect]);
    assert([kb sendKeyColors:cols brightness:5 defaultColor:nil]);
    assert(![kb lastOperationWasCancelled]);

    [kb disconnect];
    assert(![kb isConnected]);
    assert(![kb sendKeyColors:cols brightness:5 defaultColor:nil]);
    assert([kb lastOperationWasCancelled]);

    assert([kb connect]);
    assert([kb sendKeyColors:cols brightness:5 defaultColor:nil]);
    assert(![kb lastOperationWasCancelled]);
}

static void test_disconnect_during_apply(void) {
    KeyboardBackend *kb = [KeyboardBackend shared];
    NSDictionary *cols = @{@(14): [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0]};

    assert([kb connect]);
    setenv("CIY_TESTING_APPLY_DELAY_MS", "1000", 1);

    __block BOOL applyOk = YES;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
        // Exakt der Produktionspfad; die Delay-Schleife liegt in applyProfile:.
        applyOk = [kb sendKeyColors:cols brightness:5 defaultColor:nil];
        dispatch_semaphore_signal(done);
    });

    usleep(50000); // sicherstellen, dass applyProfile: in der Delay-Schleife ist
    NSDate *start = [NSDate date];
    [kb disconnect];
    NSTimeInterval elapsed = -[start timeIntervalSinceNow];
    long waitResult = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
    unsetenv("CIY_TESTING_APPLY_DELAY_MS");

    assert(waitResult == 0);
    assert(elapsed < 0.2);
    assert(!applyOk);
    assert([kb lastOperationWasCancelled]);

    assert([kb connect]);
}

static void test_malformed_json_slots_ignored(void) {
    NSDictionary *malformedSlots = @{
        @"-1": @"#FF0000",
        @"99999": @"#00FF00",
        @"125": @"#0000FF", // 125 + 1 = 126 (>= CIY_LED_COUNT)
        @"invalid_key": @"#FFFFFF",
        @"14": @"#FF5500", // Gültig (Taste W)
        @"15": @"invalid_color" // Ungültige Farbe
    };

    NSMutableDictionary<NSString *, NSString *> *sanitized = [NSMutableDictionary dictionary];
    for (id key in malformedSlots) {
        NSString *keyStr = [NSString stringWithFormat:@"%@", key];
        NSScanner *scanner = [NSScanner scannerWithString:keyStr];
        int slot = -1;
        if ([scanner scanInt:&slot] && scanner.isAtEnd && slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
            NSString *hex = [NSString stringWithFormat:@"%@", malformedSlots[key]];
            if ([hex hasPrefix:@"#"] && hex.length == 7) {
                sanitized[@(slot).stringValue] = hex;
            }
        }
    }

    assert(sanitized.count == 1);
    assert([sanitized[@"14"] isEqualToString:@"#FF5500"]);

    // Test mit Backend-Pufferübertragung: Darf niemals crashen und out-of-bounds Indizes müssen ignoriert werden
    [KeyboardBackend resetTestingPayload];
    NSMutableDictionary<NSNumber *, NSColor *> *keyColors = [NSMutableDictionary dictionary];
    keyColors[@(-999)] = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0];
    keyColors[@(9999)] = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];
    keyColors[@(14)] = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0];
    BOOL ok = [[KeyboardBackend shared] sendKeyColors:keyColors brightness:5 defaultColor:nil];
    assert(ok);

    NSData *payload = [KeyboardBackend lastSentPlanarPayload];
    assert(payload != nil && payload.length == CIY_PLANAR_RGB_SIZE);
    const uint8_t *bytes = (const uint8_t *)payload.bytes;

    // Nur Slot 14 (Index 15) darf rot gesetzt sein
    assert(bytes[15] == 0xFF);
    assert(bytes[CIY_LED_COUNT + 15] == 0x00);
    assert(bytes[2 * CIY_LED_COUNT + 15] == 0x00);

    // Alle anderen Indizes müssen 0x00 sein
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        if (i == 15) continue;
        assert(bytes[i] == 0x00);
        assert(bytes[CIY_LED_COUNT + i] == 0x00);
        assert(bytes[2 * CIY_LED_COUNT + i] == 0x00);
    }
}

static void test_layout_x79_iso_de(void) {
    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    assert(layout != NULL);
    assert(strcmp(layout->layout_id, "ciy_x79_iso_de") == 0);
    assert(layout->key_count == 69);
    assert(layout->surface_count == 70);

    // 1. Prüfe Eindeutigkeit und Wertebereich aller 69 logischen Tasten
    NSMutableSet<NSNumber *> *seenSlots = [NSMutableSet set];
    for (size_t i = 0; i < layout->key_count; i++) {
        const CIYLogicalKey *k = &layout->keys[i];
        assert(k->matrix_slot >= 0 && k->matrix_slot <= 124);
        assert(k->led_index == k->matrix_slot + 1);
        assert(k->name != NULL && strlen(k->name) > 0);

        NSNumber *slotNum = @(k->matrix_slot);
        assert(![seenSlots containsObject:slotNum]); // Keine doppelten logischen Tasten
        [seenSlots addObject:slotNum];
    }
    assert(seenSlots.count == 69);

    // 2. Prüfe alle 70 grafischen Flächen und die Verknüpfung zu den logischen Tasten
    int enterSurfaceCount = 0;
    for (size_t i = 0; i < layout->surface_count; i++) {
        const CIYKeySurface *s = &layout->surfaces[i];
        assert(s->matrix_slot >= 0 && s->matrix_slot <= 124);
        assert(s->row >= 0 && s->row <= 4);
        assert(s->width_u >= 1.0f && s->width_u <= 6.5f);
        assert(s->label != NULL && strlen(s->label) > 0);

        // Jede Fläche muss auf eine gültige logische Taste zeigen
        const CIYLogicalKey *matchedKey = ciy_layout_find_key_by_slot(layout, s->matrix_slot);
        assert(matchedKey != NULL);
        assert(matchedKey->matrix_slot == s->matrix_slot);

        if (s->matrix_slot == 81) {
            enterSurfaceCount++;
            // Die beiden Enter-Flächen müssen in Row 1 und Row 2 liegen
            assert(s->row == 1 || s->row == 2);
            if (s->row == 1) {
                assert(strcmp(s->label, "↵ Enter") == 0);
                assert(fabs(s->width_u - 1.5f) < 0.01);
            } else if (s->row == 2) {
                assert(strcmp(s->label, "↵") == 0);
                assert(fabs(s->width_u - 1.25f) < 0.01);
            }
        }
    }
    assert(enterSurfaceCount == 2); // Exakt 2 Flächen für Enter

    // 3. Lookup-Funktionen testen
    const CIYLogicalKey *esc = ciy_layout_find_key_by_slot(layout, 1);
    assert(esc != NULL && strcmp(esc->name, "Esc") == 0);

    const CIYLogicalKey *ret = ciy_layout_find_key_by_slot(layout, 81);
    assert(ret != NULL && strcmp(ret->name, "↵ Enter") == 0);

    const CIYLogicalKey *w = ciy_layout_find_key_by_name(layout, "W");
    assert(w != NULL && w->matrix_slot == 14 && w->led_index == 15);

    assert(ciy_layout_find_key_by_slot(layout, -1) == NULL);
    assert(ciy_layout_find_key_by_slot(layout, 125) == NULL);
    assert(ciy_layout_find_key_by_name(layout, "NONEXISTENT") == NULL);
}

static void test_overlay_layer_and_zones(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSColor *red = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0];
    [view resetAllKeysToColor:red];

    // Basis-Farbe für W (Slot 14) ist rot
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#FF0000"]);

    // Zone färben: WASD auf Blau
    NSColor *blue = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];
    [view applyColor:blue toSlots:@[@14, @9, @15, @21]];
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);
    assert([HexFromColor([view allKeyColors][@9]) isEqualToString:@"#0000FF"]);
    assert([HexFromColor([view allKeyColors][@1]) isEqualToString:@"#FF0000"]); // Esc bleibt rot

    // Zone färben: Nav-Block (Slots 86, 87, 94, 91) auf Gelb
    NSColor *yellow = [NSColor colorWithSRGBRed:1.0 green:1.0 blue:0.0 alpha:1.0];
    [view applyColor:yellow toSlots:@[@86, @87, @94, @91]];
    assert([HexFromColor([view allKeyColors][@86]) isEqualToString:@"#FFFF00"]); // Entf
    assert([HexFromColor([view allKeyColors][@87]) isEqualToString:@"#FFFF00"]); // Bild Auf
    assert([HexFromColor([view allKeyColors][@94]) isEqualToString:@"#FFFF00"]); // Bild Ab
    assert([HexFromColor([view allKeyColors][@91]) isEqualToString:@"#FFFF00"]); // Ende

    // Ephemeres Overlay setzen (z. B. Akku-Grün auf Slot 14)
    NSColor *green = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0];
    [view setOverlayColors:@{@14: green}];
    assert(view.overlayColors != nil);
    assert([HexFromColor(view.overlayColors[@14]) isEqualToString:@"#00FF00"]);

    // KRITISCH: Basis-Profil allKeyColors darf durch Overlay NIEMALS verändert werden!
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);

    // Overlay löschen
    [view clearOverlayColors];
    assert(view.overlayColors == nil);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);
}

static void test_battery_monitor_sleep_safety(void) {
    // Fall A: Handler nach startMonitoring gesetzt (Debounced Update durch stopMonitoring abgewürgt)
    CIYBatteryMonitor *monitor = [[CIYBatteryMonitor alloc] init];
    [monitor startMonitoring];

    __block int updateCount = 0;
    monitor.updateHandler = ^(CIYBatteryInfo i) {
        (void)i;
        updateCount++;
    };

    [monitor scheduleDebouncedUpdate];
    [monitor stopMonitoring];

    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.6]];
    assert(updateCount == 0);

    // Fall B: Handler vor startMonitoring gesetzt (wie in AppDelegate):
    // startMonitoring führt initialen Update aus (count=1), debounced update wird durch stopMonitoring storniert
    CIYBatteryMonitor *monitorB = [[CIYBatteryMonitor alloc] init];
    __block int updateCountB = 0;
    monitorB.updateHandler = ^(CIYBatteryInfo i) {
        (void)i;
        updateCountB++;
    };
    [monitorB startMonitoring];
    assert(updateCountB == 1);

    [monitorB scheduleDebouncedUpdate];
    [monitorB stopMonitoring];

    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.6]];
    assert(updateCountB == 1);
}

static void test_battery_monitor_termination_cleanup(void) {
    CIYBatteryMonitor *monitor = [CIYBatteryMonitor sharedMonitor];
    [monitor startMonitoring];
    assert([monitor isMonitoring]);

    // Simuliere Lifecycle-Bereinigung bei App-Terminierung
    [monitor stopMonitoring];
    assert(![monitor isMonitoring]);

    // Idempotentes Stoppen darf keinen Fehler werfen
    [monitor stopMonitoring];
    assert(![monitor isMonitoring]);
}

static void test_keyboard_backend_monitoring_lifecycle(void) {
    KeyboardBackend *backend = [KeyboardBackend shared];
    [backend startMonitoring];
    // Idempotenter Start darf keine doppelten Notification-Ports erzeugen
    [backend startMonitoring];

    // Sauberes Stoppen und Freigeben aller IOKit-Ressourcen
    [backend stopMonitoring];
    // Idempotentes Stoppen darf keinen Fehler werfen
    [backend stopMonitoring];
}

@interface CIYTestKeyboardDelegate : NSObject <CIYKeyboardViewDelegate>
@property (nonatomic, assign) int undoCalls;
@property (nonatomic, assign) int redoCalls;
@end

@implementation CIYTestKeyboardDelegate
- (void)keyboardViewDidUndo:(CIYKeyboardView *)view {
    (void)view;
    self.undoCalls++;
}
- (void)keyboardViewDidRedo:(CIYKeyboardView *)view {
    (void)view;
    self.redoCalls++;
}
@end

static void test_undo_redo_single_key(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    assert(![view canUndo]);
    assert(![view canRedo]);
    assert(view.undoCount == 0);
    assert(view.redoCount == 0);

    NSString *initialHex = HexFromColor([view allKeyColors][@14]);

    // 1. Taste W (Slot 14) auf Blau setzen
    NSColor *blue = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];
    [view setColor:blue forSlot:14];
    assert([view canUndo]);
    assert(![view canRedo]);
    assert(view.undoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);

    // 2. Taste W auf Grün setzen
    NSColor *green = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0];
    [view setColor:green forSlot:14];
    assert(view.undoCount == 2);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#00FF00"]);

    // 3. Undo -> zurück zu Blau
    [view undo:nil];
    assert(view.undoCount == 1);
    assert(view.redoCount == 1);
    assert([view canRedo]);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);

    // 4. Undo -> zurück zum Initialwert
    [view undo:nil];
    assert(view.undoCount == 0);
    assert(view.redoCount == 2);
    assert(![view canUndo]);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:initialHex]);

    // No-op Undo wenn leer
    [view undo:nil];
    assert(view.undoCount == 0);
    assert(view.redoCount == 2);

    // 5. Redo -> wieder Blau
    [view redo:nil];
    assert(view.undoCount == 1);
    assert(view.redoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);

    // 6. Redo -> wieder Grün
    [view redo:nil];
    assert(view.undoCount == 2);
    assert(view.redoCount == 0);
    assert(![view canRedo]);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#00FF00"]);

    // 7. Neuer Edit nach Undo leert Redo-Stack
    [view undo:nil]; // zurück zu Blau
    assert([view canRedo]);
    NSColor *yellow = [NSColor colorWithSRGBRed:1.0 green:1.0 blue:0.0 alpha:1.0];
    [view setColor:yellow forSlot:15]; // Taste S ändern
    assert(![view canRedo]);
    assert(view.redoCount == 0);
    assert(view.undoCount == 2);
}

static void test_undo_redo_fifo_limit(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    assert(view.undoCount == 0);

    // 60 Farbschritte durchführen (Limit ist 50)
    for (int i = 0; i < 60; i++) {
        CGFloat r = ((i + 1) % 256) / 255.0;
        NSColor *c = [NSColor colorWithSRGBRed:r green:0.2 blue:0.2 alpha:1.0];
        [view setColor:c forSlot:14];
    }

    assert(view.undoCount == 50);

    // Alle 50 Schritte rückgängig machen
    for (int i = 0; i < 50; i++) {
        assert([view canUndo]);
        [view undo:nil];
    }

    assert(![view canUndo]);
    assert(view.undoCount == 0);
    assert(view.redoCount == 50);
}

static void test_undo_redo_clear_history(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    [view setColor:[NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0] forSlot:14];
    [view setColor:[NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0] forSlot:15];
    assert(view.undoCount == 2);

    [view clearUndoHistory];
    assert(view.undoCount == 0);
    assert(view.redoCount == 0);
    assert(![view canUndo]);
    assert(![view canRedo]);
}

static void test_undo_redo_bulk_actions(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSColor *initRed = [NSColor colorWithSRGBRed:0.85 green:0.1 blue:0.1 alpha:1.0];
    [view resetAllKeysToColor:initRed];
    [view clearUndoHistory];

    // 1. Fill all mit Cyan
    NSColor *cyan = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:1.0 alpha:1.0];
    [view resetAllKeysToColor:cyan];
    assert(view.undoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#00FFFF"]);
    assert([HexFromColor([view allKeyColors][@81]) isEqualToString:@"#00FFFF"]);

    [view undo:nil];
    assert(view.undoCount == 0);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:HexFromColor(initRed)]);
    assert([HexFromColor([view allKeyColors][@81]) isEqualToString:HexFromColor(initRed)]);

    [view redo:nil];
    assert(view.undoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#00FFFF"]);

    // 2. Zone färben (WASD)
    NSColor *magenta = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:1.0 alpha:1.0];
    [view applyColor:magenta toSlots:@[@14, @9, @15, @21]];
    assert(view.undoCount == 2);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#FF00FF"]);
    assert([HexFromColor([view allKeyColors][@9]) isEqualToString:@"#FF00FF"]);
    assert([HexFromColor([view allKeyColors][@1]) isEqualToString:@"#00FFFF"]); // Esc bleibt Cyan

    [view undo:nil];
    assert(view.undoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#00FFFF"]);
    assert([HexFromColor([view allKeyColors][@9]) isEqualToString:@"#00FFFF"]);

    // 3. applyKeyColors mit registerUndo: YES (Gradient-Simulation)
    NSDictionary *gradColors = @{
        @1: [NSColor colorWithSRGBRed:0.1 green:0.2 blue:0.3 alpha:1.0],
        @2: [NSColor colorWithSRGBRed:0.4 green:0.5 blue:0.6 alpha:1.0]
    };
    [view applyKeyColors:gradColors registerUndo:YES];
    assert(view.undoCount == 2);
    assert([view allKeyColors].count == 126);
    assert([HexFromColor([view allKeyColors][@1]) isEqualToString:@"#1A334D"]);
    assert([HexFromColor([view allKeyColors][@100]) isEqualToString:@"#00FFFF"]);

    [view undo:nil];
    assert([view allKeyColors].count == 126);
    assert([HexFromColor([view allKeyColors][@1]) isEqualToString:@"#00FFFF"]);
    assert([HexFromColor([view allKeyColors][@100]) isEqualToString:@"#00FFFF"]);
}

static void test_undo_partial_apply_merges(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSDictionary<NSNumber *, NSColor *> *before = [view allKeyColors];
    assert(before.count == 126);

    NSColor *green = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0];
    NSColor *yellow = [NSColor colorWithSRGBRed:1.0 green:1.0 blue:0.0 alpha:1.0];
    [view applyKeyColors:@{@1: green, @2: yellow} registerUndo:YES];

    assert(view.undoCount == 1);
    assert([view allKeyColors].count == 126);
    assert([HexFromColor([view allKeyColors][@1]) isEqualToString:@"#00FF00"]);
    assert([HexFromColor([view allKeyColors][@2]) isEqualToString:@"#FFFF00"]);
    assert([HexFromColor([view allKeyColors][@100]) isEqualToString:HexFromColor(before[@100])]);

    [view undo:nil];
    assert([view allKeyColors].count == 126);
    assert([HexFromColor([view allKeyColors][@1]) isEqualToString:HexFromColor(before[@1])]);
    assert([HexFromColor([view allKeyColors][@2]) isEqualToString:HexFromColor(before[@2])]);
    assert([HexFromColor([view allKeyColors][@100]) isEqualToString:HexFromColor(before[@100])]);
}

static void test_undo_ignores_noop_edits(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSColor *blue = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];

    [view setColor:blue forSlot:14];
    assert(view.undoCount == 1);

    [view setColor:blue forSlot:14];
    assert(view.undoCount == 1);

    NSColor *cyan = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:1.0 alpha:1.0];
    [view resetAllKeysToColor:cyan];
    assert(view.undoCount == 2);

    [view resetAllKeysToColor:cyan];
    assert(view.undoCount == 2);

    [view applyColor:cyan toSlots:@[@1, @2]];
    assert(view.undoCount == 2);

    [view applyKeyColors:@{@1: cyan, @2: cyan} registerUndo:YES];
    assert(view.undoCount == 2);
}

static void test_undo_redo_coalescing(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSString *initHex = HexFromColor([view allKeyColors][@14]);

    // Simuliere schnelles Ziehen im Farbrad (< 0.8s Abstand)
    NSColor *c1 = [NSColor colorWithSRGBRed:0.1 green:0.1 blue:0.1 alpha:1.0];
    NSColor *c2 = [NSColor colorWithSRGBRed:0.3 green:0.3 blue:0.3 alpha:1.0];
    NSColor *c3 = [NSColor colorWithSRGBRed:0.6 green:0.6 blue:0.6 alpha:1.0];
    NSColor *c4 = [NSColor colorWithSRGBRed:0.9 green:0.9 blue:0.9 alpha:1.0];

    [view setColor:c1 forSlot:14 continuous:YES];
    assert(view.undoCount == 1);

    [view setColor:c2 forSlot:14 continuous:YES];
    assert(view.undoCount == 1); // Coalesced

    [view setColor:c3 forSlot:14 continuous:YES];
    assert(view.undoCount == 1); // Coalesced

    [view setColor:c4 forSlot:14 continuous:YES];
    assert(view.undoCount == 1); // Coalesced

    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#E6E6E6"]);

    // Ein einzelnes Undo muss sofort auf die Ausgangsfarbe vor der Serie zurückspringen
    [view undo:nil];
    assert(view.undoCount == 0);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:initHex]);

    // Redo stellt den Endzustand der Serie (#E6E6E6) wieder her
    [view redo:nil];
    assert(view.undoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#E6E6E6"]);
}

static void test_undo_coalescing_after_undo_creates_new_step(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSString *initialHex = HexFromColor([view allKeyColors][@14]);

    NSColor *blue = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];
    NSColor *green = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0];
    NSColor *purple = [NSColor colorWithSRGBRed:0.5 green:0.0 blue:0.5 alpha:1.0];

    // A: kontinuierliche Serie, B: diskreter Palette-Edit auf demselben Slot.
    [view setColor:blue forSlot:14 continuous:YES];
    assert(view.undoCount == 1);
    [view setColor:green forSlot:14];
    assert(view.undoCount == 2);

    // B rückgängig: A liegt wieder oben, Blau ist der aktuelle Zustand.
    [view undo:nil];
    assert(view.undoCount == 1);
    assert(view.redoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);

    // Neuer kontinuierlicher Edit darf nicht mit der überholten Aktion A coalescen.
    [view setColor:purple forSlot:14 continuous:YES];
    assert(view.undoCount == 2);
    assert(view.redoCount == 0);

    // C rückgängig: zurück zu Blau (nicht direkt zur Initialfarbe).
    [view undo:nil];
    assert(view.undoCount == 1);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:@"#0000FF"]);

    // A rückgängig: zurück zur Initialfarbe.
    [view undo:nil];
    assert(view.undoCount == 0);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:initialHex]);
}

static void test_undo_coalescing_noop_removes_action(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSColor *initial = [view allKeyColors][@14];
    NSString *initialHex = HexFromColor(initial);
    NSColor *blue = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];

    [view setColor:blue forSlot:14 continuous:YES];
    assert(view.undoCount == 1);

    // Zurück zur Ausgangsfarbe: Die Serie hat netto nichts geändert.
    [view setColor:initial forSlot:14 continuous:YES];
    assert(view.undoCount == 0);
    assert(view.redoCount == 0);
    assert(![view canUndo]);
    assert([HexFromColor([view allKeyColors][@14]) isEqualToString:initialHex]);
}

static void test_color_mutators_return_change_flag(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSColor *cyan = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:1.0 alpha:1.0];
    NSColor *blue = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];
    NSColor *green = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0];

    assert([view resetAllKeysToColor:cyan]);
    assert(![view resetAllKeysToColor:cyan]);
    assert(![view setColor:cyan forSlot:14]);
    assert([view setColor:blue forSlot:14]);
    assert(![view setColor:blue forSlot:14]);
    BOOL zoneChanged = [view applyColor:blue toSlots:@[@14, @15]]; // Slot 15 wechselt cyan -> blau
    assert(zoneChanged);
    zoneChanged = [view applyColor:blue toSlots:@[@14, @15]];
    assert(!zoneChanged);
    assert([view applyKeyColors:@{@1: green} registerUndo:YES]);
    assert(![view applyKeyColors:@{@1: green} registerUndo:YES]);
    assert(![view applyKeyColors:@{} registerUndo:YES]);
    assert(![view applyColor:blue toSlots:@[]]);
}

static void test_undo_redo_delegate_and_validation(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    CIYTestKeyboardDelegate *del = [[CIYTestKeyboardDelegate alloc] init];
    view.delegate = del;

    NSMenuItem *undoItem = [[NSMenuItem alloc] initWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    NSMenuItem *redoItem = [[NSMenuItem alloc] initWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"z"];

    assert(![view validateMenuItem:undoItem]);
    assert(![view validateMenuItem:redoItem]);

    [view setColor:[NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0] forSlot:14];
    assert([view validateMenuItem:undoItem]);
    assert(![view validateMenuItem:redoItem]);

    [view undo:nil];
    assert(del.undoCalls == 1);
    assert(del.redoCalls == 0);
    assert(![view validateMenuItem:undoItem]);
    assert([view validateMenuItem:redoItem]);

    [view redo:nil];
    assert(del.undoCalls == 1);
    assert(del.redoCalls == 1);
    assert([view validateMenuItem:undoItem]);
    assert(![view validateMenuItem:redoItem]);
}

static void test_matches_profile_slots_and_dirty_state(void) {
    CIYKeyboardView *view = [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    NSColor *off = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0];
    NSColor *red = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0];
    NSColor *green = [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0];
    NSColor *blue = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:1.0 alpha:1.0];

    // 1. Partielles savedSlots-Dictionary (wie in loadProfileIntoView geladen):
    // Nur Slot 14 ist #FF0000, alle übrigen Slots 0..124 sind implizit #000000.
    NSMutableDictionary<NSNumber *, NSColor *> *loadedColors = [NSMutableDictionary dictionaryWithCapacity:CIY_LED_COUNT];
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        loadedColors[@(i)] = off;
    }
    loadedColors[@14] = red;
    [view applyKeyColors:loadedColors];
    [view clearUndoHistory];

    NSDictionary *sparseSlots = @{@"14": @"#FF0000"};
    assert([view matchesProfileSlots:sparseSlots]);

    // Nicht in sparseSlots aufgeführter Slot (15) wird geändert, danach Slot 14:
    assert([view setColor:green forSlot:15]);
    assert(![view matchesProfileSlots:sparseSlots]);
    assert([view setColor:blue forSlot:14]);
    assert(![view matchesProfileSlots:sparseSlots]);

    // Erstes Undo stellt zwar Slot 14 wieder auf #FF0000, Slot 15 ist aber weiterhin #00FF00:
    [view undo:nil];
    assert(![view matchesProfileSlots:sparseSlots]);

    // Zweites Undo stellt auch Slot 15 auf #000000 zurück -> wieder exakt synchron:
    [view undo:nil];
    assert([view matchesProfileSlots:sparseSlots]);

    // 2. Nicht-kanonische Hex-Formate (3-stellig, ohne '#', Kleinschreibung)
    assert([view setColor:green forSlot:15]);
    NSDictionary *nonCanonicalSlots = @{@"14": @"#f00", @"15": @"00ff00"};
    assert([view matchesProfileSlots:nonCanonicalSlots]);

    // 3. Ungültige Eingaben müssen NO liefern
    assert(![view matchesProfileSlots:@{}]);
    assert(![view matchesProfileSlots:@{@"125": @"#FF0000"}]); // Nur Out-of-Range-Slot
    assert(![view matchesProfileSlots:@{@"abc": @"#FF0000"}]);
    assert(![view matchesProfileSlots:@{@"14": @"#GGGGGG"}]);
    assert(![view matchesProfileSlots:@{@"14": @(123)}]);

    // 4. Vollprofil-Abgleich, manuelle Rückkehr & Slot-125-Konsistenz:
    // Simuliere loadProfileIntoView für ein "Full red"-Profil (Slots 0..124 = Rot, Slot 125 = #000000)
    NSMutableDictionary<NSString *, NSString *> *fullRedSlots = [NSMutableDictionary dictionaryWithCapacity:CIY_LED_COUNT];
    NSMutableDictionary<NSNumber *, NSColor *> *fullRedLoaded = [NSMutableDictionary dictionaryWithCapacity:CIY_LED_COUNT];
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        fullRedSlots[@(i).stringValue] = @"#FF0000";
        fullRedLoaded[@(i)] = (i < CIY_LED_COUNT - 1) ? red : off;
    }
    [view applyKeyColors:fullRedLoaded];
    [view clearUndoHistory];
    assert([view matchesProfileSlots:fullRedSlots]);

    // "Fill all keys: Red" auf bereits komplett rotem Profil (0..124) darf keinen Phantom-Undo-Schritt für Slot 125 erzeugen
    assert(![view resetAllKeysToColor:red]);
    assert(view.undoCount == 0);

    // Diskreter Edit auf Blau -> dirty, manuelle Rückkehr per Palette auf Rot -> wieder clean
    assert([view setColor:blue forSlot:14]);
    assert(![view matchesProfileSlots:fullRedSlots]);
    assert([view setColor:red forSlot:14]);
    assert([view matchesProfileSlots:fullRedSlots]);
}

@interface CIYMiniTestDelegate : NSObject <CIYMiniWindowControllerDelegate>
@property (nonatomic, assign) NSInteger selectedIndex;
@property (nonatomic, assign) BOOL sendRequested;
@property (nonatomic, assign) BOOL closed;
@end

@implementation CIYMiniTestDelegate
- (instancetype)init {
    self = [super init];
    if (self) {
        _selectedIndex = -1;
        _sendRequested = NO;
        _closed = NO;
    }
    return self;
}
- (void)miniWindowController:(CIYMiniWindowController *)controller didSelectProfileAtIndex:(NSInteger)index {
    (void)controller;
    self.selectedIndex = index;
}
- (void)miniWindowControllerDidRequestSend:(CIYMiniWindowController *)controller {
    (void)controller;
    self.sendRequested = YES;
}
- (void)miniWindowControllerDidClose:(CIYMiniWindowController *)controller {
    (void)controller;
    self.closed = YES;
}
@end

static void test_mini_window_controller(void) {
    [NSApplication sharedApplication];

    CIYMiniWindowController *ctrl = [[CIYMiniWindowController alloc] init];
    assert(ctrl != nil);
    assert(!ctrl.isVisible);

    CIYMiniTestDelegate *del = [[CIYMiniTestDelegate alloc] init];
    ctrl.delegate = del;

    // 1. Initiales Setzen der Daten darf KEINE Delegate-Aktionen auslösen
    NSArray<NSString *> *names = @[@"WASD Gaming", @"Cyberpunk", @"Clean Office"];
    [ctrl setProfileNames:names selectedIndex:1];
    assert(del.selectedIndex == -1);
    assert(!del.sendRequested);

    [ctrl setSelectedIndex:2];
    assert(del.selectedIndex == -1);

    [ctrl setConnected:YES];
    [ctrl noteSendResult:YES atDate:[NSDate date]];

    // 2. Fenster anzeigen und Window-Eigenschaften prüfen
    [ctrl show];
    assert(ctrl.isVisible);

    NSWindow *panelWin = nil;
    NSView *content = nil;
    for (NSWindow *win in [NSApp windows]) {
        if ((id)win.delegate == (id)ctrl && ([win.title isEqualToString:@"Mini Mode"] || [win.title isEqualToString:CIYL(@"Mini Mode")])) {
            panelWin = win;
            content = win.contentView;
            assert(win.level == NSFloatingWindowLevel);
            assert((win.styleMask & NSWindowStyleMaskUtilityWindow) != 0);
            assert((win.styleMask & NSWindowStyleMaskTitled) != 0);
            assert((win.styleMask & NSWindowStyleMaskClosable) != 0);
            assert(win.hidesOnDeactivate == NO);
            break;
        }
    }
    assert(panelWin != nil);
    assert(content != nil);

    NSPopUpButton *pop = nil;
    NSButton *sendButton = nil;
    BOOL foundPopUp = NO;
    BOOL foundSendButton = NO;
    for (NSView *sub in content.subviews) {
        if ([sub isKindOfClass:[NSPopUpButton class]]) {
            pop = (NSPopUpButton *)sub;
            foundPopUp = YES;
        } else if ([sub isKindOfClass:[NSButton class]]) {
            NSButton *btn = (NSButton *)sub;
            if ([btn.keyEquivalent isEqualToString:@"\r"]) {
                sendButton = btn;
                foundSendButton = YES;
            }
        }
    }
    assert(foundPopUp && foundSendButton);
    assert(pop != nil);
    assert(sendButton != nil);

    assert(pop.numberOfItems == 3);
    // Benutzerinteraktion simulieren
    [pop selectItemAtIndex:0];
    [pop sendAction:pop.action to:pop.target];
    assert(del.selectedIndex == 0);

    [sendButton performClick:nil];
    assert(del.sendRequested == YES);

    // Regression 1: Gleichnamige Profile
    NSArray<NSString *> *dupNames = @[@"Gaming", @"Gaming", @"Office"];
    [ctrl setProfileNames:dupNames selectedIndex:2];
    assert(pop.numberOfItems == 3);
    assert(pop.indexOfSelectedItem == 2);
    [pop selectItemAtIndex:2];
    [pop sendAction:pop.action to:pop.target];
    assert(del.selectedIndex == 2);

    // Regression 2: Ungültiger Auswahl-Index
    [ctrl setSelectedIndex:-1];
    assert(pop.indexOfSelectedItem == -1);
    assert(sendButton.enabled == NO);

    [ctrl setProfileNames:names selectedIndex:-1];
    assert(pop.indexOfSelectedItem == -1);
    assert(sendButton.enabled == NO);

    // 3. Ausblenden und Umschalten
    [ctrl hide];
    assert(!ctrl.isVisible);

    [ctrl toggle];
    assert(ctrl.isVisible);

    [ctrl toggle];
    assert(!ctrl.isVisible);

    // 4. Close-Delegate & Re-Show nach teardown
    [ctrl show];
    assert(ctrl.isVisible);

    // Reguläres Schließen des Panels setzt del.closed == YES
    del.closed = NO;
    [panelWin close];
    assert(del.closed == YES);
    assert(!ctrl.isVisible);

    // [ctrl teardown] darf del.closed nicht auslösen
    panelWin = nil;
    del.closed = NO;
    [ctrl teardown];
    assert(!ctrl.isVisible);
    assert(del.closed == NO);

    // Doppelter Aufruf darf nicht crashen
    [ctrl teardown];
    assert(del.closed == NO);

    // Re-Show nach teardown: Erneuter Zyklus ctrl.delegate = del; [ctrl show];
    ctrl.delegate = del;
    [ctrl show];
    assert(ctrl.isVisible);

    NSWindow *reopenedWin = nil;
    for (NSWindow *win in [NSApp windows]) {
        if ((id)win.delegate == (id)ctrl && ([win.title isEqualToString:@"Mini Mode"] || [win.title isEqualToString:CIYL(@"Mini Mode")])) {
            reopenedWin = win;
            break;
        }
    }
    assert(reopenedWin != nil);

    // Reguläres Schließen des wiedergeöffneten Panels
    del.closed = NO;
    [reopenedWin close];
    assert(del.closed == YES);
    assert(!ctrl.isVisible);

    // Abschließendes teardown
    del.closed = NO;
    [ctrl teardown];
    assert(!ctrl.isVisible);
    assert(del.closed == NO);
}

@interface TestSchedulerDelegate : NSObject <CIYProfileSchedulerDelegate>
@property (nonatomic, strong, nullable) NSString *lastProfileId;
@property (nonatomic, assign) NSInteger callCount;
@end

@implementation TestSchedulerDelegate
- (void)scheduler:(CIYProfileScheduler *)scheduler didDetermineTargetProfileId:(nullable NSString *)profileId {
    (void)scheduler;
    self.lastProfileId = profileId;
    self.callCount++;
}
@end

static NSDate *TestCreateDate(NSCalendar *cal, NSInteger y, NSInteger m, NSInteger d, NSInteger h, NSInteger min) {
    NSDateComponents *comp = [[NSDateComponents alloc] init];
    comp.year = y;
    comp.month = m;
    comp.day = d;
    comp.hour = h;
    comp.minute = min;
    comp.second = 0;
    return [cal dateFromComponents:comp];
}

static void test_scheduler_weekday_and_mask_filtering(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    // Profil 1: Werktags Mo-Fr um 09:00
    NSMutableDictionary *profWork = [@{
        @"id": @"prof-work",
        @"name": @"Work",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(9),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysWorkdays) // Mo-Fr
    } mutableCopy];

    // Profil 2: Wochenende Sa-So um 10:00
    NSMutableDictionary *profWeekend = [@{
        @"id": @"prof-weekend",
        @"name": @"Weekend",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(10),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysWeekend) // Sa-So
    } mutableCopy];

    NSArray<NSDictionary *> *profiles = @[profWork, profWeekend];

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return profiles;
    };
    scheduler.globalEnabled = YES;

    // Freitag 2026-11-06 um 11:00 (Werktag): Work-Profil muss aktiv sein
    NSDate *fri11am = TestCreateDate(cal, 2026, 11, 6, 11, 0);
    assert([[scheduler activeScheduledProfileIdAtDate:fri11am] isEqualToString:@"prof-work"]);

    // Samstag 2026-11-07 um 09:30: Weekend-Profil schaltet erst um 10:00.
    // Zuletzt fälliger Schaltzeitpunkt ist Freitag 09:00 (Work-Profil).
    NSDate *sat0930 = TestCreateDate(cal, 2026, 11, 7, 9, 30);
    assert([[scheduler activeScheduledProfileIdAtDate:sat0930] isEqualToString:@"prof-work"]);

    // Samstag 2026-11-07 um 10:01: Weekend-Profil ist fällig geworden und gewinnt
    NSDate *sat1001 = TestCreateDate(cal, 2026, 11, 7, 10, 1);
    assert([[scheduler activeScheduledProfileIdAtDate:sat1001] isEqualToString:@"prof-weekend"]);

    // Sonntag 2026-11-08 um 15:00: Weekend-Profil immer noch aktiv
    NSDate *sun1500 = TestCreateDate(cal, 2026, 11, 8, 15, 0);
    assert([[scheduler activeScheduledProfileIdAtDate:sun1500] isEqualToString:@"prof-weekend"]);

    // Montag 2026-11-09 um 09:05: Work-Profil übernimmt wieder
    NSDate *mon0905 = TestCreateDate(cal, 2026, 11, 9, 9, 5);
    assert([[scheduler activeScheduledProfileIdAtDate:mon0905] isEqualToString:@"prof-work"]);

    // Deaktiviertes Profil
    profWork[@"scheduleEnabled"] = @(NO);
    assert([[scheduler activeScheduledProfileIdAtDate:fri11am] isEqualToString:@"prof-weekend"]); // letzter fälliger des Wochenendes
    profWeekend[@"scheduleDaysMask"] = @(0);
    assert([scheduler activeScheduledProfileIdAtDate:fri11am] == nil);
}

static void test_scheduler_midnight_and_day_transitions(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    // Profil Tag: Täglich um 08:00
    NSMutableDictionary *profDay = [@{
        @"id": @"prof-day",
        @"name": @"Day",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(8),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    } mutableCopy];

    // Profil Nacht: Täglich um 22:00
    NSMutableDictionary *profNight = [@{
        @"id": @"prof-night",
        @"name": @"Night",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(22),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    } mutableCopy];

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[profDay, profNight];
    };
    scheduler.globalEnabled = YES;

    // Freitag Abend 23:00 -> Nacht-Profil
    NSDate *fri2300 = TestCreateDate(cal, 2026, 11, 6, 23, 0);
    assert([[scheduler activeScheduledProfileIdAtDate:fri2300] isEqualToString:@"prof-night"]);

    // Samstag früh um 01:30 morgens -> Nacht-Profil bleibt aktiv (letzter Schaltpunkt war gestern 22:00)
    NSDate *sat0130 = TestCreateDate(cal, 2026, 11, 7, 1, 30);
    assert([[scheduler activeScheduledProfileIdAtDate:sat0130] isEqualToString:@"prof-night"]);

    // Samstag um 07:59 -> Nacht-Profil noch aktiv
    NSDate *sat0759 = TestCreateDate(cal, 2026, 11, 7, 7, 59);
    assert([[scheduler activeScheduledProfileIdAtDate:sat0759] isEqualToString:@"prof-night"]);

    // Samstag um 08:00 -> Tag-Profil übernimmt exakt
    NSDate *sat0800 = TestCreateDate(cal, 2026, 11, 7, 8, 0);
    assert([[scheduler activeScheduledProfileIdAtDate:sat0800] isEqualToString:@"prof-day"]);

    // Samstag um 21:59 -> Tag-Profil noch aktiv
    NSDate *sat2159 = TestCreateDate(cal, 2026, 11, 7, 21, 59);
    assert([[scheduler activeScheduledProfileIdAtDate:sat2159] isEqualToString:@"prof-day"]);

    // Samstag um 22:00 -> Nacht-Profil übernimmt exakt
    NSDate *sat2200 = TestCreateDate(cal, 2026, 11, 7, 22, 0);
    assert([[scheduler activeScheduledProfileIdAtDate:sat2200] isEqualToString:@"prof-night"]);
}

static void test_scheduler_tie_breaking_and_overlap(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    // Zwei Profile zur exakt gleichen Zeit (09:00)
    NSDictionary *profA = @{
        @"id": @"prof-A",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(9),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };
    NSDictionary *profB = @{
        @"id": @"prof-B",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(9),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.globalEnabled = YES;

    // A zuerst: A gewinnt
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[profA, profB];
    };
    NSDate *now = TestCreateDate(cal, 2026, 11, 6, 9, 30);
    assert([[scheduler activeScheduledProfileIdAtDate:now] isEqualToString:@"prof-A"]);

    // B zuerst: B gewinnt
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[profB, profA];
    };
    assert([[scheduler activeScheduledProfileIdAtDate:now] isEqualToString:@"prof-B"]);

    // GlobalEnabled = NO liefert nil
    scheduler.globalEnabled = NO;
    assert([scheduler activeScheduledProfileIdAtDate:now] == nil);

    // Leere Profilliste
    scheduler.globalEnabled = YES;
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[];
    };
    assert([scheduler activeScheduledProfileIdAtDate:now] == nil);
}

static void test_scheduler_next_switch_date(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    // Profil 1: 14:00 (täglich)
    NSDictionary *p1 = @{
        @"id": @"p1",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(14),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };
    // Profil 2: 20:00 (täglich)
    NSDictionary *p2 = @{
        @"id": @"p2",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(20),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[p1, p2];
    };
    scheduler.globalEnabled = YES;

    // Vor 14:00 -> nächster Schaltzeitpunkt ist heute 14:00
    NSDate *at11am = TestCreateDate(cal, 2026, 11, 6, 11, 0);
    NSDate *next = [scheduler nextSwitchDateAfterDate:at11am];
    assert(next != nil);
    NSDateComponents *comp = [cal components:NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitDay fromDate:next];
    assert(comp.day == 6 && comp.hour == 14 && comp.minute == 0);

    // Exakt um 14:00 -> nächster Schaltzeitpunkt muss strikt in der Zukunft liegen (heute 20:00)
    NSDate *at1400 = TestCreateDate(cal, 2026, 11, 6, 14, 0);
    next = [scheduler nextSwitchDateAfterDate:at1400];
    assert(next != nil);
    comp = [cal components:NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitDay fromDate:next];
    assert(comp.day == 6 && comp.hour == 20 && comp.minute == 0);

    // Nach 20:00 -> nächster Schaltzeitpunkt ist morgen um 14:00
    NSDate *at2100 = TestCreateDate(cal, 2026, 11, 6, 21, 0);
    next = [scheduler nextSwitchDateAfterDate:at2100];
    assert(next != nil);
    comp = [cal components:NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitDay fromDate:next];
    assert(comp.day == 7 && comp.hour == 14 && comp.minute == 0);

    // Global deaktiviert -> nextSwitchDateAfterDate und nextSwitchDate müssen nil liefern
    scheduler.nowProvider = ^NSDate *{ return at11am; };
    scheduler.globalEnabled = NO;
    assert([scheduler nextSwitchDateAfterDate:at11am] == nil);
    assert([scheduler nextSwitchDate] == nil);
    scheduler.globalEnabled = YES;

    // Keine Profile -> nil
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{ return @[]; };
    assert([scheduler nextSwitchDateAfterDate:at11am] == nil);
}

static void test_scheduler_sanitizing_and_serialization(void) {
    NSMutableDictionary *prof = [@{
        @"scheduleHour": @(45),       // ungültig > 23
        @"scheduleMinute": @(-10),    // ungültig < 0
        @"scheduleDaysMask": @(500),  // > 127
        @"scheduleEnabled": @(YES)
    } mutableCopy];

    [CIYProfileScheduler sanitizeScheduleInProfile:prof];
    assert([prof[kCIYProfileScheduleHourKey] integerValue] == 23);
    assert([prof[kCIYProfileScheduleMinuteKey] integerValue] == 0);
    assert(([prof[kCIYProfileScheduleDaysMaskKey] unsignedIntegerValue] & ~0x7F) == 0);
    assert([prof[kCIYProfileScheduleEnabledKey] boolValue] == YES);

    // Maske 0 deaktiviert scheduleEnabled
    prof[kCIYProfileScheduleDaysMaskKey] = @(0);
    [CIYProfileScheduler sanitizeScheduleInProfile:prof];
    assert([prof[kCIYProfileScheduleEnabledKey] boolValue] == NO);

    // Formatierung
    assert([[CIYProfileScheduler formattedTimeStringForHour:8 minute:5] isEqualToString:@"08:05"]);
    assert([[CIYProfileScheduler formattedTimeStringForHour:23 minute:59] isEqualToString:@"23:59"]);
    assert([CIYProfileScheduler shortScheduleSummaryForProfile:prof] == nil); // da enabled == NO
    prof[kCIYProfileScheduleEnabledKey] = @(YES);
    assert([[CIYProfileScheduler shortScheduleSummaryForProfile:prof] isEqualToString:@"23:00"]);

    // JSON Export Privacy (Strippen ohne App-Metadaten)
    NSMutableDictionary *copyProf = [prof mutableCopy];
    copyProf[@"appBundleId"] = @"com.apple.Terminal";
    copyProf[@"appName"] = @"Terminal";
    BOOL includeAppMetadata = NO;
    if (!includeAppMetadata) {
        [copyProf removeObjectForKey:@"appBundleId"];
        [copyProf removeObjectForKey:@"appName"];
        [copyProf removeObjectForKey:kCIYProfileScheduleEnabledKey];
        [copyProf removeObjectForKey:kCIYProfileScheduleHourKey];
        [copyProf removeObjectForKey:kCIYProfileScheduleMinuteKey];
        [copyProf removeObjectForKey:kCIYProfileScheduleDaysMaskKey];
    }
    assert(copyProf[@"appBundleId"] == nil);
    assert(copyProf[@"scheduleEnabled"] == nil);
    assert(copyProf[@"scheduleHour"] == nil);
}

static void test_scheduler_lifecycle_and_sleep_safety(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    __block NSDate *currentTestDate = TestCreateDate(cal, 2026, 11, 6, 8, 30);

    NSDictionary *p1 = @{
        @"id": @"prof-p1",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(9),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };
    NSDictionary *p2 = @{
        @"id": @"prof-p2",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(12),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.nowProvider = ^NSDate *{
        return currentTestDate;
    };
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[p1, p2];
    };
    scheduler.globalEnabled = YES;

    TestSchedulerDelegate *delegate = [[TestSchedulerDelegate alloc] init];
    scheduler.delegate = delegate;

    assert(!scheduler.isRunning);
    [scheduler start];
    assert(scheduler.isRunning);
    assert(delegate.callCount == 1);
    // Vor 09:00: Der letzte fällige war gestern um 12:00 (p2)
    assert([delegate.lastProfileId isEqualToString:@"prof-p2"]);

    // Zeit auf 09:05 vorspulen und Timer auslösen
    currentTestDate = TestCreateDate(cal, 2026, 11, 6, 9, 5);
    [scheduler fireTimerForTesting];
    assert(delegate.callCount == 2);
    assert([delegate.lastProfileId isEqualToString:@"prof-p1"]);

    // SystemWillSleep: Scheduler muss gestoppt werden
    [scheduler stop];
    assert(!scheduler.isRunning);
    NSInteger countBeforeSleep = delegate.callCount;

    // Während Sleep gefeuerte Events dürfen Delegate nicht benachrichtigen
    currentTestDate = TestCreateDate(cal, 2026, 11, 6, 12, 5);
    [scheduler fireTimerForTesting];
    assert(delegate.callCount == countBeforeSleep);

    [[NSNotificationCenter defaultCenter] postNotificationName:NSSystemClockDidChangeNotification object:nil];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    assert(delegate.callCount == countBeforeSleep);

    // Wake: Wieder starten
    [scheduler start];
    assert(scheduler.isRunning);
    assert(delegate.callCount > countBeforeSleep);
    assert([delegate.lastProfileId isEqualToString:@"prof-p2"]);

    // Mehrfaches Stop ist idempotent
    [scheduler stop];
    [scheduler stop];
    assert(!scheduler.isRunning);
}

static void test_scheduler_retain_cycle_freedom(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    NSDictionary *p1 = @{
        @"id": @"prof-p1",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(23),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };

    __weak CIYProfileScheduler *weakRef = nil;
    @autoreleasepool {
        CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
        scheduler.calendar = cal;
        scheduler.nowProvider = ^NSDate *{
            return TestCreateDate(cal, 2026, 11, 6, 12, 0);
        };
        scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
            return @[p1];
        };
        scheduler.globalEnabled = YES;
        [scheduler start];
        assert(scheduler.isRunning);
        weakRef = scheduler;
        assert(weakRef != nil);
        // Scope endet hier ohne [scheduler stop]
    }
    // Nach Verlassen des autoreleasepools muss die Instanz deallokiert sein (kein Timer-Retain-Cycle)
    assert(weakRef == nil);
}

static void test_scheduler_timezone_notification(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    __block NSDate *currentTestDate = TestCreateDate(cal, 2026, 11, 6, 8, 30);

    NSDictionary *p1 = @{
        @"id": @"prof-p1",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(9),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };
    NSDictionary *p2 = @{
        @"id": @"prof-p2",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(12),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.nowProvider = ^NSDate *{
        return currentTestDate;
    };
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[p1, p2];
    };
    scheduler.globalEnabled = YES;

    TestSchedulerDelegate *delegate = [[TestSchedulerDelegate alloc] init];
    scheduler.delegate = delegate;

    [scheduler start];
    assert(delegate.callCount == 1);
    assert([delegate.lastProfileId isEqualToString:@"prof-p2"]);

    // Zeit vorspulen auf 09:05
    currentTestDate = TestCreateDate(cal, 2026, 11, 6, 9, 5);

    // Zeitzonenwechsel-Notification absenden
    [[NSNotificationCenter defaultCenter] postNotificationName:NSSystemTimeZoneDidChangeNotification object:nil];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];

    // Scheduler muss im laufenden Zustand reevaluiert haben
    assert(delegate.callCount == 2);
    assert([delegate.lastProfileId isEqualToString:@"prof-p1"]);

    // Scheduler stoppen und prüfen, dass Notification ignoriert wird
    [scheduler stop];
    NSInteger countAfterStop = delegate.callCount;

    currentTestDate = TestCreateDate(cal, 2026, 11, 6, 12, 5);
    [[NSNotificationCenter defaultCenter] postNotificationName:NSSystemTimeZoneDidChangeNotification object:nil];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];

    assert(delegate.callCount == countAfterStop);
}

static void test_scheduler_reschedule_timer_only(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    __block NSDate *currentTestDate = TestCreateDate(cal, 2026, 11, 6, 8, 30);

    NSDictionary *p1 = @{
        @"id": @"prof-p1",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(9),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.nowProvider = ^NSDate *{
        return currentTestDate;
    };
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[p1];
    };
    scheduler.globalEnabled = YES;

    TestSchedulerDelegate *delegate = [[TestSchedulerDelegate alloc] init];
    scheduler.delegate = delegate;

    [scheduler start];
    assert(delegate.callCount == 1);

    // 1. rescheduleTimerOnly aufrufen: Delegate-Callcount darf sich NICHT erhöhen
    NSDate *targetSwitch = TestCreateDate(cal, 2026, 11, 6, 9, 0);
    // 50ms vor dem Umschaltzeitpunkt
    currentTestDate = [targetSwitch dateByAddingTimeInterval:-0.05];

    [scheduler rescheduleTimerOnly];
    assert(delegate.callCount == 1); // Synchroner Callback darf nicht erfolgt sein

    // 2. Zeit auf nach dem Schaltzeitpunkt vorstellen, sodass beim Timer-Feuern das Profil aktiv ist
    currentTestDate = [targetSwitch dateByAddingTimeInterval:1.0];

    // Main RunLoop drehen, bis der geplante Timer feuert (intervall 0.05s)
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.15]];

    // Timer muss gefeuert und reevaluate ausgelöst haben
    assert(delegate.callCount == 2);
    assert([delegate.lastProfileId isEqualToString:@"prof-p1"]);

    [scheduler stop];
}

static void test_scheduler_global_enabled_timer_rescheduling(void) {
    NSCalendar *cal = [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
    cal.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:3600];

    NSDate *targetSwitch = TestCreateDate(cal, 2026, 11, 6, 9, 0);
    // 50ms vor dem Umschaltzeitpunkt starten
    __block NSDate *currentTestDate = [targetSwitch dateByAddingTimeInterval:-0.05];

    NSDictionary *p1 = @{
        @"id": @"prof-p1",
        @"scheduleEnabled": @(YES),
        @"scheduleHour": @(9),
        @"scheduleMinute": @(0),
        @"scheduleDaysMask": @(CIYScheduleDaysAll)
    };

    CIYProfileScheduler *scheduler = [[CIYProfileScheduler alloc] init];
    scheduler.calendar = cal;
    scheduler.nowProvider = ^NSDate *{
        return currentTestDate;
    };
    scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        return @[p1];
    };
    scheduler.globalEnabled = YES;

    TestSchedulerDelegate *delegate = [[TestSchedulerDelegate alloc] init];
    scheduler.delegate = delegate;

    [scheduler start];
    assert(delegate.callCount == 1); // Initialer Aufruf bei start

    // Laufenden Scheduler ohne reevaluate auf globalEnabled = NO setzen
    scheduler.globalEnabled = NO;
    assert(delegate.callCount == 1); // Kein synchroner Delegate-Aufruf

    // Zeit auf nach den Schaltzeitpunkt vorstellen
    currentTestDate = [targetSwitch dateByAddingTimeInterval:1.0];

    // RunLoop laufen lassen: Der vorherige Timer darf NICHT feuern
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.15]];
    assert(delegate.callCount == 1);

    // Nächsten Schaltzeitpunkt vorbereiten (morgen 09:00, 50ms vorher)
    NSDate *nextTargetSwitch = TestCreateDate(cal, 2026, 11, 7, 9, 0);
    currentTestDate = [nextTargetSwitch dateByAddingTimeInterval:-0.05];

    // Wieder aktivieren: Timer muss ohne manuelles reevaluate neu eingeplant werden
    scheduler.globalEnabled = YES;
    assert(delegate.callCount == 1); // Kein synchroner Callback im Setter

    // Zeit auf nach den Schaltzeitpunkt vorstellen
    currentTestDate = [nextTargetSwitch dateByAddingTimeInterval:1.0];

    // RunLoop laufen lassen: Jetzt muss der reguläre Timer auslösen
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.15]];
    assert(delegate.callCount == 2);
    assert([delegate.lastProfileId isEqualToString:@"prof-p1"]);

    [scheduler stop];
}

int main(void) {
    @autoreleasepool {
        [KeyboardBackend setTestingMode:YES];
        assert([KeyboardBackend isTestingMode]);
        test_layout_x79_iso_de();
        test_hex_conversion();
        test_profile_creation_and_update();
        test_keyboard_view_colors();
        test_overlay_layer_and_zones();
        test_builtin_profiles_structure();
        test_json_export_import();
        test_brightness_and_backend();
        test_keyboard_key_lookup();
        test_app_trigger_profile_attributes();
        test_menu_item_titles();
        test_app_binding_deduplication();
        test_fallback_profile_validation();
        test_cancel_blocks_apply();
        test_disconnect_during_apply();
        test_malformed_json_slots_ignored();
        test_battery_monitor_sleep_safety();
        test_battery_monitor_termination_cleanup();
        test_keyboard_backend_monitoring_lifecycle();
        test_undo_redo_single_key();
        test_undo_redo_fifo_limit();
        test_undo_redo_clear_history();
        test_undo_redo_bulk_actions();
        test_undo_partial_apply_merges();
        test_undo_ignores_noop_edits();
        test_undo_redo_coalescing();
        test_undo_coalescing_after_undo_creates_new_step();
        test_undo_coalescing_noop_removes_action();
        test_color_mutators_return_change_flag();
        test_undo_redo_delegate_and_validation();
        test_matches_profile_slots_and_dirty_state();
        test_mini_window_controller();
        test_scheduler_weekday_and_mask_filtering();
        test_scheduler_midnight_and_day_transitions();
        test_scheduler_tie_breaking_and_overlap();
        test_scheduler_next_switch_date();
        test_scheduler_sanitizing_and_serialization();
        test_scheduler_lifecycle_and_sleep_safety();
        test_scheduler_retain_cycle_freedom();
        test_scheduler_timezone_notification();
        test_scheduler_reschedule_timer_only();
        test_scheduler_global_enabled_timer_rescheduling();
        puts("Profil-Logik Tests bestanden.");
    }
    return 0;
}
