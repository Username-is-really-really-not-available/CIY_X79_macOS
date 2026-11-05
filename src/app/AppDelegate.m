#import "AppDelegate.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdlib.h>
#import "../core/ciy_battery.h"
#import "../core/ciy_gradient.h"
#import "CIYBatteryMonitor.h"

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

    // Zonen & Verlauf (v1.4)
    NSPopUpButton *_zonePopUp;
    NSButton *_gradientBtn;
    NSWindow *_gradientSheet;
    NSColorWell *_gradStartWell;
    NSColorWell *_gradEndWell;
    NSPopUpButton *_gradDirPopUp;

    // Akku-Spiegelung (v1.4)
    NSButton *_batteryMirrorCheckbox;
    NSPopUpButton *_batterySourcePopUp;
    NSPopUpButton *_batteryTargetPopUp;
    NSTextField *_batteryStatusLabel;
    NSString *_customBatterySlots;

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
    BOOL _isWakingUp;
    BOOL _wakeSyncScheduled;
    BOOL _sendInFlight;
    BOOL _sendDirty;
    CFAbsoluteTime _lastSendTime;
    NSUInteger _wakeRetryCount;
    NSStatusItem *_statusItem;
}
@end

static NSString * const kCustomProfilesKey = @"CIYX79CustomProfiles_v1";
static NSString * const kActiveProfileIdKey = @"CIYX79ActiveProfileId_v1";
static NSString * const kAutoTriggerEnabledKey = @"CIYX79AutoTriggerEnabled_v1";
static NSString * const kFallbackProfileIdKey = @"CIYX79FallbackProfileId_v1";
static NSString * const kBatteryMirrorEnabledKey = @"CIYX79BatteryMirrorEnabled_v1";
static NSString * const kBatterySourceKey = @"CIYX79BatterySource_v1";
static NSString * const kBatteryTargetKey = @"CIYX79BatteryTarget_v1";
static NSString * const kBatteryCustomSlotsKey = @"CIYX79BatteryCustomSlots_v1";

static const NSUInteger kMaxProfilesCount = 64;
static const NSUInteger kMaxAppMetadataLength = 256;

static NSArray<NSNumber *> *SlotsForZone(NSInteger tag) {
    switch (tag) {
        case 0: // WASD
            return @[@14, @9, @15, @21];
        case 1: // Pfeile
            return @[@88, @83, @89, @95];
        case 2: // Zahlenreihe 1-0
            return @[@7, @13, @19, @25, @31, @37, @43, @49, @55, @61];
        case 3: // Modifikatoren
            return @[@2, @3, @4, @82, @5, @11, @17, @53, @77, @80];
        case 4: // Alpha (A-Z)
            return @[@14, @9, @15, @21, @8, @20, @26, @32, @38, @44, @50, @56, @62,
                     @27, @33, @39, @45, @51, @57, @10, @16, @22, @28, @34, @40, @46];
        case 5: // Nav-Spalte (Entf, Bild Auf, Bild Ab, Ende)
            return @[@86, @87, @94, @91];
        default:
            return @[];
    }
}

static NSString *HexFromColor(NSColor *color) {
    if (!color) return @"#000000";
    NSColor *srgb = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!srgb) return @"#000000"; // Sicherer Fallback statt Crash bei inkompatiblem Farbraum
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

static NSString *TruncateProfileName(NSString *name, NSUInteger maxUnits) {
    if (name.length <= maxUnits) return name;
    NSRange r = [name rangeOfComposedCharacterSequencesForRange:NSMakeRange(0, maxUnits)];
    NSString *s = [name substringWithRange:r];
    while (s.length > maxUnits) {
        NSRange last = [s rangeOfComposedCharacterSequenceAtIndex:s.length - 1];
        if (last.location == 0) break; // würde die einzige Sequenz entfernen – leer vermeiden
        s = [s substringToIndex:last.location];
    }
    return s;
}

static NSString *SanitizeAppMetadata(id raw) {
    if (![raw isKindOfClass:[NSString class]]) return nil;
    NSString *str = (NSString *)raw;
    NSString *cleaned = [[str componentsSeparatedByCharactersInSet:[NSCharacterSet controlCharacterSet]] componentsJoinedByString:@""];
    NSString *trimmed = [cleaned stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) return nil;
    if (trimmed.length > kMaxAppMetadataLength) {
        trimmed = TruncateProfileName(trimmed, kMaxAppMetadataLength);
    }
    return trimmed.length > 0 ? trimmed : nil;
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
    NSRect frame = NSMakeRect(100, 100, 940, 660);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled |
                                                         NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable |
                                                         NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];

    self.window.title = @"CIY X79 RGB Tastatur-Steuerung (macOS)";
    self.window.minSize = NSMakeSize(940, 600);
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

    // Mit Tastatur asynchron im Hintergrund verbinden und aktives Profil übertragen (blockiert nicht den UI-Start)
    [self runHardware:^BOOL{
        return [[KeyboardBackend shared] connect];
    } completion:^(BOOL ok) {
        [self updateConnectionStatus];
        if (ok) {
            [self sendCurrentProfileToHardware];
        }
    }];

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
    // Akku-Überwachung einrichten (v1.4)
    __weak typeof(self) weakSelf = self;
    [CIYBatteryMonitor sharedMonitor].updateHandler = ^(CIYBatteryInfo info) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf handleBatteryUpdate:info];
        });
    };

    BOOL batteryMirrorOn = [[NSUserDefaults standardUserDefaults] boolForKey:kBatteryMirrorEnabledKey];
    NSInteger savedSource = [[NSUserDefaults standardUserDefaults] integerForKey:kBatterySourceKey];
    [_batterySourcePopUp selectItemWithTag:savedSource];
    [CIYBatteryMonitor sharedMonitor].preferredSource = (CIYBatterySourceType)savedSource;

    NSInteger savedTarget = [[NSUserDefaults standardUserDefaults] integerForKey:kBatteryTargetKey];
    [_batteryTargetPopUp selectItemWithTag:savedTarget];
    _customBatterySlots = [[NSUserDefaults standardUserDefaults] stringForKey:kBatteryCustomSlotsKey] ?: @"14,9,15,21";

    if (batteryMirrorOn) {
        _batteryMirrorCheckbox.state = NSControlStateValueOn;
        [[CIYBatteryMonitor sharedMonitor] startMonitoring];
        [self updateBatteryDisplay];
    }

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
    _selectedKeyLabel.frame = NSMakeRect(16, kbBoxH - 46, 168, 22);
    _selectedKeyLabel.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_selectedKeyLabel];

    NSTextField *lblColorPick = [self labelWithText:@"Farbe:" fontSize:12 weight:NSFontWeightMedium];
    lblColorPick.frame = NSMakeRect(188, kbBoxH - 46, 40, 22);
    lblColorPick.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:lblColorPick];

    NSArray<NSDictionary *> *colors = KeyPalette();
    CGFloat btnX = 230;
    for (NSDictionary *cDict in colors) {
        NSButton *b = [NSButton buttonWithTitle:cDict[@"name"] target:self action:@selector(onKeyColorChosen:)];
        b.frame = NSMakeRect(btnX, kbBoxH - 48, 38, 24);
        b.bezelStyle = NSBezelStyleRounded;
        b.tag = (NSInteger)[colors indexOfObject:cDict];
        b.autoresizingMask = NSViewMinYMargin;
        [kbView addSubview:b];
        btnX += 42;
    }

    // Farbrad / Farbwähler (NSColorWell)
    _colorWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(btnX + 2, kbBoxH - 47, 32, 22)];
    _colorWell.color = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0];
    _colorWell.toolTip = @"Eigene Farbe wählen (macOS Farbrad)...";
    _colorWell.target = self;
    _colorWell.action = @selector(onColorWellChanged:);
    _colorWell.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_colorWell];

    NSButton *fillAllBtn = [NSButton buttonWithTitle:@"Alle..." target:self action:@selector(onFillAllKeys:)];
    fillAllBtn.toolTip = @"Alle 69 Tasten im Editor mit einer Basisfarbe füllen";
    fillAllBtn.frame = NSMakeRect(btnX + 38, kbBoxH - 48, 52, 24);
    fillAllBtn.bezelStyle = NSBezelStyleRounded;
    fillAllBtn.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:fillAllBtn];

    // Zonen-Auswahl (v1.4)
    _zonePopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(btnX + 94, kbBoxH - 48, 76, 24) pullsDown:YES];
    [_zonePopUp addItemWithTitle:@"Zone ▾"];
    NSMenuItem *wasdItem = [[NSMenuItem alloc] initWithTitle:@"WASD" action:@selector(onApplyZone:) keyEquivalent:@""];
    wasdItem.target = self;
    wasdItem.tag = 0;
    [_zonePopUp.menu addItem:wasdItem];

    NSMenuItem *arrowsItem = [[NSMenuItem alloc] initWithTitle:@"Pfeiltasten" action:@selector(onApplyZone:) keyEquivalent:@""];
    arrowsItem.target = self;
    arrowsItem.tag = 1;
    [_zonePopUp.menu addItem:arrowsItem];

    NSMenuItem *numItem = [[NSMenuItem alloc] initWithTitle:@"Zahlen 1–0" action:@selector(onApplyZone:) keyEquivalent:@""];
    numItem.target = self;
    numItem.tag = 2;
    [_zonePopUp.menu addItem:numItem];

    NSMenuItem *modItem = [[NSMenuItem alloc] initWithTitle:@"Modifikatoren" action:@selector(onApplyZone:) keyEquivalent:@""];
    modItem.target = self;
    modItem.tag = 3;
    [_zonePopUp.menu addItem:modItem];

    NSMenuItem *alphaItem = [[NSMenuItem alloc] initWithTitle:@"Buchstaben (Alpha)" action:@selector(onApplyZone:) keyEquivalent:@""];
    alphaItem.target = self;
    alphaItem.tag = 4;
    [_zonePopUp.menu addItem:alphaItem];

    NSMenuItem *navZoneItem = [[NSMenuItem alloc] initWithTitle:@"Nav-Spalte" action:@selector(onApplyZone:) keyEquivalent:@""];
    navZoneItem.target = self;
    navZoneItem.tag = 5;
    [_zonePopUp.menu addItem:navZoneItem];

    _zonePopUp.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_zonePopUp];

    // Verlauf-Generator (v1.4)
    _gradientBtn = [NSButton buttonWithTitle:@"Verlauf..." target:self action:@selector(onOpenGradientSheet:)];
    _gradientBtn.toolTip = @"2-Farben-Linear-Farbverlauf über U-Koordinaten oder Reihen generieren";
    _gradientBtn.frame = NSMakeRect(btnX + 174, kbBoxH - 48, 72, 24);
    _gradientBtn.bezelStyle = NSBezelStyleRounded;
    _gradientBtn.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_gradientBtn];

    // Helligkeits-Steuerung (Slider 1..5)
    NSTextField *lblBright = [self labelWithText:@"☀️" fontSize:13 weight:NSFontWeightRegular];
    lblBright.frame = NSMakeRect(btnX + 250, kbBoxH - 46, 18, 22);
    lblBright.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:lblBright];

    _brightnessSlider = [[NSSlider alloc] initWithFrame:NSMakeRect(btnX + 270, kbBoxH - 48, 54, 24)];
    _brightnessSlider.minValue = 1;
    _brightnessSlider.maxValue = 5;
    _brightnessSlider.numberOfTickMarks = 5;
    _brightnessSlider.allowsTickMarkValuesOnly = YES;
    _brightnessSlider.integerValue = 5;
    _brightnessSlider.continuous = NO; // Verhindert mehrfaches Queueing beim Ziehen
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

    // Fußzeile mit Akku-Spiegelung (v1.4)
    NSTextField *footer = [self labelWithText:@"CIY X79 Controller • Profil-Zentrierte IOKit USB Ansteuerung" fontSize:11 weight:NSFontWeightRegular];
    footer.textColor = [NSColor tertiaryLabelColor];
    footer.frame = NSMakeRect(24, 16, 320, 18);
    [container addSubview:footer];

    CGFloat rightEdge = container.bounds.size.width - 24;

    _batteryStatusLabel = [self labelWithText:@"" fontSize:11 weight:NSFontWeightMedium];
    _batteryStatusLabel.frame = NSMakeRect(rightEdge - 90, 15, 90, 18);
    _batteryStatusLabel.textColor = [NSColor secondaryLabelColor];
    _batteryStatusLabel.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [container addSubview:_batteryStatusLabel];

    _batteryTargetPopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(rightEdge - 90 - 118, 12, 114, 24) pullsDown:NO];
    [_batteryTargetPopUp addItemWithTitle:@"Zahlen 1–0"];
    _batteryTargetPopUp.lastItem.tag = 0;
    [_batteryTargetPopUp addItemWithTitle:@"Leertaste"];
    _batteryTargetPopUp.lastItem.tag = 1;
    [_batteryTargetPopUp addItemWithTitle:@"Nav-Spalte"];
    _batteryTargetPopUp.lastItem.tag = 2;
    [_batteryTargetPopUp addItemWithTitle:@"Eigene..."];
    _batteryTargetPopUp.lastItem.tag = 3;
    _batteryTargetPopUp.target = self;
    _batteryTargetPopUp.action = @selector(onBatteryTargetChanged:);
    _batteryTargetPopUp.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [container addSubview:_batteryTargetPopUp];

    _batterySourcePopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(rightEdge - 90 - 118 - 124, 12, 120, 24) pullsDown:NO];
    [_batterySourcePopUp addItemWithTitle:@"Intern (Mac)"];
    _batterySourcePopUp.lastItem.tag = 0;
    [_batterySourcePopUp addItemWithTitle:@"Peripherie (BT)"];
    _batterySourcePopUp.lastItem.tag = 1;
    [_batterySourcePopUp addItemWithTitle:@"USV"];
    _batterySourcePopUp.lastItem.tag = 2;
    [_batterySourcePopUp addItemWithTitle:@"Alle Quellen"];
    _batterySourcePopUp.lastItem.tag = 3;
    _batterySourcePopUp.target = self;
    _batterySourcePopUp.action = @selector(onBatterySourceChanged:);
    _batterySourcePopUp.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [container addSubview:_batterySourcePopUp];

    _batteryMirrorCheckbox = [NSButton checkboxWithTitle:@"⚡ Akku spiegeln" target:self action:@selector(onToggleBatteryMirror:)];
    _batteryMirrorCheckbox.frame = NSMakeRect(rightEdge - 90 - 118 - 124 - 120, 14, 116, 20);
    _batteryMirrorCheckbox.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    _batteryMirrorCheckbox.toolTip = @"Spiegelt den Ladestand flüchtig auf die CIY X79 (ohne das Profil zu überschreiben)";
    [container addSubview:_batteryMirrorCheckbox];
}

#pragma mark - Profilverwaltung

- (void)loadProfiles {
    NSArray *saved = [[NSUserDefaults standardUserDefaults] arrayForKey:kCustomProfilesKey];
    if (saved && [saved isKindOfClass:[NSArray class]] && saved.count > 0) {
        _profiles = [NSMutableArray arrayWithCapacity:MIN(saved.count, kMaxProfilesCount)];
        NSMutableSet<NSString *> *seenIds = [NSMutableSet set];
        NSMutableSet<NSString *> *seenBundleIds = [NSMutableSet set];
        for (id item in saved) {
            if (_profiles.count >= kMaxProfilesCount) break;
            if (![item isKindOfClass:[NSDictionary class]]) continue;
            NSDictionary *dict = (NSDictionary *)item;
            if (![dict[@"slots"] isKindOfClass:[NSDictionary class]]) continue;

            NSMutableDictionary *prof = [dict mutableCopy];
            prof[@"slots"] = [dict[@"slots"] mutableCopy];

            NSString *profId = prof[@"id"];
            if (![profId isKindOfClass:[NSString class]] || profId.length == 0 || [seenIds containsObject:profId]) {
                profId = [[NSUUID UUID] UUIDString];
                prof[@"id"] = profId;
            }
            [seenIds addObject:profId];

            NSString *name = prof[@"name"];
            if ([name isKindOfClass:[NSString class]]) {
                NSString *trimmed = [name stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                name = (trimmed.length > 0) ? TruncateProfileName(trimmed, 40) : @"Unbenannt";
            } else {
                name = @"Unbenannt";
            }
            prof[@"name"] = name;

            NSString *bundleId = SanitizeAppMetadata(prof[@"appBundleId"]);
            if (bundleId.length > 0 && ![seenBundleIds containsObject:bundleId]) {
                [seenBundleIds addObject:bundleId];
                prof[@"appBundleId"] = bundleId;
                NSString *appName = SanitizeAppMetadata(prof[@"appName"]);
                if (appName.length > 0) {
                    prof[@"appName"] = appName;
                } else {
                    [prof removeObjectForKey:@"appName"];
                }
            } else {
                [prof removeObjectForKey:@"appBundleId"];
                [prof removeObjectForKey:@"appName"];
            }

            [_profiles addObject:prof];
        }
    } else {
        _profiles = [NSMutableArray array];
        [self addDefaultProfiles];
        [self saveProfilesToDefaults];
    }

    if (_profiles.count == 0) {
        // Defekte/leere gespeicherte Liste: auf die eingebauten Profile zurückfallen.
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
            NSString *rawAppName = [panel.URL.lastPathComponent stringByDeletingPathExtension];
            NSString *appName = SanitizeAppMetadata(rawAppName) ?: @"App";
            NSString *bundleId = SanitizeAppMetadata(appBundle.bundleIdentifier);
            if (!bundleId || bundleId.length == 0) {
                bundleId = SanitizeAppMetadata([NSString stringWithFormat:@"local.%@", appName]) ?: @"local.App";
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

    // Ungespeicherte Bearbeitungen des Benutzers im Editor niemals automatisch überschreiben!
    if (_hasUnsavedEdits) {
        _statusLabel.stringValue = [NSString stringWithFormat:@"● Auto-Trigger pausiert: Profil „%@“ hat ungespeicherte Änderungen", [self currentProfileName]];
        _statusLabel.textColor = [NSColor systemOrangeColor];
        return;
    }

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
        // Strikt gegen Out-of-Bounds absichern: Slot muss eine Ganzzahl >= 0 und < (CIY_LED_COUNT - 1) sein (überlaufsicher ohne Addition)
        if ([scanner scanInt:&slot] && scanner.isAtEnd && slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
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
    if (_profiles.count >= kMaxProfilesCount) {
        NSAlert *limitAlert = [[NSAlert alloc] init];
        limitAlert.messageText = @"Profillimit erreicht";
        limitAlert.informativeText = [NSString stringWithFormat:@"Es können maximal %lu Profile gespeichert werden. Bitte löschen Sie zuerst ein nicht benötigtes Profil.", (unsigned long)kMaxProfilesCount];
        [limitAlert addButtonWithTitle:@"OK"];
        [limitAlert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

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
            if (self->_profiles.count >= kMaxProfilesCount) return;
            NSString *name = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (name.length == 0) {
                name = [NSString stringWithFormat:@"Profil %lu", (unsigned long)(self->_profiles.count + 1)];
            } else if (name.length > 40) {
                name = TruncateProfileName(name, 40);
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
                if (name.length > 40) name = TruncateProfileName(name, 40);
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

            NSUInteger nextIdx = (idx > 0) ? (NSUInteger)(idx - 1) : 0;
            self->_currentActiveProfileId = self->_profiles[nextIdx][@"id"];
            if ([self->_fallbackProfileId isEqualToString:deletedId]) {
                self->_fallbackProfileId = self->_profiles[nextIdx][@"id"];
                [[NSUserDefaults standardUserDefaults] setObject:self->_fallbackProfileId forKey:kFallbackProfileIdKey];
            }

            [self saveProfilesToDefaults]; // Erst speichern, nachdem activeProfileId aktualisiert wurde!

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

#pragma mark - Zonen- & Farbverlauf-Aktionen (v1.4)

- (void)onApplyZone:(NSMenuItem *)sender {
    NSArray<NSNumber *> *slots = SlotsForZone(sender.tag);
    if (slots.count == 0) return;
    NSColor *color = _colorWell.color;
    [_keyboardView applyColor:color toSlots:slots];
    [self setHasUnsavedEdits:YES];
    [self updateButtonsState];

    NSString *profName = [self currentProfileName];
    _statusLabel.stringValue = [NSString stringWithFormat:@"● Zone „%@“ eingefärbt (Profil „%@“ ungespeichert)", sender.title, profName];
    _statusLabel.textColor = [NSColor systemOrangeColor];
}

- (void)onOpenGradientSheet:(id)sender {
    (void)sender;
    _gradientSheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 380, 220)
                                                 styleMask:(NSWindowStyleMaskTitled)
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    _gradientSheet.title = @"Farbverlauf-Generator (Linear Gradient)";

    NSView *content = _gradientSheet.contentView;

    NSTextField *lblStart = [self labelWithText:@"Startfarbe:" fontSize:12 weight:NSFontWeightMedium];
    lblStart.frame = NSMakeRect(24, 162, 90, 20);
    [content addSubview:lblStart];

    _gradStartWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(120, 160, 48, 26)];
    _gradStartWell.color = [NSColor colorWithSRGBRed:0.0 green:0.9 blue:1.0 alpha:1.0]; // Cyan
    [content addSubview:_gradStartWell];

    NSTextField *lblEnd = [self labelWithText:@"Endfarbe:" fontSize:12 weight:NSFontWeightMedium];
    lblEnd.frame = NSMakeRect(24, 122, 90, 20);
    [content addSubview:lblEnd];

    _gradEndWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(120, 120, 48, 26)];
    _gradEndWell.color = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.8 alpha:1.0]; // Magenta
    [content addSubview:_gradEndWell];

    NSTextField *lblDir = [self labelWithText:@"Richtung:" fontSize:12 weight:NSFontWeightMedium];
    lblDir.frame = NSMakeRect(24, 82, 90, 20);
    [content addSubview:lblDir];

    _gradDirPopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(120, 80, 230, 26) pullsDown:NO];
    [_gradDirPopUp addItemWithTitle:@"Horizontal (Links → Rechts)"];
    [_gradDirPopUp addItemWithTitle:@"Vertikal (Oben → Unten)"];
    [content addSubview:_gradDirPopUp];

    NSButton *applyBtn = [NSButton buttonWithTitle:@"Auf Tastatur anwenden" target:self action:@selector(onApplyGradient:)];
    applyBtn.frame = NSMakeRect(190, 20, 166, 32);
    applyBtn.bezelStyle = NSBezelStyleRounded;
    applyBtn.keyEquivalent = @"\r";
    [content addSubview:applyBtn];

    NSButton *copyBtn = [NSButton buttonWithTitle:@"ciy_ctl kopieren" target:self action:@selector(onCopyGradientCommand:)];
    copyBtn.frame = NSMakeRect(80, 20, 106, 32);
    copyBtn.bezelStyle = NSBezelStyleRounded;
    [content addSubview:copyBtn];

    NSButton *cancelBtn = [NSButton buttonWithTitle:@"Abbrechen" target:self action:@selector(onCancelGradientSheet:)];
    cancelBtn.frame = NSMakeRect(16, 20, 60, 32);
    cancelBtn.bezelStyle = NSBezelStyleRounded;
    [content addSubview:cancelBtn];

    [self.window beginSheet:_gradientSheet completionHandler:nil];
}

- (void)onApplyGradient:(id)sender {
    (void)sender;
    NSColor *c1 = _gradStartWell.color;
    NSColor *c2 = _gradEndWell.color;
    CIYGradientDirection dir = (_gradDirPopUp.indexOfSelectedItem == 1) ? CIY_GRADIENT_VERTICAL : CIY_GRADIENT_HORIZONTAL;

    NSColor *srgb1 = [c1 colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] ?: c1;
    NSColor *srgb2 = [c2 colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] ?: c2;

    CGFloat r1, g1, b1, a1;
    CGFloat r2, g2, b2, a2;
    [srgb1 getRed:&r1 green:&g1 blue:&b1 alpha:&a1];
    [srgb2 getRed:&r2 green:&g2 blue:&b2 alpha:&a2];

    uint8_t ur1 = (uint8_t)(r1 * 255.0 + 0.5);
    uint8_t ug1 = (uint8_t)(g1 * 255.0 + 0.5);
    uint8_t ub1 = (uint8_t)(b1 * 255.0 + 0.5);

    uint8_t ur2 = (uint8_t)(r2 * 255.0 + 0.5);
    uint8_t ug2 = (uint8_t)(g2 * 255.0 + 0.5);
    uint8_t ub2 = (uint8_t)(b2 * 255.0 + 0.5);

    const CIYKeyboardLayout *layout = ciy_layout_x79_iso_de();
    NSMutableDictionary<NSNumber *, NSColor *> *newColors = [NSMutableDictionary dictionaryWithCapacity:layout->key_count];

    for (size_t i = 0; i < layout->key_count; i++) {
        int slot = layout->keys[i].matrix_slot;
        uint8_t r = 0, g = 0, b = 0;
        if (ciy_gradient_calculate_slot_color(slot, ur1, ug1, ub1, ur2, ug2, ub2, dir, &r, &g, &b)) {
            newColors[@(slot)] = [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0];
        }
    }

    [_keyboardView applyKeyColors:newColors];
    [self setHasUnsavedEdits:YES];
    [self updateButtonsState];
    [self sendCurrentProfileToHardware];

    if (_gradientSheet) {
        [self.window endSheet:_gradientSheet];
        _gradientSheet = nil;
    }
}

- (void)onCopyGradientCommand:(id)sender {
    (void)sender;
    NSString *hex1 = HexFromColor(_gradStartWell.color);
    NSString *hex2 = HexFromColor(_gradEndWell.color);
    NSString *dirStr = (_gradDirPopUp.indexOfSelectedItem == 1) ? @"vertical" : @"horizontal";
    NSString *cmdStr = [NSString stringWithFormat:@"ciy_ctl gradient %@ %@ --dir %@ -b 5\n", hex1, hex2, dirStr];
    [[NSPasteboard generalPasteboard] clearContents];
    [[NSPasteboard generalPasteboard] setString:cmdStr forType:NSPasteboardTypeString];
    _statusLabel.stringValue = @"● Verlauf-Befehl in Zwischenablage kopiert";
    _statusLabel.textColor = [NSColor systemCyanColor];
}

- (void)onCancelGradientSheet:(id)sender {
    (void)sender;
    if (_gradientSheet) {
        [self.window endSheet:_gradientSheet];
        _gradientSheet = nil;
    }
}

#pragma mark - Akku-Spiegelung (v1.4)

- (void)onToggleBatteryMirror:(NSButton *)sender {
    BOOL enabled = (sender.state == NSControlStateValueOn);
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:kBatteryMirrorEnabledKey];

    if (enabled) {
        [[CIYBatteryMonitor sharedMonitor] startMonitoring];
        [self updateBatteryDisplay];
    } else {
        [[CIYBatteryMonitor sharedMonitor] stopMonitoring];
        _batteryStatusLabel.stringValue = @"";
        [_keyboardView clearOverlayColors];
        [self sendCurrentProfileToHardware];
    }
}

- (void)onBatterySourceChanged:(NSPopUpButton *)sender {
    [[NSUserDefaults standardUserDefaults] setInteger:sender.selectedItem.tag forKey:kBatterySourceKey];
    [CIYBatteryMonitor sharedMonitor].preferredSource = (CIYBatterySourceType)sender.selectedItem.tag;
    if (_batteryMirrorCheckbox.state == NSControlStateValueOn) {
        [self updateBatteryDisplay];
    }
}

- (void)onBatteryTargetChanged:(NSPopUpButton *)sender {
    if (sender.selectedItem.tag == 3) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Benutzerdefinierte Akku-Tastenfolge";
        alert.informativeText = @"Geben Sie die Matrix-Slots kommagetrennt ein (z. B. 14,9,15,21 für WASD oder 88,83,89,95 für Pfeile):";
        NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
        input.stringValue = _customBatterySlots ?: @"14,9,15,21";
        alert.accessoryView = input;
        [alert addButtonWithTitle:@"Übernehmen"];
        [alert addButtonWithTitle:@"Abbrechen"];
        if ([alert runModal] == NSAlertFirstButtonReturn) {
            NSString *val = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (val.length > 0) {
                _customBatterySlots = val;
                [[NSUserDefaults standardUserDefaults] setObject:_customBatterySlots forKey:kBatteryCustomSlotsKey];
            }
        }
    }
    [[NSUserDefaults standardUserDefaults] setInteger:sender.selectedItem.tag forKey:kBatteryTargetKey];
    if (_batteryMirrorCheckbox.state == NSControlStateValueOn) {
        [self updateBatteryDisplay];
    }
}

- (void)updateBatteryDisplay {
    CIYBatteryInfo info = [[CIYBatteryMonitor sharedMonitor] currentInfo];
    [self handleBatteryUpdate:info];
}

- (void)handleBatteryUpdate:(CIYBatteryInfo)info {
    if (_batteryMirrorCheckbox.state != NSControlStateValueOn) return;

    if (info.percent < 0) {
        _batteryStatusLabel.stringValue = @"N/A";
        [_keyboardView clearOverlayColors];
        [self sendCurrentProfileToHardware];
        return;
    }

    _batteryStatusLabel.stringValue = [NSString stringWithFormat:@"%d%%%@",
                                       info.percent, info.is_charging ? @" ⚡" : @""];

    int targetSlots[64];
    size_t targetCount = 0;
    NSInteger targetIndex = _batteryTargetPopUp.selectedItem.tag;
    const char *targetStr = "1-0";
    if (targetIndex == 1) {
        targetStr = "space";
    } else if (targetIndex == 2) {
        targetStr = "nav";
    } else if (targetIndex == 3) {
        targetStr = [_customBatterySlots UTF8String] ?: "14,9,15,21";
    }
    ciy_battery_parse_target(targetStr, targetSlots, 64, &targetCount);

    uint8_t rgb[CIY_PLANAR_RGB_SIZE];
    ciy_battery_calculate_overlay(NULL, info.percent, info.is_charging, targetSlots, targetCount, false, rgb);

    NSMutableDictionary<NSNumber *, NSColor *> *overlay = [NSMutableDictionary dictionaryWithCapacity:targetCount];
    for (size_t i = 0; i < targetCount; i++) {
        int slot = targetSlots[i];
        int led = slot + 1;
        uint8_t r = rgb[led];
        uint8_t g = rgb[CIY_LED_COUNT + led];
        uint8_t b = rgb[2 * CIY_LED_COUNT + led];
        overlay[@(slot)] = [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0];
    }

    [_keyboardView setOverlayColors:overlay];
    [self sendCurrentProfileToHardware];
}

#pragma mark - USB-Hardware Transfer (Reines Profil-Senden)

- (void)sendCurrentProfileToHardware {
    if (![KeyboardBackend shared].isConnected) {
        [self updateConnectionStatus];
        return;
    }
    if (_sendInFlight) {
        _sendDirty = YES;
        return;
    }
    _sendInFlight = YES;
    _sendDirty = NO;

    // Liest den aktuellen Zustand der Tasten aus der Canvas-Ansicht aus
    NSDictionary<NSNumber *, NSColor *> *allColors = [_keyboardView allKeyColors];
    NSMutableDictionary<NSNumber *, NSColor *> *snapshot = [allColors mutableCopy];
    NSDictionary<NSNumber *, NSColor *> *overlay = [_keyboardView overlayColors];
    if (overlay && _batteryMirrorCheckbox.state == NSControlStateValueOn) {
        [snapshot addEntriesFromDictionary:overlay];
    }
    uint8_t br = (_currentBrightness >= 1 && _currentBrightness <= 5) ? _currentBrightness : 5;
    CFAbsoluteTime delay = MAX(0.0, (_lastSendTime + 0.12) - CFAbsoluteTimeGetCurrent());

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)), _hwQueue, ^{
        BOOL ok = [[KeyboardBackend shared] sendKeyColors:snapshot brightness:br defaultColor:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_lastSendTime = CFAbsoluteTimeGetCurrent();
            self->_sendInFlight = NO;
            [self reportResult:ok];
            if (ok) {
                NSString *profName = [self currentProfileName];
                if (self->_hasUnsavedEdits) {
                    self->_statusLabel.stringValue = [NSString stringWithFormat:@"● Vorschau „%@“ (ungespeichert) auf Tastatur", profName];
                    self->_statusLabel.textColor = [NSColor systemOrangeColor];
                } else {
                    self->_statusLabel.stringValue = [NSString stringWithFormat:@"● Profil „%@“ aktiv auf Tastatur", profName];
                    self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
                }
            } else if (![KeyboardBackend shared].lastOperationWasCancelled) {
                self->_statusLabel.stringValue = @"● Senden fehlgeschlagen (USB prüfen)";
                self->_statusLabel.textColor = [NSColor systemRedColor];
            }
            if (self->_sendDirty && [KeyboardBackend shared].isConnected) {
                [self sendCurrentProfileToHardware];
            }
        });
    });
}

- (void)cancelHardwareSync {
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(performHardwareSync)
                                               object:nil];
    _wakeSyncScheduled = NO;
}

- (void)onHotplug:(NSNotification *)note {
    BOOL connected = [note.userInfo[@"connected"] boolValue];
    if (!connected) {
        [self cancelHardwareSync];
        _isWakingUp = NO;
        [[KeyboardBackend shared] requestCancel];
        [self updateConnectionStatus];
        dispatch_async(_hwQueue, ^{
            [[KeyboardBackend shared] disconnect];
        });
        return;
    }
    if (_isWakingUp) {
        if (_wakeSyncScheduled) {
            // Während des Aufwachvorgangs übernimmt der verzögerte Timer die Synchronisation.
            return;
        }
        // Kein Timer mehr ausstehend (z. B. durch Disconnect abgebrochen): normal weiter.
        _isWakingUp = NO;
    }
    // Normaler Hotplug im laufenden Betrieb: 100 ms Debouncing gegen Kontaktprellen
    [self cancelHardwareSync];
    _wakeRetryCount = 0;
    [self performSelector:@selector(performHardwareSync) withObject:nil afterDelay:0.1];
}

- (void)onSystemWillSleep:(NSNotification *)note {
    (void)note;
    _isWakingUp = YES;
    // Anstehende Auto-Switches und Hardware-Syncs stornieren, damit während des Suspend-Vorgangs keine Transfers laufen
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
    [self cancelHardwareSync];
    // Akku-Überwachung während Ruhezustand pausieren
    [[CIYBatteryMonitor sharedMonitor] stopMonitoring];
    // Verbindung vorübergehend trennen, um verwaiste USB-Handles nach dem Aufwachen zu verhindern
    [[KeyboardBackend shared] requestCancel];
    if (_hwQueue) {
        dispatch_sync(_hwQueue, ^{
            [[KeyboardBackend shared] disconnect];
        });
    } else {
        [[KeyboardBackend shared] disconnect];
    }
    [self updateConnectionStatus];
}

- (void)onSystemDidWake:(NSNotification *)note {
    (void)note;
    // 500 ms Verzögerung, damit macOS USB-Root-Hub und MCU nach dem Aufwachen vollständig initialisiert sind.
    // Konsolidiert Aufwach- und Hotplug-Events zu einem einzigen Reconnect.
    [self cancelHardwareSync];
    _wakeRetryCount = 0;
    _wakeSyncScheduled = YES;
    [self performSelector:@selector(performHardwareSync) withObject:nil afterDelay:0.5];
}

- (void)performHardwareSync {
    _isWakingUp = NO;
    _wakeSyncScheduled = NO;
    [self runHardware:^BOOL{
        [[KeyboardBackend shared] disconnect];
        return [[KeyboardBackend shared] connect];
    } completion:^(BOOL ok) {
        [self updateConnectionStatus];
        if (ok) {
            self->_wakeRetryCount = 0;
            // Tastatur mit dem aktuell aktiven Profil synchronisieren
            [self sendCurrentProfileToHardware];
            if (self->_batteryMirrorCheckbox.state == NSControlStateValueOn) {
                [[CIYBatteryMonitor sharedMonitor] startMonitoring];
                [self updateBatteryDisplay];
            }
        } else if (self->_wakeRetryCount < 3) {
            self->_wakeRetryCount++;
            NSTimeInterval delay = 0.5 * (1 << (self->_wakeRetryCount - 1));
            [self cancelHardwareSync];
            self->_wakeSyncScheduled = YES;
            [self performSelector:@selector(performHardwareSync) withObject:nil afterDelay:delay];
        }
    }];
}

- (void)onReconnect:(id)sender {
    (void)sender;
    [self cancelHardwareSync];
    _wakeRetryCount = 0;
    [self performHardwareSync];
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

    NSButton *includeAppsCheckbox = [NSButton checkboxWithTitle:@"App-Verknüpfungen (Auto-Trigger) mitexportieren" target:nil action:nil];
    includeAppsCheckbox.state = NSControlStateValueOff;
    [includeAppsCheckbox sizeToFit];
    NSRect cbFrame = includeAppsCheckbox.frame;
    NSView *accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, cbFrame.size.width + 24, cbFrame.size.height + 16)];
    includeAppsCheckbox.frame = NSMakeRect(12, 8, cbFrame.size.width, cbFrame.size.height);
    [accessory addSubview:includeAppsCheckbox];
    panel.accessoryView = accessory;

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            BOOL includeAppMetadata = (includeAppsCheckbox.state == NSControlStateValueOn);
            NSMutableArray<NSDictionary *> *exportList = [NSMutableArray arrayWithCapacity:self->_profiles.count];
            for (NSDictionary *prof in self->_profiles) {
                NSMutableDictionary *copyProf = [prof mutableCopy];
                if ([prof[@"slots"] isKindOfClass:[NSDictionary class]]) {
                    copyProf[@"slots"] = [prof[@"slots"] mutableCopy];
                }
                if (!includeAppMetadata) {
                    [copyProf removeObjectForKey:@"appBundleId"];
                    [copyProf removeObjectForKey:@"appName"];
                }
                [exportList addObject:copyProf];
            }

            NSError *err = nil;
            NSData *jsonData = [NSJSONSerialization dataWithJSONObject:exportList
                                                               options:NSJSONWritingPrettyPrinted
                                                                 error:&err];
            if (jsonData && !err) {
                BOOL ok = [jsonData writeToURL:panel.URL atomically:YES];
                if (ok) {
                    self->_statusLabel.stringValue = [NSString stringWithFormat:@"● %lu Profil(e) exportiert", (unsigned long)exportList.count];
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
    if (_profiles.count >= kMaxProfilesCount) {
        [self showImportError:[NSString stringWithFormat:@"Das Limit von maximal %lu Profilen ist bereits erreicht. Bitte löschen Sie zuerst nicht benötigte Profile.", (unsigned long)kMaxProfilesCount]];
        return;
    }

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"CIY X79 Beleuchtungsprofile importieren";
    panel.prompt = @"Importieren";
    panel.allowedContentTypes = @[UTTypeJSON];
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            if (self->_profiles.count >= kMaxProfilesCount) {
                [self showImportError:[NSString stringWithFormat:@"Das Limit von maximal %lu Profilen ist bereits erreicht.", (unsigned long)kMaxProfilesCount]];
                return;
            }
            size_t rawSize = 0;
            CIYValidationResult valRes;
            char *rawBuf = ciy_read_text_file_with_result([panel.URL.path fileSystemRepresentation], &rawSize, &valRes);
            if (!rawBuf || rawSize == 0 || rawSize > 512 * 1024) {
                if (rawBuf) free(rawBuf);
                NSString *errMsg = (valRes.error_message[0] != '\0')
                    ? [NSString stringWithUTF8String:valRes.error_message]
                    : @"Datei konnte nicht sicher gelesen werden.";
                [self showImportError:errMsg ?: @"Datei konnte nicht sicher gelesen werden."];
                return;
            }
            NSData *data = [NSData dataWithBytesNoCopy:rawBuf length:rawSize freeWhenDone:YES];
            NSError *err = nil;
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
            BOOL limitReached = NO;
            NSString *lastId = nil;
            for (id item in items) {
                if (self->_profiles.count >= kMaxProfilesCount) {
                    limitReached = YES;
                    break;
                }
                if ([item isKindOfClass:[NSDictionary class]]) {
                    NSDictionary *dict = (NSDictionary *)item;
                    NSString *name = @"Importiertes Profil";
                    if ([dict[@"name"] isKindOfClass:[NSString class]]) {
                        NSString *trimmed = [dict[@"name"] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                        if (trimmed.length > 0) {
                            if (trimmed.length > 40) trimmed = TruncateProfileName(trimmed, 40);
                            name = trimmed;
                        }
                    }
                    NSDictionary *slots = dict[@"slots"];
                    if (slots && [slots isKindOfClass:[NSDictionary class]]) {
                        NSMutableDictionary<NSString *, NSString *> *fullSlots = [NSMutableDictionary dictionaryWithCapacity:126];
                        for (int s = 0; s < 126; s++) {
                            fullSlots[@(s).stringValue] = @"#000000";
                        }
                        NSUInteger validSlotCount = 0;
                        for (id slotKey in slots) {
                            NSString *keyStr = [NSString stringWithFormat:@"%@", slotKey];
                            NSScanner *scanner = [NSScanner scannerWithString:keyStr];
                            int slot = -1;
                            // Strikte Validierung: Muss echte Zahl sein und im Bereich 0..124 liegen (überlaufsicher ohne vorzeitige Addition)
                            if ([scanner scanInt:&slot] && scanner.isAtEnd && slot >= 0 && slot < (CIY_LED_COUNT - 1)) {
                                if ([slots[slotKey] isKindOfClass:[NSString class]]) {
                                    NSString *hex = (NSString *)slots[slotKey];
                                    uint8_t tr = 0, tg = 0, tb = 0;
                                    if (ciy_parse_hex_color([hex UTF8String], &tr, &tg, &tb)) {
                                        fullSlots[@(slot).stringValue] = [NSString stringWithFormat:@"#%02X%02X%02X", tr, tg, tb];
                                        validSlotCount++;
                                    }
                                }
                            }
                        }
                        if (validSlotCount == 0) {
                            continue;
                        }
                        NSMutableDictionary *newProf = [NSMutableDictionary dictionary];
                        NSString *newId = [[NSUUID UUID] UUIDString];
                        newProf[@"id"] = newId;
                        newProf[@"name"] = name;
                        newProf[@"slots"] = fullSlots;
                        NSString *importedBundleId = SanitizeAppMetadata(dict[@"appBundleId"]);
                        if (importedBundleId.length > 0) {
                            // Bestehende Verknüpfung bei anderen Profilen lösen, um Konflikte zu vermeiden:
                            for (NSMutableDictionary *existing in self->_profiles) {
                                if ([existing[@"appBundleId"] isEqualToString:importedBundleId]) {
                                    [existing removeObjectForKey:@"appBundleId"];
                                    [existing removeObjectForKey:@"appName"];
                                }
                            }
                            newProf[@"appBundleId"] = importedBundleId;
                            NSString *importedAppName = SanitizeAppMetadata(dict[@"appName"]);
                            if (importedAppName.length > 0) {
                                newProf[@"appName"] = importedAppName;
                            }
                        }
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
                if (limitReached) {
                    [self showImportError:[NSString stringWithFormat:@"Es wurden %lu Profil(e) importiert. Weitere Profile wurden übersprungen, da das Maximum von %lu Profilen erreicht ist.", (unsigned long)importedCount, (unsigned long)kMaxProfilesCount]];
                }
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
    if (!ok && ![KeyboardBackend shared].lastOperationWasCancelled) {
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
    [[CIYBatteryMonitor sharedMonitor] stopMonitoring];
    [[KeyboardBackend shared] requestCancel];
    if (_hwQueue) {
        dispatch_sync(_hwQueue, ^{
            [[KeyboardBackend shared] disconnect];
        });
    } else {
        [[KeyboardBackend shared] disconnect];
    }
}

@end
