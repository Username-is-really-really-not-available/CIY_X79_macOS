#import <Cocoa/Cocoa.h>
#import <assert.h>
#import "../src/core/ciy_hid.h"
#import "../src/app/KeyboardBackend.h"
#import "../src/app/CIYKeyboardView.h"

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
    if (ig < 0) ig = 0; else if (ig > 255) ir = 255;
    if (ib < 0) ib = 0; else if (ib > 255) ib = 255;
    return [NSString stringWithFormat:@"#%02X%02X%02X", ir, ig, ib];
}

static NSColor *ColorFromHex(NSString *hex) {
    if (!hex || ![hex isKindOfClass:[NSString class]] || ![hex hasPrefix:@"#"] || hex.length != 7) {
        return [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0];
    }
    unsigned int rgbVal = 0;
    NSScanner *scanner = [NSScanner scannerWithString:[hex substringFromIndex:1]];
    if (![scanner scanHexInt:&rgbVal]) {
        return [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0];
    }
    CGFloat r = ((rgbVal >> 16) & 0xFF) / 255.0;
    CGFloat g = ((rgbVal >> 8) & 0xFF) / 255.0;
    CGFloat b = (rgbVal & 0xFF) / 255.0;
    return [NSColor colorWithSRGBRed:r green:g blue:b alpha:1.0];
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
        if ([scanner scanInt:&slot] && scanner.isAtEnd && slot >= 0 && (slot + 1) < CIY_LED_COUNT) {
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

int main(void) {
    @autoreleasepool {
        [KeyboardBackend setTestingMode:YES];
        test_hex_conversion();
        test_profile_creation_and_update();
        test_keyboard_view_colors();
        test_builtin_profiles_structure();
        test_json_export_import();
        test_brightness_and_backend();
        test_keyboard_key_lookup();
        test_app_trigger_profile_attributes();
        test_menu_item_titles();
        test_app_binding_deduplication();
        test_fallback_profile_validation();
        test_malformed_json_slots_ignored();
        puts("Profil-Logik Tests bestanden.");
    }
    return 0;
}
