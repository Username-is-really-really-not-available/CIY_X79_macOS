#import "AppDelegate.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface AppDelegate () {
    NSTextField *_statusLabel;
    NSTextField *_selectedKeyLabel;
    CIYKeyboardView *_keyboardView;
    int _currentSelectedSlot;
    dispatch_queue_t _hwQueue; // serielle Queue: USB-Zugriffe blockieren nicht die Oberfläche

    // Profilverwaltung
    NSPopUpButton *_profilesPopUp;
    NSButton *_applyProfileBtn;
    NSButton *_saveProfileBtn;
    NSButton *_renameProfileBtn;
    NSButton *_deleteProfileBtn;

    // Editor & Hardware-Steuerung
    NSColorWell *_colorWell;
    NSSlider *_brightnessSlider;
    uint8_t _currentBrightness;

    // Auto-Trigger (App-Fokus-Erkennung)
    NSButton *_autoTriggerCheckbox;
    NSTextField *_linkedAppLabel;
    NSButton *_assignAppBtn;
    NSButton *_clearAppBtn;
    NSButton *_setDefaultBtn;
    BOOL _autoTriggerEnabled;
    NSString *_fallbackProfileId;
    NSString *_pendingAppBundleId;
    NSString *_pendingAppName;

    NSMutableArray<NSMutableDictionary *> *_profiles;
    NSString *_currentActiveProfileId;
    BOOL _hasUnsavedEdits;
    NSStatusItem *_statusItem;
}
@end

static NSString * const kCustomProfilesKey = @"CIYX79CustomProfiles_v1";
static NSString * const kActiveProfileIdKey = @"CIYX79ActiveProfileId_v1";
static NSString * const kAutoTriggerEnabledKey = @"CIYX79AutoTriggerEnabled_v1";
static NSString * const kFallbackProfileIdKey = @"CIYX79FallbackProfileId_v1";

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

@implementation AppDelegate

/// Gemeinsame Farbpalette für Tastenbuttons (Reihenfolge = Button-Tag).
static NSArray<NSDictionary *> *KeyPalette(void) {
    static NSArray<NSDictionary *> *palette;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        palette = @[
            @{@"name": @"Rot",  @"c": [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0]},
            @{@"name": @"Grün", @"c": [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0]},
            @{@"name": @"Blau", @"c": [NSColor colorWithSRGBRed:0.0 green:0.4 blue:1.0 alpha:1.0]},
            @{@"name": @"Gelb", @"c": [NSColor colorWithSRGBRed:1.0 green:0.9 blue:0.0 alpha:1.0]},
            @{@"name": @"Lila", @"c": [NSColor colorWithSRGBRed:0.9 green:0.0 blue:0.9 alpha:1.0]},
            @{@"name": @"Cyan", @"c": [NSColor colorWithSRGBRed:0.0 green:1.0 blue:1.0 alpha:1.0]},
            @{@"name": @"Weiß", @"c": [NSColor colorWithSRGBRed:1.0 green:1.0 blue:1.0 alpha:1.0]},
            @{@"name": @"Aus",  @"c": [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0]}
        ];
    });
    return palette;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    (void)aNotification;
    _hwQueue = dispatch_queue_create("com.juliengenau.ciy-x79-control.hardware", DISPATCH_QUEUE_SERIAL);
    _currentSelectedSlot = 14; // Standard-Auswahl: Taste W
    _currentBrightness = 5;    // Standard: Maximale Helligkeit (1..5)
    _hasUnsavedEdits = NO;

    [self setupMainMenu];

    // Hauptfenster einrichten
    NSRect frame = NSMakeRect(100, 100, 920, 660);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled |
                                                         NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable |
                                                         NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];

    self.window.title = @"CIY X79 RGB Tastatur-Steuerung (macOS)";
    self.window.minSize = NSMakeSize(880, 600);
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    self.window.releasedWhenClosed = NO;
    [self.window center];

    NSView *content = self.window.contentView;
    content.wantsLayer = YES;
    content.layer.backgroundColor = [NSColor colorWithCalibratedWhite:0.11 alpha:1.0].CGColor;

    [self buildUI:content];
    [self loadProfiles];

    // Status Item in macOS-Menüleiste einrichten
    [self setupStatusItem];

    // Mit Tastatur verbinden
    [[KeyboardBackend shared] connect];
    [self updateConnectionStatus];

    // Zuletzt aktives Profil direkt auf die Tastatur übertragen
    if ([KeyboardBackend shared].isConnected) {
        [self sendCurrentProfileToHardware];
    }

    // Hotplug: Tastatur an-/abstecken automatisch überwachen
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(onHotplug:)
                                                 name:KeyboardBackendHotplugNotification
                                               object:nil];
    [[KeyboardBackend shared] startMonitoring];

    // Auto-Trigger: App-Fokuswechsel im System überwachen (ohne Sonderrechte, unprivilegiert)
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
                                                           selector:@selector(onAppActivated:)
                                                               name:NSWorkspaceDidActivateApplicationNotification
                                                             object:nil];

    // Sleep / Wake: Ruhezustand und Aufwachen für saubere USB-Reinitialisierung überwachen
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
                                                           selector:@selector(onSystemWillSleep:)
                                                               name:NSWorkspaceWillSleepNotification
                                                             object:nil];
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
                                                           selector:@selector(onSystemDidWake:)
                                                               name:NSWorkspaceDidWakeNotification
                                                             object:nil];

    [self.window makeKeyAndOrderFront:nil];
}

- (void)buildUI:(NSView *)container {
    CGFloat y = container.bounds.size.height;

    // Header Title
    NSTextField *titleLabel = [self labelWithText:@"CIY X79 Mechanical Keyboard" fontSize:18 weight:NSFontWeightBold];
    titleLabel.frame = NSMakeRect(24, y - 44, 380, 24);
    titleLabel.autoresizingMask = NSViewMinYMargin;
    [container addSubview:titleLabel];

    NSTextField *subtitleLabel = [self labelWithText:@"ISO-DE 65% Layout • Profil-Zentrierte RGB-Steuerung" fontSize:12 weight:NSFontWeightRegular];
    subtitleLabel.textColor = [NSColor secondaryLabelColor];
    subtitleLabel.frame = NSMakeRect(24, y - 64, 380, 18);
    subtitleLabel.autoresizingMask = NSViewMinYMargin;
    [container addSubview:subtitleLabel];

    // Status Indicator (oben rechts)
    _statusLabel = [self labelWithText:@"● Verbindung wird geprüft..." fontSize:12 weight:NSFontWeightMedium];
    _statusLabel.frame = NSMakeRect(container.bounds.size.width - 340, y - 44, 316, 20);
    _statusLabel.alignment = NSTextAlignmentRight;
    _statusLabel.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [container addSubview:_statusLabel];

    NSButton *reconnectBtn = [NSButton buttonWithTitle:@"Neu verbinden" target:self action:@selector(onReconnect:)];
    reconnectBtn.frame = NSMakeRect(container.bounds.size.width - 130, y - 70, 106, 24);
    reconnectBtn.bezelStyle = NSBezelStyleRounded;
    reconnectBtn.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [container addSubview:reconnectBtn];

    // Trennlinie
    NSBox *sep1 = [[NSBox alloc] initWithFrame:NSMakeRect(24, y - 78, container.bounds.size.width - 48, 1)];
    sep1.boxType = NSBoxSeparator;
    sep1.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [container addSubview:sep1];

    // Sektion: Profil-Verwaltung & Steuerung
    CGFloat profBoxH = 104;
    CGFloat profBoxY = y - 78 - profBoxH - 8;
    NSBox *boxProfile = [[NSBox alloc] initWithFrame:NSMakeRect(24, profBoxY, container.bounds.size.width - 48, profBoxH)];
    boxProfile.title = @"Profil-Verwaltung & Hardware-Übertragung";
    boxProfile.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [container addSubview:boxProfile];

    NSView *profView = boxProfile.contentView;

    // Zeile 1: Profil-Auswahl und Aktionen (y = 44)
    NSTextField *lblProf = [self labelWithText:@"Aktives Profil:" fontSize:12 weight:NSFontWeightMedium];
    lblProf.frame = NSMakeRect(16, 46, 84, 20);
    [profView addSubview:lblProf];

    _profilesPopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(102, 44, 198, 26) pullsDown:NO];
    _profilesPopUp.target = self;
    _profilesPopUp.action = @selector(onSelectProfile:);
    [profView addSubview:_profilesPopUp];

    _applyProfileBtn = [NSButton buttonWithTitle:@"Senden (↵)" target:self action:@selector(onApplyProfileClicked:)];
    _applyProfileBtn.toolTip = @"Ausgewähltes Profil sofort auf die Tastatur übertragen (Modus 19 Customize)";
    _applyProfileBtn.frame = NSMakeRect(306, 44, 128, 26);
    _applyProfileBtn.bezelStyle = NSBezelStyleRounded;
    _applyProfileBtn.keyEquivalent = @"\r"; // Enter-Taste aktiviert Senden
    [profView addSubview:_applyProfileBtn];

    _saveProfileBtn = [NSButton buttonWithTitle:@"Speichern & Senden" target:self action:@selector(onSaveAndSendProfile:)];
    _saveProfileBtn.toolTip = @"Aktuelle Farbanpassungen im Profil sichern und direkt an die Tastatur senden";
    _saveProfileBtn.frame = NSMakeRect(440, 44, 134, 26);
    _saveProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_saveProfileBtn];

    NSButton *newProfileBtn = [NSButton buttonWithTitle:@"+ Neu..." target:self action:@selector(onNewProfile:)];
    newProfileBtn.toolTip = @"Aktuelle Tastenfarben als neues Profil anlegen und speichern";
    newProfileBtn.frame = NSMakeRect(580, 44, 60, 26);
    newProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:newProfileBtn];

    _renameProfileBtn = [NSButton buttonWithTitle:@"Umbenennen" target:self action:@selector(onRenameProfile:)];
    _renameProfileBtn.toolTip = @"Ausgewähltes Profil umbenennen";
    _renameProfileBtn.frame = NSMakeRect(646, 44, 84, 26);
    _renameProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_renameProfileBtn];

    _deleteProfileBtn = [NSButton buttonWithTitle:@"Löschen" target:self action:@selector(onDeleteProfile:)];
    _deleteProfileBtn.toolTip = @"Ausgewähltes Profil entfernen";
    _deleteProfileBtn.frame = NSMakeRect(736, 44, 64, 26);
    _deleteProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_deleteProfileBtn];

    NSPopUpButton *actionsBtn = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(806, 44, 66, 26) pullsDown:YES];
    [actionsBtn addItemWithTitle:@"Mehr ▾"];
    NSMenuItem *exportItem = [[NSMenuItem alloc] initWithTitle:@"Profile exportieren..." action:@selector(onExportProfiles:) keyEquivalent:@"e"];
    exportItem.target = self;
    [actionsBtn.menu addItem:exportItem];
    NSMenuItem *importItem = [[NSMenuItem alloc] initWithTitle:@"Profile importieren..." action:@selector(onImportProfiles:) keyEquivalent:@"o"];
    importItem.target = self;
    [actionsBtn.menu addItem:importItem];
    [actionsBtn.menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *fallbackItem = [[NSMenuItem alloc] initWithTitle:@"Als Standard-Fallback festlegen" action:@selector(onSetDefaultFallback:) keyEquivalent:@""];
    fallbackItem.target = self;
    [actionsBtn.menu addItem:fallbackItem];
    NSMenuItem *resetItem = [[NSMenuItem alloc] initWithTitle:@"Auf Standard zurücksetzen" action:@selector(onResetProfiles:) keyEquivalent:@""];
    resetItem.target = self;
    [actionsBtn.menu addItem:resetItem];
    [profView addSubview:actionsBtn];

    // Zeile 2: Auto-Trigger bei App-Fokus (y = 10)
    _autoTriggerCheckbox = [NSButton checkboxWithTitle:@"⚡ Auto-Trigger bei App-Fokus" target:self action:@selector(onToggleAutoTrigger:)];
    _autoTriggerCheckbox.frame = NSMakeRect(16, 11, 206, 22);
    _autoTriggerCheckbox.toolTip = @"Wechselt Profile automatisch, sobald verknüpfte Spiele oder Anwendungen aktiv werden (ohne Keylogger, 100% unprivilegiert)";
    [profView addSubview:_autoTriggerCheckbox];

    NSTextField *lblLink = [self labelWithText:@"Verknüpfte App:" fontSize:12 weight:NSFontWeightMedium];
    lblLink.frame = NSMakeRect(228, 13, 96, 18);
    [profView addSubview:lblLink];

    _linkedAppLabel = [self labelWithText:@"Keine App verknüpft (Manuell)" fontSize:12 weight:NSFontWeightRegular];
    _linkedAppLabel.frame = NSMakeRect(326, 13, 240, 18);
    _linkedAppLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [profView addSubview:_linkedAppLabel];

    _assignAppBtn = [NSButton buttonWithTitle:@"App wählen..." target:self action:@selector(onAssignApp:)];
    _assignAppBtn.toolTip = @"Eine Applikation (z. B. aus /Applications) mit diesem Profil verknüpfen";
    _assignAppBtn.frame = NSMakeRect(572, 9, 106, 26);
    _assignAppBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_assignAppBtn];

    _clearAppBtn = [NSButton buttonWithTitle:@"✕ Lösen" target:self action:@selector(onClearApp:)];
    _clearAppBtn.toolTip = @"App-Verknüpfung für dieses Profil entfernen";
    _clearAppBtn.frame = NSMakeRect(682, 9, 74, 26);
    _clearAppBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_clearAppBtn];

    _setDefaultBtn = [NSButton buttonWithTitle:@"☆ Fallback" target:self action:@selector(onSetDefaultFallback:)];
    _setDefaultBtn.toolTip = @"Dieses Profil als Standard-Fallback verwenden, wenn eine nicht verknüpfte App im Fokus ist";
    _setDefaultBtn.frame = NSMakeRect(760, 9, 112, 26);
    _setDefaultBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_setDefaultBtn];

    // Sektion: Interaktiver Tastatur-Editor
    CGFloat kbBoxY = 44;
    CGFloat kbBoxH = profBoxY - kbBoxY - 8;
    NSBox *boxKeyboard = [[NSBox alloc] initWithFrame:NSMakeRect(24, kbBoxY, container.bounds.size.width - 48, kbBoxH)];
    boxKeyboard.title = @"Tasten-Editor (65% ISO-DE Layout)";
    boxKeyboard.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [container addSubview:boxKeyboard];

    NSView *kbView = boxKeyboard.contentView;

    // Farb- und Tasten-Leiste
    _selectedKeyLabel = [self labelWithText:@"Ausgewählte Taste: [ W ] (Slot 14)" fontSize:12 weight:NSFontWeightBold];
    _selectedKeyLabel.frame = NSMakeRect(16, kbBoxH - 46, 184, 22);
    _selectedKeyLabel.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_selectedKeyLabel];

    NSTextField *lblColorPick = [self labelWithText:@"Farbe:" fontSize:12 weight:NSFontWeightMedium];
    lblColorPick.frame = NSMakeRect(204, kbBoxH - 46, 42, 22);
    lblColorPick.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:lblColorPick];

    NSArray<NSDictionary *> *colors = KeyPalette();
    CGFloat btnX = 248;
    for (NSDictionary *cDict in colors) {
        NSButton *b = [NSButton buttonWithTitle:cDict[@"name"] target:self action:@selector(onKeyColorChosen:)];
        b.frame = NSMakeRect(btnX, kbBoxH - 48, 44, 24);
        b.bezelStyle = NSBezelStyleRounded;
        b.tag = (NSInteger)[colors indexOfObject:cDict];
        b.autoresizingMask = NSViewMinYMargin;
        [kbView addSubview:b];
        btnX += 48;
    }

    // Farbrad / Farbwähler (NSColorWell)
    _colorWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(btnX + 2, kbBoxH - 47, 34, 22)];
    _colorWell.color = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0];
    _colorWell.toolTip = @"Eigene Farbe wählen (macOS Farbrad)...";
    _colorWell.target = self;
    _colorWell.action = @selector(onColorWellChanged:);
    _colorWell.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_colorWell];

    NSButton *fillAllBtn = [NSButton buttonWithTitle:@"Alle..." target:self action:@selector(onFillAllKeys:)];
    fillAllBtn.toolTip = @"Alle 68 Tasten im Editor mit einer Basisfarbe füllen";
    fillAllBtn.frame = NSMakeRect(btnX + 42, kbBoxH - 48, 64, 24);
    fillAllBtn.bezelStyle = NSBezelStyleRounded;
    fillAllBtn.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:fillAllBtn];

    // Helligkeits-Steuerung (Slider 1..5)
    NSTextField *lblBright = [self labelWithText:@"☀️" fontSize:13 weight:NSFontWeightRegular];
    lblBright.frame = NSMakeRect(btnX + 112, kbBoxH - 46, 18, 22);
    lblBright.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:lblBright];

    _brightnessSlider = [[NSSlider alloc] initWithFrame:NSMakeRect(btnX + 132, kbBoxH - 48, 66, 24)];
    _brightnessSlider.minValue = 1;
    _brightnessSlider.maxValue = 5;
    _brightnessSlider.numberOfTickMarks = 5;
    _brightnessSlider.allowsTickMarkValuesOnly = YES;
    _brightnessSlider.integerValue = 5;
    _brightnessSlider.target = self;
    _brightnessSlider.action = @selector(onBrightnessChanged:);
    _brightnessSlider.toolTip = @"Helligkeit des Profils (1 bis 5)";
    _brightnessSlider.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_brightnessSlider];

    // Interaktive Canvas-Ansicht
    NSRect canvasRect = NSMakeRect(16, 14, kbView.bounds.size.width - 32, kbBoxH - 68);
    _keyboardView = [[CIYKeyboardView alloc] initWithFrame:canvasRect];
    _keyboardView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _keyboardView.delegate = self;
    _keyboardView.selectedSlot = _currentSelectedSlot;
    [kbView addSubview:_keyboardView];

    // Fußzeile
    NSTextField *footer = [self labelWithText:@"CIY X79 Controller • Profil-Zentrierte IOKit USB Ansteuerung (Report 0x0B -> 50ms -> Modus 19)" fontSize:11 weight:NSFontWeightRegular];
    footer.textColor = [NSColor tertiaryLabelColor];
    footer.frame = NSMakeRect(24, 16, 680, 18);
    [container addSubview:footer];
}

#pragma mark - Profilverwaltung

- (void)loadProfiles {
    NSArray *saved = [[NSUserDefaults standardUserDefaults] arrayForKey:kCustomProfilesKey];
    if (saved && [saved isKindOfClass:[NSArray class]] && saved.count > 0) {
        _profiles = [NSMutableArray arrayWithCapacity:saved.count];
        for (id item in saved) {
            if ([item isKindOfClass:[NSDictionary class]]) {
                [_profiles addObject:[item mutableCopy]];
            }
        }
    } else {
        _profiles = [NSMutableArray array];
        [self addDefaultProfiles];
        [self saveProfilesToDefaults];
    }

    NSString *lastActiveId = [[NSUserDefaults standardUserDefaults] stringForKey:kActiveProfileIdKey];
    NSUInteger selIdx = 0;
    if (lastActiveId) {
        for (NSUInteger i = 0; i < _profiles.count; i++) {
            if ([_profiles[i][@"id"] isEqualToString:lastActiveId]) {
                selIdx = i;
                break;
            }
        }
    }

    [self refreshProfilesPopUp];
    if (selIdx < _profiles.count) {
        [_profilesPopUp selectItemAtIndex:(NSInteger)selIdx];
        _currentActiveProfileId = _profiles[selIdx][@"id"];
        [self loadProfileIntoView:_profiles[selIdx]];
    }

    _autoTriggerEnabled = [[NSUserDefaults standardUserDefaults] boolForKey:kAutoTriggerEnabledKey];
    _fallbackProfileId = [[NSUserDefaults standardUserDefaults] stringForKey:kFallbackProfileIdKey];
    BOOL fallbackFound = NO;
    if (_fallbackProfileId) {
        for (NSDictionary *p in _profiles) {
            if ([p[@"id"] isEqualToString:_fallbackProfileId]) {
                fallbackFound = YES;
                break;
            }
        }
    }
    if (!fallbackFound && _profiles.count > 0) {
        NSUInteger fallbackIdx = (_profiles.count > 6) ? 6 : 0;
        _fallbackProfileId = _profiles[fallbackIdx][@"id"];
        [[NSUserDefaults standardUserDefaults] setObject:_fallbackProfileId forKey:kFallbackProfileIdKey];
    }

    [self setHasUnsavedEdits:NO];
    [self updateButtonsState];
    [self updateAppTriggerUI];
}

- (void)addDefaultProfiles {
    // 1. WASD Gaming (Rot / Blau)
    [self addBuiltinProfileNamed:@"🎮 WASD Gaming (Rot / Basis Blau)"
                         baseHex:@"#0066FF"
                        wasdHex:@"#FF0000"
                       arrowHex:@"#0066FF"];

    // 2. WASD Stealth (Grün / Aus)
    [self addBuiltinProfileNamed:@"🎮 WASD Stealth (Grün / Basis Aus)"
                         baseHex:@"#000000"
                        wasdHex:@"#00FF00"
                       arrowHex:@"#000000"];

    // 3. Pfeiltasten (Gelb / Blau)
    [self addBuiltinProfileNamed:@"🎯 Pfeiltasten (Gelb / Basis Blau)"
                         baseHex:@"#0066FF"
                        wasdHex:@"#0066FF"
                       arrowHex:@"#FFFF00"];

    // 4. WASD + Pfeile (Rot & Gelb / Blau)
    [self addBuiltinProfileNamed:@"🕹 WASD + Pfeile (Rot & Gelb / Blau)"
                         baseHex:@"#0066FF"
                        wasdHex:@"#FF0000"
                       arrowHex:@"#FFFF00"];

    // 5. Vollrot (Alles Rot)
    [self addBuiltinProfileNamed:@"🔴 Vollrot (Alles Rot)"
                         baseHex:@"#FF0000"
                        wasdHex:@"#FF0000"
                       arrowHex:@"#FF0000"];

    // 6. Cyberpunk (Cyan / Lila)
    [self addBuiltinProfileNamed:@"🌌 Cyberpunk (Cyan / Lila)"
                         baseHex:@"#990099"
                        wasdHex:@"#00FFFF"
                       arrowHex:@"#FFFF00"];

    // 7. Büro / Clean (Dezent Weiß)
    [self addBuiltinProfileNamed:@"💼 Büro / Clean (Dezent Weiß)"
                         baseHex:@"#CCCCCC"
                        wasdHex:@"#FFFFFF"
                       arrowHex:@"#FFFFFF"];

    // 8. Beleuchtung Aus
    [self addBuiltinProfileNamed:@"⚫ Beleuchtung Aus"
                         baseHex:@"#000000"
                        wasdHex:@"#000000"
                       arrowHex:@"#000000"];
}

- (void)addBuiltinProfileNamed:(NSString *)name
                       baseHex:(NSString *)baseHex
                       wasdHex:(NSString *)wasdHex
                      arrowHex:(NSString *)arrowHex {
    NSMutableDictionary<NSString *, NSString *> *slots = [NSMutableDictionary dictionaryWithCapacity:126];
    for (int i = 0; i < 126; i++) {
        slots[@(i).stringValue] = baseHex;
    }
    const int wasd[] = {14, 9, 15, 21};
    for (int k = 0; k < 4; k++) slots[@(wasd[k]).stringValue] = wasdHex;

    const int arrows[] = {88, 83, 89, 95};
    for (int k = 0; k < 4; k++) slots[@(arrows[k]).stringValue] = arrowHex;

    [_profiles addObject:[@{
        @"id": [[NSUUID UUID] UUIDString],
        @"name": name,
        @"slots": slots
    } mutableCopy]];
}

- (void)saveProfilesToDefaults {
    [[NSUserDefaults standardUserDefaults] setObject:_profiles forKey:kCustomProfilesKey];
    if (_currentActiveProfileId) {
        [[NSUserDefaults standardUserDefaults] setObject:_currentActiveProfileId forKey:kActiveProfileIdKey];
    }
    [[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)refreshProfilesPopUp {
    [_profilesPopUp removeAllItems];
    for (NSDictionary *prof in _profiles) {
        NSString *name = prof[@"name"] ?: @"Unbenannt";
        [_profilesPopUp addItemWithTitle:name];
    }
}

- (void)updateButtonsState {
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    BOOL hasSelection = (idx >= 0 && (NSUInteger)idx < _profiles.count);

    _applyProfileBtn.enabled = hasSelection;
    _saveProfileBtn.enabled = hasSelection;
    _renameProfileBtn.enabled = hasSelection;
    _deleteProfileBtn.enabled = (hasSelection && _profiles.count > 1);
    [self updateAppTriggerUI];
}

#pragma mark - Auto-Trigger (App-Fokus & Profilwechsel)

- (void)updateAppTriggerUI {
    _autoTriggerCheckbox.state = _autoTriggerEnabled ? NSControlStateValueOn : NSControlStateValueOff;

    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx >= 0 && (NSUInteger)idx < _profiles.count) {
        NSDictionary *prof = _profiles[(NSUInteger)idx];
        NSString *appName = prof[@"appName"];
        NSString *bundleId = prof[@"appBundleId"];
        BOOL isFallback = (_fallbackProfileId && [prof[@"id"] isEqualToString:_fallbackProfileId]);

        if (appName.length > 0 || bundleId.length > 0) {
            NSString *display = appName.length > 0 ? appName : bundleId;
            _linkedAppLabel.stringValue = [NSString stringWithFormat:@"%@%@ (%@)", isFallback ? @"[★ Fallback] " : @"", display, bundleId ?: @""];
            _linkedAppLabel.textColor = [NSColor controlAccentColor];
            _clearAppBtn.enabled = YES;
        } else {
            _linkedAppLabel.stringValue = isFallback ? @"[★ Standard-Fallback] Keine App verknüpft" : @"Keine App verknüpft (Manuell)";
            _linkedAppLabel.textColor = isFallback ? [NSColor systemYellowColor] : [NSColor secondaryLabelColor];
            _clearAppBtn.enabled = NO;
        }

        _assignAppBtn.enabled = YES;
        _setDefaultBtn.enabled = !isFallback;
        _setDefaultBtn.title = isFallback ? @"★ Fallback" : @"☆ Als Fallback";
    } else {
        _linkedAppLabel.stringValue = @"-";
        _assignAppBtn.enabled = NO;
        _clearAppBtn.enabled = NO;
        _setDefaultBtn.enabled = NO;
    }

    [self updateStatusItemMenu];
}

- (void)onToggleAutoTrigger:(NSButton *)sender {
    _autoTriggerEnabled = (sender.state == NSControlStateValueOn);
    [[NSUserDefaults standardUserDefaults] setBool:_autoTriggerEnabled forKey:kAutoTriggerEnabledKey];
    [[NSUserDefaults standardUserDefaults] synchronize];
    [self updateAppTriggerUI];

    if (_autoTriggerEnabled) {
        _statusLabel.stringValue = @"● Auto-Trigger aktiv (wechselt Profil bei App-Fokus)";
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (frontApp && frontApp.bundleIdentifier) {
            _pendingAppBundleId = [frontApp.bundleIdentifier copy];
            _pendingAppName = [(frontApp.localizedName ?: frontApp.bundleIdentifier) copy];
            [self executeAutoSwitch];
        }
    } else {
        [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
        _statusLabel.stringValue = @"● Auto-Trigger deaktiviert (Manuelle Profilwahl)";
        _statusLabel.textColor = [NSColor secondaryLabelColor];
    }
}

- (void)toggleAutoTriggerMenu:(id)sender {
    (void)sender;
    _autoTriggerEnabled = !_autoTriggerEnabled;
    [[NSUserDefaults standardUserDefaults] setBool:_autoTriggerEnabled forKey:kAutoTriggerEnabledKey];
    [[NSUserDefaults standardUserDefaults] synchronize];
    [self updateAppTriggerUI];

    if (_autoTriggerEnabled) {
        _statusLabel.stringValue = @"● Auto-Trigger aktiv (wechselt Profil bei App-Fokus)";
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (frontApp && frontApp.bundleIdentifier) {
            _pendingAppBundleId = [frontApp.bundleIdentifier copy];
            _pendingAppName = [(frontApp.localizedName ?: frontApp.bundleIdentifier) copy];
            [self executeAutoSwitch];
        }
    } else {
        [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
        _statusLabel.stringValue = @"● Auto-Trigger deaktiviert (Manuelle Profilwahl)";
        _statusLabel.textColor = [NSColor secondaryLabelColor];
    }
}

- (void)onAssignApp:(id)sender {
    (void)sender;
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Anwendung auswählen für automatisches Profil";
    panel.prompt = @"Verknüpfen";
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;
    panel.canChooseFiles = YES;
    panel.directoryURL = [NSURL fileURLWithPath:@"/Applications"];
    panel.allowedContentTypes = @[UTTypeApplication];

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            NSBundle *appBundle = [NSBundle bundleWithURL:panel.URL];
            NSString *bundleId = appBundle.bundleIdentifier;
            NSString *appName = [panel.URL.lastPathComponent stringByDeletingPathExtension];
            if (!bundleId || bundleId.length == 0) {
                bundleId = [NSString stringWithFormat:@"local.%@", appName];
            }

            NSMutableDictionary *prof = self->_profiles[(NSUInteger)idx];

            // Bestehende Verknüpfung dieser App bei anderen Profilen entfernen (Konfliktvermeidung)
            for (NSMutableDictionary *other in self->_profiles) {
                if (other != prof && [other[@"appBundleId"] isEqualToString:bundleId]) {
                    [other removeObjectForKey:@"appBundleId"];
                    [other removeObjectForKey:@"appName"];
                }
            }

            prof[@"appBundleId"] = bundleId;
            prof[@"appName"] = appName;

            [self saveProfilesToDefaults];
            [self updateAppTriggerUI];

            self->_statusLabel.stringValue = [NSString stringWithFormat:@"● Profil „%@“ mit App „%@“ verknüpft", prof[@"name"] ?: @"", appName];
            self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        }
    }];
}

- (void)onClearApp:(id)sender {
    (void)sender;
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    [prof removeObjectForKey:@"appBundleId"];
    [prof removeObjectForKey:@"appName"];

    [self saveProfilesToDefaults];
    [self updateAppTriggerUI];

    _statusLabel.stringValue = [NSString stringWithFormat:@"● App-Verknüpfung für Profil „%@“ gelöst", prof[@"name"] ?: @""];
    _statusLabel.textColor = [NSColor secondaryLabelColor];
}

- (void)onSetDefaultFallback:(id)sender {
    (void)sender;
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    NSDictionary *prof = _profiles[(NSUInteger)idx];
    _fallbackProfileId = prof[@"id"];
    [[NSUserDefaults standardUserDefaults] setObject:_fallbackProfileId forKey:kFallbackProfileIdKey];
    [[NSUserDefaults standardUserDefaults] synchronize];

    [self updateAppTriggerUI];
    _statusLabel.stringValue = [NSString stringWithFormat:@"● Profil „%@“ als Standard-Fallback festgelegt", prof[@"name"] ?: @""];
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
}

- (void)onAppActivated:(NSNotification *)note {
    if (!_autoTriggerEnabled) return;

    NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
    if (!app || [app isEqual:[NSRunningApplication currentApplication]]) return;

    NSString *bundleId = app.bundleIdentifier;
    if (!bundleId || bundleId.length == 0) {
        if (app.localizedName.length > 0) {
            bundleId = [NSString stringWithFormat:@"local.%@", app.localizedName];
        } else {
            return;
        }
    }

    NSString *ownBundleId = [[NSBundle mainBundle] bundleIdentifier];
    if (ownBundleId && [bundleId isEqualToString:ownBundleId]) {
        // Eigenen Fokus auf CIYX79Control ignorieren
        return;
    }

    _pendingAppBundleId = [bundleId copy];
    _pendingAppName = [(app.localizedName ?: bundleId) copy];

    // Debouncing (Entprellen) 250 ms gegen Cmd+Tab Stakkato:
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
    [self performSelector:@selector(executeAutoSwitch) withObject:nil afterDelay:0.25];
}

- (void)executeAutoSwitch {
    if (!_autoTriggerEnabled || !_pendingAppBundleId) return;

    // 1. Suche nach einem Profil mit passender Bundle-ID
    NSInteger targetIdx = -1;
    for (NSUInteger i = 0; i < _profiles.count; i++) {
        NSString *linkedId = _profiles[i][@"appBundleId"];
        if (linkedId && [linkedId isEqualToString:_pendingAppBundleId]) {
            targetIdx = (NSInteger)i;
            break;
        }
    }

    // 2. Falls kein Treffer: Fallback-Profil verwenden
    if (targetIdx < 0) {
        if (_fallbackProfileId) {
            for (NSUInteger i = 0; i < _profiles.count; i++) {
                if ([_profiles[i][@"id"] isEqualToString:_fallbackProfileId]) {
                    targetIdx = (NSInteger)i;
                    break;
                }
            }
        }
    }

    if (targetIdx < 0) return;

    // Kein redundanter USB-Traffic, wenn das Profil schon aktiv ist
    if ([_profiles[(NSUInteger)targetIdx][@"id"] isEqualToString:_currentActiveProfileId]) {
        return;
    }

    [self activateProfileAtIndex:targetIdx];

    NSString *name = [self currentProfileName];
    _statusLabel.stringValue = [NSString stringWithFormat:@"● Auto-Trigger: „%@“ aktiv (%@)", name, _pendingAppName ?: @"App"];
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
}

- (NSString *)currentProfileName {
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx >= 0 && (NSUInteger)idx < _profiles.count) {
        return _profiles[(NSUInteger)idx][@"name"] ?: @"Profil";
    }
    return @"Profil";
}

- (void)loadProfileIntoView:(NSDictionary *)prof {
    NSDictionary<NSString *, NSString *> *slots = prof[@"slots"];
    if (!slots || ![slots isKindOfClass:[NSDictionary class]]) return;

    NSMutableDictionary<NSNumber *, NSColor *> *keyColors = [NSMutableDictionary dictionaryWithCapacity:slots.count];
    for (NSString *slotStr in slots) {
        NSScanner *scanner = [NSScanner scannerWithString:slotStr];
        int slot = -1;
        // Strikt gegen Out-of-Bounds absichern: Slot muss eine Ganzzahl >= 0 und Slot + 1 < CIY_LED_COUNT sein
        if ([scanner scanInt:&slot] && scanner.isAtEnd && slot >= 0 && (slot + 1) < CIY_LED_COUNT) {
            NSString *hex = slots[slotStr];
            keyColors[@(slot)] = ColorFromHex(hex);
        }
    }
    [_keyboardView applyKeyColors:keyColors];
    if (self->_colorWell && self->_currentSelectedSlot >= 0) {
        NSColor *selC = keyColors[@(self->_currentSelectedSlot)];
        if (selC) self->_colorWell.color = selC;
    }
}

#pragma mark - Profil Aktionen

- (void)activateProfileAtIndex:(NSInteger)idx {
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    [_profilesPopUp selectItemAtIndex:idx];
    NSDictionary *prof = _profiles[(NSUInteger)idx];
    _currentActiveProfileId = prof[@"id"];
    [self saveProfilesToDefaults];

    [self loadProfileIntoView:prof];
    [self setHasUnsavedEdits:NO];
    [self updateButtonsState];

    // Direkt das ausgewählte Profil an die Tastatur senden
    [self sendCurrentProfileToHardware];
}

- (void)onSelectProfile:(NSPopUpButton *)sender {
    [self activateProfileAtIndex:sender.indexOfSelectedItem];
}

- (void)onStatusMenuSelectProfile:(NSMenuItem *)sender {
    [self activateProfileAtIndex:sender.tag];
}

- (void)onApplyProfileClicked:(id)sender {
    (void)sender;
    [self sendCurrentProfileToHardware];
}

- (void)onSaveAndSendProfile:(id)sender {
    (void)sender;
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *name = prof[@"name"] ?: @"Profil";

    // Aktuelle Editor-Farben im ausgewählten Profil speichern
    NSDictionary<NSNumber *, NSColor *> *allColors = [_keyboardView allKeyColors];
    NSMutableDictionary<NSString *, NSString *> *slotHex = [NSMutableDictionary dictionaryWithCapacity:allColors.count];
    for (NSNumber *slotNum in allColors) {
        slotHex[slotNum.stringValue] = HexFromColor(allColors[slotNum]);
    }
    prof[@"slots"] = slotHex;

    [self saveProfilesToDefaults];
    [self setHasUnsavedEdits:NO];
    [self updateButtonsState];

    // Sofort an Tastatur übertragen
    [self sendCurrentProfileToHardware];

    _statusLabel.stringValue = [NSString stringWithFormat:@"● Profil „%@“ gespeichert & gesendet", name];
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
}

- (void)onNewProfile:(id)sender {
    (void)sender;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Neues Beleuchtungsprofil anlegen";
    alert.informativeText = @"Geben Sie einen Namen für das neue Profil ein:";
    [alert addButtonWithTitle:@"Speichern & Senden"];
    [alert addButtonWithTitle:@"Abbrechen"];

    NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
    input.placeholderString = @"z. B. Coding, Cyberpunk, CS:GO";
    input.stringValue = @"";
    alert.accessoryView = input;
    [alert.window setInitialFirstResponder:input];

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            NSString *name = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (name.length == 0) {
                name = [NSString stringWithFormat:@"Profil %lu", (unsigned long)(self->_profiles.count + 1)];
            } else if (name.length > 40) {
                name = [name substringToIndex:40];
            }

            NSDictionary<NSNumber *, NSColor *> *allColors = [self->_keyboardView allKeyColors];
            NSMutableDictionary<NSString *, NSString *> *slotHex = [NSMutableDictionary dictionaryWithCapacity:allColors.count];
            for (NSNumber *slotNum in allColors) {
                slotHex[slotNum.stringValue] = HexFromColor(allColors[slotNum]);
            }

            NSMutableDictionary *newProf = [NSMutableDictionary dictionary];
            NSString *newId = [[NSUUID UUID] UUIDString];
            newProf[@"id"] = newId;
            newProf[@"name"] = name;
            newProf[@"slots"] = slotHex;

            [self->_profiles addObject:newProf];
            self->_currentActiveProfileId = newId;
            [self saveProfilesToDefaults];
            [self refreshProfilesPopUp];

            [self->_profilesPopUp selectItemAtIndex:(NSInteger)(self->_profiles.count - 1)];
            [self setHasUnsavedEdits:NO];
            [self updateButtonsState];

            [self sendCurrentProfileToHardware];
        }
    }];
}

- (void)onRenameProfile:(id)sender {
    (void)sender;
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *currentName = prof[@"name"] ?: @"";

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Profil umbenennen";
    alert.informativeText = @"Geben Sie den neuen Namen für dieses Profil ein:";
    [alert addButtonWithTitle:@"Umbenennen"];
    [alert addButtonWithTitle:@"Abbrechen"];

    NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
    input.stringValue = currentName;
    alert.accessoryView = input;
    [alert.window setInitialFirstResponder:input];

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            NSString *name = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (name.length > 0) {
                if (name.length > 40) name = [name substringToIndex:40];
                prof[@"name"] = name;
                [self saveProfilesToDefaults];
                [self refreshProfilesPopUp];
                [self->_profilesPopUp selectItemAtIndex:idx];
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"● Profil umbenannt in „%@“", name];
                self->_statusLabel.textColor = [NSColor secondaryLabelColor];
            }
        }
    }];
}

- (void)onDeleteProfile:(id)sender {
    (void)sender;
    NSInteger idx = _profilesPopUp.indexOfSelectedItem;
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    if (_profiles.count <= 1) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Profil kann nicht gelöscht werden";
        alert.informativeText = @"Es muss mindestens ein Profil erhalten bleiben.";
        [alert addButtonWithTitle:@"OK"];
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *name = prof[@"name"] ?: @"Profil";

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithFormat:@"Profil „%@“ löschen?", name];
    alert.informativeText = @"Möchten Sie dieses Beleuchtungsprofil wirklich endgültig löschen?";
    [alert addButtonWithTitle:@"Löschen"];
    [alert addButtonWithTitle:@"Abbrechen"];
    alert.alertStyle = NSAlertStyleCritical;

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            NSString *deletedId = self->_profiles[(NSUInteger)idx][@"id"];
            [self->_profiles removeObjectAtIndex:(NSUInteger)idx];
            [self saveProfilesToDefaults];

            NSUInteger nextIdx = (idx > 0) ? (NSUInteger)(idx - 1) : 0;
            self->_currentActiveProfileId = self->_profiles[nextIdx][@"id"];
            if ([self->_fallbackProfileId isEqualToString:deletedId]) {
                self->_fallbackProfileId = self->_profiles[nextIdx][@"id"];
                [[NSUserDefaults standardUserDefaults] setObject:self->_fallbackProfileId forKey:kFallbackProfileIdKey];
            }
            [self refreshProfilesPopUp];
            [self->_profilesPopUp selectItemAtIndex:(NSInteger)nextIdx];
            [self loadProfileIntoView:self->_profiles[nextIdx]];

            [self setHasUnsavedEdits:NO];
            [self updateButtonsState];
            [self sendCurrentProfileToHardware];
        }
    }];
}

- (void)onFillAllKeys:(id)sender {
    NSView *targetView = self.window.contentView;
    NSPoint popLocation = NSMakePoint(200, 200);

    if ([sender isKindOfClass:[NSView class]]) {
        targetView = (NSView *)sender;
        popLocation = NSMakePoint(0, 0);
    } else if (self->_colorWell) {
        targetView = self->_colorWell;
        popLocation = NSMakePoint(0, 0);
    }

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Alle Tasten färben"];
    NSArray<NSDictionary *> *colors = KeyPalette();
    for (NSDictionary *cDict in colors) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Alle Tasten: %@", cDict[@"name"]]
                                                      action:@selector(onFillAllWithColorItem:)
                                               keyEquivalent:@""];
        item.target = self;
        item.representedObject = cDict[@"c"];
        [menu addItem:item];
    }
    [menu popUpMenuPositioningItem:nil atLocation:popLocation inView:targetView];
}

- (void)onFillAllWithColorItem:(NSMenuItem *)sender {
    NSColor *color = sender.representedObject;
    if (!color) return;

    [_keyboardView resetAllKeysToColor:color];
    [self setHasUnsavedEdits:YES];
    [self updateButtonsState];

    NSString *profName = [self currentProfileName];
    _statusLabel.stringValue = [NSString stringWithFormat:@"● Alle Tasten gefärbt (Profil „%@“ ungespeichert)", profName];
    _statusLabel.textColor = [NSColor systemOrangeColor];
    // WICHTIG: Kein USB-Senden bei Zwischenschritten im Editor
}

#pragma mark - CIYKeyboardViewDelegate & Farbwahl

- (void)keyboardViewDidSelectKey:(int)slot label:(NSString *)label {
    _currentSelectedSlot = slot;
    _selectedKeyLabel.stringValue = [NSString stringWithFormat:@"Ausgewählte Taste: [ %@ ] (Slot %d)", label, slot];
    NSColor *c = [_keyboardView allKeyColors][@(slot)];
    if (c && _colorWell) {
        _colorWell.color = c;
    }
}

- (void)onKeyColorChosen:(NSButton *)sender {
    if (_currentSelectedSlot < 0) return;

    NSArray<NSDictionary *> *colors = KeyPalette();
    if (sender.tag >= 0 && (NSUInteger)sender.tag < colors.count) {
        NSColor *c = colors[(NSUInteger)sender.tag][@"c"];
        [_keyboardView setColor:c forSlot:_currentSelectedSlot];
        if (_colorWell) _colorWell.color = c;
        [self setHasUnsavedEdits:YES];
        [self updateButtonsState];

        NSString *profName = [self currentProfileName];
        _statusLabel.stringValue = [NSString stringWithFormat:@"● Taste bearbeitet (Profil „%@“ ungespeichert)", profName];
        _statusLabel.textColor = [NSColor systemOrangeColor];
        // WICHTIG: Kein USB-Senden bei Zwischenschritten im Editor!
    }
}

- (void)onColorWellChanged:(NSColorWell *)sender {
    if (_currentSelectedSlot < 0) return;
    NSColor *chosen = sender.color;
    [_keyboardView setColor:chosen forSlot:_currentSelectedSlot];
    [self setHasUnsavedEdits:YES];
    [self updateButtonsState];

    NSString *profName = [self currentProfileName];
    _statusLabel.stringValue = [NSString stringWithFormat:@"● Eigene Farbe gewählt (Profil „%@“ ungespeichert)", profName];
    _statusLabel.textColor = [NSColor systemOrangeColor];
}

- (void)onBrightnessChanged:(NSSlider *)sender {
    _currentBrightness = (uint8_t)sender.integerValue;
    sender.toolTip = [NSString stringWithFormat:@"Helligkeit: %d / 5", _currentBrightness];
    [self sendCurrentProfileToHardware];
}

#pragma mark - USB-Hardware Transfer (Reines Profil-Senden)

- (void)sendCurrentProfileToHardware {
    if (![KeyboardBackend shared].isConnected) {
        [self updateConnectionStatus];
        return;
    }

    // Liest den aktuellen Zustand der Tasten aus der Canvas-Ansicht aus
    NSDictionary<NSNumber *, NSColor *> *allColors = [_keyboardView allKeyColors];
    NSDictionary<NSNumber *, NSColor *> *snapshot = [allColors copy];
    uint8_t br = (_currentBrightness >= 1 && _currentBrightness <= 5) ? _currentBrightness : 5;

    [self runHardware:^BOOL{
        return [[KeyboardBackend shared] sendKeyColors:snapshot brightness:br defaultColor:nil];
    } completion:^(BOOL ok) {
        if (ok) {
            NSString *profName = [self currentProfileName];
            self->_statusLabel.stringValue = [NSString stringWithFormat:@"● Profil „%@“ aktiv auf Tastatur", profName];
            self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        } else {
            self->_statusLabel.stringValue = @"● Senden fehlgeschlagen (USB prüfen)";
            self->_statusLabel.textColor = [NSColor systemRedColor];
        }
    }];
}

- (void)onHotplug:(NSNotification *)note {
    BOOL connected = [note.userInfo[@"connected"] boolValue];
    if (!connected) {
        [[KeyboardBackend shared] disconnect];
        [self updateConnectionStatus];
        return;
    }
    [self runHardware:^BOOL{
        return [[KeyboardBackend shared] connect];
    } completion:^(BOOL ok) {
        [self updateConnectionStatus];
        if (ok) {
            // Tastatur nach erneutem Einstecken automatisch mit dem aktiven Profil synchronisieren
            [self sendCurrentProfileToHardware];
        }
    }];
}

- (void)onSystemWillSleep:(NSNotification *)note {
    (void)note;
    // Anstehende Auto-Switches stornieren, damit während des Suspend-Vorgangs keine Transfers laufen
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
    // Verbindung vorübergehend trennen, um verwaiste USB-Handles nach dem Aufwachen zu verhindern
    [[KeyboardBackend shared] disconnect];
    [self updateConnectionStatus];
}

- (void)onSystemDidWake:(NSNotification *)note {
    (void)note;
    // 500 ms Verzögerung, damit macOS USB-Root-Hub und MCU nach dem Aufwachen vollständig initialisiert sind
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(500 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        [self runHardware:^BOOL{
            [[KeyboardBackend shared] disconnect];
            return [[KeyboardBackend shared] connect];
        } completion:^(BOOL ok) {
            [self updateConnectionStatus];
            if (ok) {
                // Den durch den Ruhezustand gelöschten flüchtigen XRAM der Tastatur wieder mit dem Profil befüllen
                [self sendCurrentProfileToHardware];
            }
        }];
    });
}

- (void)onReconnect:(id)sender {
    (void)sender;
    [self runHardware:^BOOL{
        [[KeyboardBackend shared] disconnect];
        return [[KeyboardBackend shared] connect];
    } completion:^(BOOL ok) {
        [self updateConnectionStatus];
        if (ok) {
            [self sendCurrentProfileToHardware];
        }
    }];
}

- (void)setHasUnsavedEdits:(BOOL)edited {
    _hasUnsavedEdits = edited;
    [self.window setDocumentEdited:edited];
}

- (void)updateConnectionStatus {
    BOOL connected = [KeyboardBackend shared].isConnected;
    if (connected) {
        _statusLabel.stringValue = @"● CIY X79 Verbunden (VID: 0x258A / PID: 0x0049)";
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
    } else {
        _statusLabel.stringValue = @"● Tastatur nicht gefunden (USB prüfen)";
        _statusLabel.textColor = [NSColor systemRedColor];
    }
    [self updateStatusItemMenu];
}

#pragma mark - Profil Export & Import

- (void)onExportProfiles:(id)sender {
    (void)sender;
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.title = @"CIY X79 Beleuchtungsprofile exportieren";
    panel.prompt = @"Exportieren";
    panel.nameFieldStringValue = @"CIYX79_Profile.json";
    panel.allowedContentTypes = @[UTTypeJSON];

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            NSError *err = nil;
            NSData *jsonData = [NSJSONSerialization dataWithJSONObject:self->_profiles
                                                               options:NSJSONWritingPrettyPrinted
                                                                 error:&err];
            if (jsonData && !err) {
                BOOL ok = [jsonData writeToURL:panel.URL atomically:YES];
                if (ok) {
                    self->_statusLabel.stringValue = [NSString stringWithFormat:@"● %lu Profil(e) exportiert", (unsigned long)self->_profiles.count];
                    self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
                    return;
                }
            }
            NSAlert *errAlert = [[NSAlert alloc] init];
            errAlert.messageText = @"Export fehlgeschlagen";
            errAlert.informativeText = err.localizedDescription ?: @"Die Datei konnte nicht geschrieben werden.";
            [errAlert beginSheetModalForWindow:self.window completionHandler:nil];
        }
    }];
}

- (void)onImportProfiles:(id)sender {
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"CIY X79 Beleuchtungsprofile importieren";
    panel.prompt = @"Importieren";
    panel.allowedContentTypes = @[UTTypeJSON];
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            NSError *err = nil;
            NSData *data = [NSData dataWithContentsOfURL:panel.URL options:0 error:&err];
            if (!data || err) {
                [self showImportError:@"Datei konnte nicht gelesen werden."];
                return;
            }
            id json = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:&err];
            if (!json || err) {
                [self showImportError:@"Ungültiges JSON-Format."];
                return;
            }

            NSArray *items = nil;
            if ([json isKindOfClass:[NSArray class]]) {
                items = json;
            } else if ([json isKindOfClass:[NSDictionary class]]) {
                items = @[json];
            } else {
                [self showImportError:@"JSON enthält keine Profile."];
                return;
            }

            NSUInteger importedCount = 0;
            NSString *lastId = nil;
            for (id item in items) {
                if ([item isKindOfClass:[NSDictionary class]]) {
                    NSDictionary *dict = (NSDictionary *)item;
                    NSString *name = dict[@"name"] ?: @"Importiertes Profil";
                    NSDictionary *slots = dict[@"slots"];
                    if (slots && [slots isKindOfClass:[NSDictionary class]]) {
                        NSMutableDictionary<NSString *, NSString *> *fullSlots = [NSMutableDictionary dictionaryWithCapacity:126];
                        for (int s = 0; s < 126; s++) {
                            fullSlots[@(s).stringValue] = @"#000000";
                        }
                        for (id slotKey in slots) {
                            NSString *keyStr = [NSString stringWithFormat:@"%@", slotKey];
                            NSScanner *scanner = [NSScanner scannerWithString:keyStr];
                            int slot = -1;
                            // Strikte Validierung: Muss echte Zahl sein und im Bereich 0..124 liegen
                            if ([scanner scanInt:&slot] && scanner.isAtEnd && slot >= 0 && (slot + 1) < CIY_LED_COUNT) {
                                NSString *hex = [NSString stringWithFormat:@"%@", slots[slotKey]];
                                if ([hex hasPrefix:@"#"] && hex.length == 7) {
                                    fullSlots[@(slot).stringValue] = hex;
                                }
                            }
                        }
                        NSMutableDictionary *newProf = [NSMutableDictionary dictionary];
                        NSString *newId = [[NSUUID UUID] UUIDString];
                        newProf[@"id"] = newId;
                        newProf[@"name"] = name;
                        newProf[@"slots"] = fullSlots;
                        if (dict[@"appBundleId"]) newProf[@"appBundleId"] = dict[@"appBundleId"];
                        if (dict[@"appName"]) newProf[@"appName"] = dict[@"appName"];
                        [self->_profiles addObject:newProf];
                        lastId = newId;
                        importedCount++;
                    }
                }
            }

            if (importedCount > 0) {
                self->_currentActiveProfileId = lastId;
                [self saveProfilesToDefaults];
                [self refreshProfilesPopUp];
                [self->_profilesPopUp selectItemAtIndex:(NSInteger)(self->_profiles.count - 1)];
                [self loadProfileIntoView:self->_profiles.lastObject];
                [self setHasUnsavedEdits:NO];
                [self updateButtonsState];
                [self sendCurrentProfileToHardware];

                self->_statusLabel.stringValue = [NSString stringWithFormat:@"● %lu Profil(e) erfolgreich importiert", (unsigned long)importedCount];
                self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
            } else {
                [self showImportError:@"Keine gültigen Beleuchtungsprofile in der Datei gefunden."];
            }
        }
    }];
}

- (void)showImportError:(NSString *)msg {
    NSAlert *errAlert = [[NSAlert alloc] init];
    errAlert.messageText = @"Import fehlgeschlagen";
    errAlert.informativeText = msg;
    [errAlert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)onResetProfiles:(id)sender {
    (void)sender;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Profile auf Werkseinstellungen zurücksetzen?";
    alert.informativeText = @"Alle benutzerdefinierten Profile werden durch die vorkonfigurierten Standard-Profile ersetzt.";
    [alert addButtonWithTitle:@"Zurücksetzen"];
    [alert addButtonWithTitle:@"Abbrechen"];
    alert.alertStyle = NSAlertStyleWarning;

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            [self->_profiles removeAllObjects];
            [self addDefaultProfiles];
            self->_currentActiveProfileId = self->_profiles.firstObject[@"id"];
            self->_fallbackProfileId = (self->_profiles.count > 6) ? self->_profiles[6][@"id"] : self->_profiles[0][@"id"];
            [[NSUserDefaults standardUserDefaults] setObject:self->_fallbackProfileId forKey:kFallbackProfileIdKey];
            [self saveProfilesToDefaults];
            [self refreshProfilesPopUp];
            [self->_profilesPopUp selectItemAtIndex:0];
            [self loadProfileIntoView:self->_profiles.firstObject];
            [self setHasUnsavedEdits:NO];
            [self updateButtonsState];
            [self sendCurrentProfileToHardware];

            self->_statusLabel.stringValue = @"● Profile auf Standardwerte zurückgesetzt";
            self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        }
    }];
}

- (void)onOpenHelp:(id)sender {
    (void)sender;
    NSString *readmePath = [[NSBundle mainBundle] pathForResource:@"README" ofType:@"md"];
    if (!readmePath || ![[NSFileManager defaultManager] fileExistsAtPath:readmePath]) {
        NSString *bundleDir = [[NSBundle mainBundle] bundlePath];
        readmePath = [[bundleDir stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"README.md"];
    }
    if ([[NSFileManager defaultManager] fileExistsAtPath:readmePath]) {
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:readmePath]];
    } else {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"CIY X79 RGB Steuerung (macOS)";
        alert.informativeText = @"Weitere Informationen und Hardware-Details finden Sie in der beiliegenden Dokumentation README.md.";
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
    }
}

#pragma mark - macOS Menüleiste

- (void)setupMainMenu {
    NSMenu *mainMenu = [[NSMenu alloc] init];

    // 1. App Menü (CIY X79 Control)
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"CIY X79 Control"];
    [appMenu addItemWithTitle:@"Über CIY X79 Control" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *hideItem = [[NSMenuItem alloc] initWithTitle:@"CIY X79 Control ausblenden" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:hideItem];

    NSMenuItem *hideOthers = [[NSMenuItem alloc] initWithTitle:@"Andere ausblenden" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    hideOthers.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    [appMenu addItem:hideOthers];

    [appMenu addItemWithTitle:@"Alle einblenden" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"CIY X79 Control beenden" action:@selector(terminate:) keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    appMenuItem.submenu = appMenu;
    [mainMenu addItem:appMenuItem];

    // 2. Ablage (File)
    NSMenuItem *fileMenuItem = [[NSMenuItem alloc] init];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"Ablage"];
    [fileMenu addItemWithTitle:@"Neues Profil..." action:@selector(onNewProfile:) keyEquivalent:@"n"];
    [fileMenu addItemWithTitle:@"Profil speichern & senden" action:@selector(onSaveAndSendProfile:) keyEquivalent:@"s"];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:@"Profile exportieren..." action:@selector(onExportProfiles:) keyEquivalent:@"e"];
    [fileMenu addItemWithTitle:@"Profile importieren..." action:@selector(onImportProfiles:) keyEquivalent:@"o"];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:@"Fenster schließen" action:@selector(performClose:) keyEquivalent:@"w"];
    fileMenuItem.submenu = fileMenu;
    [mainMenu addItem:fileMenuItem];

    // 3. Bearbeiten (Edit)
    NSMenuItem *editMenuItem = [[NSMenuItem alloc] init];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Bearbeiten"];
    [editMenu addItemWithTitle:@"Alle Tasten füllen..." action:@selector(onFillAllKeys:) keyEquivalent:@""];
    editMenuItem.submenu = editMenu;
    [mainMenu addItem:editMenuItem];

    // 4. Tastatur (Hardware)
    NSMenuItem *hwMenuItem = [[NSMenuItem alloc] init];
    NSMenu *hwMenu = [[NSMenu alloc] initWithTitle:@"Tastatur"];
    [hwMenu addItemWithTitle:@"Aktives Profil an Tastatur senden" action:@selector(onApplyProfileClicked:) keyEquivalent:@"r"];
    [hwMenu addItemWithTitle:@"Neu verbinden" action:@selector(onReconnect:) keyEquivalent:@"u"];
    [hwMenu addItem:[NSMenuItem separatorItem]];
    [hwMenu addItemWithTitle:@"Auto-Trigger bei App-Fokus umschalten" action:@selector(toggleAutoTriggerMenu:) keyEquivalent:@"t"];
    [hwMenu addItemWithTitle:@"App mit aktuellem Profil verknüpfen..." action:@selector(onAssignApp:) keyEquivalent:@""];
    [hwMenu addItemWithTitle:@"App-Verknüpfung entfernen" action:@selector(onClearApp:) keyEquivalent:@""];
    [hwMenu addItemWithTitle:@"Aktuelles Profil als Standard-Fallback setzen" action:@selector(onSetDefaultFallback:) keyEquivalent:@""];
    hwMenuItem.submenu = hwMenu;
    [mainMenu addItem:hwMenuItem];

    // 5. Fenster (Window)
    NSMenuItem *windowMenuItem = [[NSMenuItem alloc] init];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Fenster"];
    NSMenuItem *openWinItem = [[NSMenuItem alloc] initWithTitle:@"Hauptfenster öffnen" action:@selector(onOpenMainWindow:) keyEquivalent:@""];
    openWinItem.target = self;
    [windowMenu addItem:openWinItem];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItemWithTitle:@"Minimieren" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:@"Zoomen" action:@selector(performZoom:) keyEquivalent:@""];
    windowMenuItem.submenu = windowMenu;
    [mainMenu addItem:windowMenuItem];
    [NSApp setWindowsMenu:windowMenu];

    // 6. Hilfe (Help)
    NSMenuItem *helpMenuItem = [[NSMenuItem alloc] init];
    NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:@"Hilfe"];
    [helpMenu addItemWithTitle:@"Dokumentation & Anleitung" action:@selector(onOpenHelp:) keyEquivalent:@"?"];
    helpMenuItem.submenu = helpMenu;
    [mainMenu addItem:helpMenuItem];
    [NSApp setHelpMenu:helpMenu];

    [NSApp setMainMenu:mainMenu];
}

#pragma mark - Menüleisten-Statusobjekt (Tray Icon)

- (void)setupStatusItem {
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    NSImage *icon = nil;
    if (@available(macOS 11.0, *)) {
        icon = [NSImage imageWithSystemSymbolName:@"keyboard" accessibilityDescription:@"CIY X79 Control"];
    }
    if (icon) {
        icon.template = YES;
        _statusItem.button.image = icon;
    } else {
        _statusItem.button.title = @"⌨";
    }
    _statusItem.button.toolTip = @"CIY X79 RGB Control";
    [self updateStatusItemMenu];
}

- (void)updateStatusItemMenu {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateStatusItemMenu];
        });
        return;
    }
    if (!_statusItem) return;

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"CIY X79 Status"];

    // 1. Verbindungs-Status Header (nicht auswählbar)
    BOOL connected = [KeyboardBackend shared].isConnected;
    NSString *statusTitle = connected ? @"CIY X79: Verbunden" : @"CIY X79: Getrennt (USB prüfen)";
    NSMenuItem *statusHeader = [[NSMenuItem alloc] initWithTitle:statusTitle action:nil keyEquivalent:@""];
    statusHeader.enabled = NO;
    [menu addItem:statusHeader];

    [menu addItem:[NSMenuItem separatorItem]];

    // 2. Fenster öffnen
    NSMenuItem *openWindowItem = [[NSMenuItem alloc] initWithTitle:@"Hauptfenster öffnen..." action:@selector(onOpenMainWindow:) keyEquivalent:@""];
    openWindowItem.target = self;
    [menu addItem:openWindowItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 3. Profile
    NSMenuItem *profHeader = [[NSMenuItem alloc] initWithTitle:@"Profile:" action:nil keyEquivalent:@""];
    profHeader.enabled = NO;
    [menu addItem:profHeader];

    for (NSUInteger i = 0; i < _profiles.count; i++) {
        NSDictionary *prof = _profiles[i];
        NSString *name = prof[@"name"] ?: [NSString stringWithFormat:@"Profil %lu", (unsigned long)(i + 1)];
        NSString *profId = prof[@"id"];
        BOOL isActive = (profId && [profId isEqualToString:_currentActiveProfileId]);

        NSString *itemTitle = name;
        NSString *appName = prof[@"appName"];
        if (appName.length > 0) {
            itemTitle = [NSString stringWithFormat:@"%@  [%@]", name, appName];
        } else if (_fallbackProfileId && [profId isEqualToString:_fallbackProfileId]) {
            itemTitle = [NSString stringWithFormat:@"%@  [Fallback]", name];
        }

        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:itemTitle action:@selector(onStatusMenuSelectProfile:) keyEquivalent:@""];
        item.target = self;
        item.tag = (NSInteger)i;
        item.state = isActive ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }

    [menu addItem:[NSMenuItem separatorItem]];

    // 4. Auto-Trigger Toggle
    NSMenuItem *triggerItem = [[NSMenuItem alloc] initWithTitle:@"⚡ Auto-Trigger (App-Fokus)" action:@selector(toggleAutoTriggerMenu:) keyEquivalent:@""];
    triggerItem.target = self;
    triggerItem.state = _autoTriggerEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:triggerItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 5. Beenden
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"CIY X79 Control beenden" action:@selector(terminate:) keyEquivalent:@"q"];
    quitItem.target = NSApp;
    [menu addItem:quitItem];

    _statusItem.menu = menu;
}

- (void)onOpenMainWindow:(id)sender {
    (void)sender;
    if (self.window.isMiniaturized) {
        [self.window deminiaturize:nil];
    }
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

#pragma mark - Helpers

/// Führt USB-Operationen asynchron auf der seriellen Queue aus und meldet das Ergebnis auf dem Main-Thread
- (void)runHardware:(BOOL (^)(void))work completion:(void (^_Nullable)(BOOL ok))completion {
    dispatch_async(_hwQueue, ^{
        BOOL ok = work();
        dispatch_async(dispatch_get_main_queue(), ^{
            [self reportResult:ok];
            if (completion) completion(ok);
        });
    });
}

- (void)reportResult:(BOOL)ok {
    if (!ok) {
        NSLog(@"CIY X79: Hardware-Übertragung fehlgeschlagen");
    }
    [self updateConnectionStatus];
}

- (NSTextField *)labelWithText:(NSString *)text fontSize:(CGFloat)size weight:(NSFontWeight)weight {
    NSTextField *lbl = [[NSTextField alloc] initWithFrame:NSZeroRect];
    lbl.stringValue = text;
    lbl.font = [NSFont systemFontOfSize:size weight:weight];
    lbl.editable = NO;
    lbl.selectable = NO;
    lbl.bezeled = NO;
    lbl.drawsBackground = NO;
    return lbl;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    // Die App bleibt als Menüleistensymbol (Tray) aktiv, damit Auto-Trigger
    // und Schnellzugriff auf Profile auch ohne geöffnetes Hauptfenster funktionieren.
    return NO;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag {
    (void)sender;
    if (self.window.isMiniaturized) {
        [self.window deminiaturize:nil];
    }
    if (!flag) {
        [self.window makeKeyAndOrderFront:nil];
    }
    [NSApp activateIgnoringOtherApps:YES];
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:self];
    [[KeyboardBackend shared] stopMonitoring];
    [[KeyboardBackend shared] disconnect];
}

@end
