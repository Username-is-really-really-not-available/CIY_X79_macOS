#import "AppDelegate.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <stdlib.h>
#import "../core/ciy_battery.h"
#import "../core/ciy_gradient.h"
#import "CIYBatteryMonitor.h"
#import "CIYLocalization.h"
#import "CIYMiniWindowController.h"
#import "CIYProfileScheduler.h"
#import "CIYStatusItemPresenter.h"
#import "CIYProfileTags.h"

@interface AppDelegate () <CIYMiniWindowControllerDelegate, NSMenuItemValidation, CIYProfileSchedulerDelegate> {
    NSTextField *_statusLabel;
    NSTextField *_selectedKeyLabel;
    CIYKeyboardView *_keyboardView;
    int _currentSelectedSlot;
    dispatch_queue_t _hwQueue; // serielle Queue: USB-Zugriffe blockieren nicht die Oberfläche

    // Profilverwaltung & Tags (Roadmap 3.1)
    NSPopUpButton *_profilesPopUp;
    NSButton *_applyProfileBtn;
    NSButton *_saveProfileBtn;
    NSButton *_renameProfileBtn;
    NSButton *_deleteProfileBtn;
    NSPopUpButton *_profileFilterPopUp;
    NSButton *_editTagsBtn;
    NSString *_selectedFilterTag;
    NSArray<NSNumber *> *_visibleProfileIndices;
    BOOL _activeProfilePrependedInPopUp;

    // Tag-Bearbeitung Sheet
    NSWindow *_tagSheet;
    NSString *_tagSheetProfileId;
    NSMutableArray<NSButton *> *_tagSuggestedCheckboxes;
    NSTextField *_tagCustomField;
    NSTextField *_tagErrorLabel;

    // Profil-Zeitsteuerung (Roadmap 2.1)
    CIYProfileScheduler *_scheduler;
    NSMenuItem *_scheduleActiveMenuItem;
    NSWindow *_scheduleSheet;
    NSButton *_schedEnabledCheckbox;
    NSDatePicker *_schedTimePicker;
    NSMutableArray<NSButton *> *_schedDayCheckboxes;
    NSTextField *_schedErrorLabel;
    NSString *_scheduleSheetProfileId;
    NSArray<NSButton *> *_schedQuickButtons;
    BOOL _scheduleSwitchDeferredByEdits;

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
    BOOL _isSleeping;
    BOOL _isWakingUp;
    BOOL _wakeSyncScheduled;
    BOOL _sendInFlight;
    BOOL _sendDirty;
    CFAbsoluteTime _lastSendTime;
    NSUInteger _wakeRetryCount;
    NSStatusItem *_statusItem;
    NSMenu *_quickSwitchMenu;
    CIYMiniWindowController *_miniController;
    BOOL _hasLastSendResult;
    BOOL _lastSendOk;
    NSDate *_lastSendDate;
    NSUserDefaults *_userDefaults;
#if defined(CIY_ENABLE_TEST_HOOKS)
    NSString *_testDefaultsSuiteName;
#endif
}
- (NSUserDefaults *)userDefaults;
- (void)recordSendResult:(BOOL)ok;
- (void)dismissScheduleSheet;
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
static const NSInteger kLanguageMenuTag = 0x4C4E47;

static void CIYUpdateLanguageMenuStates(NSMenu *menu, CIYLanguage preferred) {
    if (!menu) return;
    for (NSMenuItem *item in menu.itemArray) {
        if (item.tag == kLanguageMenuTag && item.submenu) {
            for (NSMenuItem *langItem in item.submenu.itemArray) {
                langItem.state = (langItem.tag == preferred)
                    ? NSControlStateValueOn : NSControlStateValueOff;
            }
        }
        if (item.submenu) {
            CIYUpdateLanguageMenuStates(item.submenu, preferred);
        }
    }
}

static NSString *CIYLocalizedFileReadErrorMessage(CIYFileReadError error) {
    switch (error) {
        case CIYFileReadErrorNullPath:
            return CIYL(@"The file path is invalid.");
        case CIYFileReadErrorOpenFailed:
            return CIYL(@"The selected file could not be opened. It may not exist or you may lack the required permissions.");
        case CIYFileReadErrorSymlinkRejected:
            return CIYL(@"Symbolic links cannot be imported for security reasons.");
        case CIYFileReadErrorNotRegularFile:
            return CIYL(@"The selected path is not a regular file.");
        case CIYFileReadErrorEmptyFile:
            return CIYL(@"The selected file is empty.");
        case CIYFileReadErrorTooLarge:
            return CIYL(@"The selected file exceeds the maximum size of 512 KB.");
        case CIYFileReadErrorStreamFailed:
            return CIYL(@"The file could not be opened for reading.");
        case CIYFileReadErrorOutOfMemory:
            return CIYL(@"Not enough memory to read the file.");
        case CIYFileReadErrorReadFailed:
            return CIYL(@"The file contains invalid binary data or could not be read completely.");
        case CIYFileReadErrorNone:
        default:
            return CIYL(@"The file could not be read safely.");
    }
}

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

@interface CIYMainWindow : NSWindow <NSMenuItemValidation>
@property (nonatomic, weak, nullable) CIYKeyboardView *keyboardView;
@end

@implementation CIYMainWindow

- (nullable NSTextView *)activeFieldEditor {
    id responder = self.firstResponder;
    return [responder isKindOfClass:[NSTextView class]] ? (NSTextView *)responder : nil;
}

- (void)undo:(id)sender {
    NSTextView *editor = [self activeFieldEditor];
    if (editor) {
        [editor.undoManager undo];
        return;
    }
    if (self.keyboardView.canUndo) {
        [self.keyboardView undo:sender];
    }
}

- (void)redo:(id)sender {
    NSTextView *editor = [self activeFieldEditor];
    if (editor) {
        [editor.undoManager redo];
        return;
    }
    if (self.keyboardView.canRedo) {
        [self.keyboardView redo:sender];
    }
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    NSTextView *editor = [self activeFieldEditor];
    if (editor) {
        if (menuItem.action == @selector(undo:)) return editor.undoManager.canUndo;
        if (menuItem.action == @selector(redo:)) return editor.undoManager.canRedo;
        return YES;
    }
    if (menuItem.action == @selector(undo:)) return self.keyboardView.canUndo;
    if (menuItem.action == @selector(redo:)) return self.keyboardView.canRedo;
    return YES;
}

@end

@implementation AppDelegate

- (NSUserDefaults *)userDefaults {
    return _userDefaults ?: [NSUserDefaults standardUserDefaults];
}

/// Gemeinsame Farbpalette für Tastenbuttons (Reihenfolge = Button-Tag).
static NSArray<NSDictionary *> *KeyPalette(void) {
    return @[
        @{@"name": CIYL(@"Red"),    @"c": [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.0 alpha:1.0]},
        @{@"name": CIYL(@"Green"),  @"c": [NSColor colorWithSRGBRed:0.0 green:1.0 blue:0.0 alpha:1.0]},
        @{@"name": CIYL(@"Blue"),   @"c": [NSColor colorWithSRGBRed:0.0 green:0.4 blue:1.0 alpha:1.0]},
        @{@"name": CIYL(@"Yellow"), @"c": [NSColor colorWithSRGBRed:1.0 green:0.9 blue:0.0 alpha:1.0]},
        @{@"name": CIYL(@"Purple"), @"c": [NSColor colorWithSRGBRed:0.9 green:0.0 blue:0.9 alpha:1.0]},
        @{@"name": CIYL(@"Cyan"),   @"c": [NSColor colorWithSRGBRed:0.0 green:1.0 blue:1.0 alpha:1.0]},
        @{@"name": CIYL(@"White"),  @"c": [NSColor colorWithSRGBRed:1.0 green:1.0 blue:1.0 alpha:1.0]},
        @{@"name": CIYL(@"Off"),    @"c": [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0]}
    ];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    (void)aNotification;
    _hwQueue = dispatch_queue_create("com.julien.ciy-x79-control.hardware", DISPATCH_QUEUE_SERIAL);
    _currentSelectedSlot = 14; // Standard-Auswahl: Taste W
    _currentBrightness = 5;    // Standard: Maximale Helligkeit (1..5)
    _hasUnsavedEdits = NO;

    [self loadLanguagePreference];
    [self setupMainMenu];

    // Hauptfenster einrichten
    NSRect frame = NSMakeRect(100, 100, 940, 660);
    self.window = [[CIYMainWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled |
                                                         NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable |
                                                         NSWindowStyleMaskResizable)
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];

    self.window.title = CIYL(@"CIY X79 RGB Keyboard Control (macOS)");
    self.window.minSize = NSMakeSize(940, 600);
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    self.window.releasedWhenClosed = NO;
    [self.window center];

    NSView *content = self.window.contentView;
    content.wantsLayer = YES;
    content.layer.backgroundColor = [NSColor colorWithCalibratedWhite:0.11 alpha:1.0].CGColor;

    [self buildUI:content];
    ((CIYMainWindow *)self.window).keyboardView = _keyboardView;
    [self loadProfiles];

    // Profil-Zeitsteuerung initialisieren (Roadmap 2.1)
    _scheduler = [[CIYProfileScheduler alloc] init];
    _scheduler.delegate = self;
    __weak typeof(self) weakSelf = self;
    _scheduler.profilesProvider = ^NSArray<NSDictionary *> *{
        __strong typeof(weakSelf) strongSelf = weakSelf;
        return strongSelf ? strongSelf->_profiles : @[];
    };
    _scheduler.globalEnabled = [[self userDefaults] objectForKey:kCIYScheduleEnabledDefaultsKey]
        ? [[self userDefaults] boolForKey:kCIYScheduleEnabledDefaultsKey]
        : YES;
    [self refreshScheduleMenuStates];

    if (_autoTriggerEnabled) {
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        NSString *ownBundleId = [[NSBundle mainBundle] bundleIdentifier];
        if (frontApp && ![frontApp isEqual:[NSRunningApplication currentApplication]]) {
            NSString *bid = frontApp.bundleIdentifier;
            if (!bid || bid.length == 0) {
                if (frontApp.localizedName.length > 0) {
                    bid = [NSString stringWithFormat:@"local.%@", frontApp.localizedName];
                }
            }
            if (bid && !(ownBundleId && [bid isEqualToString:ownBundleId])) {
                _pendingAppBundleId = [bid copy];
                _pendingAppName = [(frontApp.localizedName ?: bid) copy];
                [self executeAutoSwitch];
            }
        }
    }

    [_scheduler start];

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
    [CIYBatteryMonitor sharedMonitor].updateHandler = ^(CIYBatteryInfo info) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf handleBatteryUpdate:info];
        });
    };

    BOOL batteryMirrorOn = [[self userDefaults] boolForKey:kBatteryMirrorEnabledKey];
    NSInteger savedSource = [[self userDefaults] integerForKey:kBatterySourceKey];
    [_batterySourcePopUp selectItemWithTag:savedSource];
    [CIYBatteryMonitor sharedMonitor].preferredSource = (CIYBatterySourceType)savedSource;

    NSInteger savedTarget = [[self userDefaults] integerForKey:kBatteryTargetKey];
    [_batteryTargetPopUp selectItemWithTag:savedTarget];
    _customBatterySlots = [[self userDefaults] stringForKey:kBatteryCustomSlotsKey] ?: @"14,9,15,21";

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
    NSTextField *titleLabel = [self labelWithText:CIYL(@"CIY X79 Mechanical Keyboard") fontSize:18 weight:NSFontWeightBold];
    titleLabel.frame = NSMakeRect(24, y - 44, 380, 24);
    titleLabel.autoresizingMask = NSViewMinYMargin;
    [container addSubview:titleLabel];

    NSTextField *subtitleLabel = [self labelWithText:CIYL(@"ISO-DE 65% Layout • Profile-focused RGB control") fontSize:12 weight:NSFontWeightRegular];
    subtitleLabel.textColor = [NSColor secondaryLabelColor];
    subtitleLabel.frame = NSMakeRect(24, y - 64, 380, 18);
    subtitleLabel.autoresizingMask = NSViewMinYMargin;
    [container addSubview:subtitleLabel];

    // Status Indicator (oben rechts)
    _statusLabel = [self labelWithText:CIYL(@"● Connection is being checked...") fontSize:12 weight:NSFontWeightMedium];
    _statusLabel.frame = NSMakeRect(container.bounds.size.width - 340, y - 44, 316, 20);
    _statusLabel.alignment = NSTextAlignmentRight;
    _statusLabel.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [container addSubview:_statusLabel];

    NSButton *reconnectBtn = [NSButton buttonWithTitle:CIYL(@"Reconnect") target:self action:@selector(onReconnect:)];
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
    CGFloat profBoxH = 140;
    CGFloat profBoxY = y - 78 - profBoxH - 8;
    NSBox *boxProfile = [[NSBox alloc] initWithFrame:NSMakeRect(24, profBoxY, container.bounds.size.width - 48, profBoxH)];
    boxProfile.title = CIYL(@"Profile management & hardware transmission");
    boxProfile.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [container addSubview:boxProfile];

    NSView *profView = boxProfile.contentView;

    // Zeile 1: Filter nach Tags & Tag-Bearbeitung (y = 78)
    NSTextField *lblFilter = [self labelWithText:CIYL(@"Filter by tag:") fontSize:12 weight:NSFontWeightMedium];
    lblFilter.frame = NSMakeRect(16, 80, 84, 20);
    [profView addSubview:lblFilter];

    _profileFilterPopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(102, 78, 198, 26) pullsDown:NO];
    _profileFilterPopUp.target = self;
    _profileFilterPopUp.action = @selector(onSelectProfileFilter:);
    [profView addSubview:_profileFilterPopUp];

    _editTagsBtn = [NSButton buttonWithTitle:CIYL(@"Tags...") target:self action:@selector(onOpenTagSheet:)];
    _editTagsBtn.toolTip = CIYL(@"Edit tags for the selected profile");
    _editTagsBtn.frame = NSMakeRect(306, 78, 80, 26);
    _editTagsBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_editTagsBtn];

    // Zeile 2: Profil-Auswahl und Aktionen (y = 44)
    NSTextField *lblProf = [self labelWithText:CIYL(@"Active profile:") fontSize:12 weight:NSFontWeightMedium];
    lblProf.frame = NSMakeRect(16, 46, 84, 20);
    [profView addSubview:lblProf];

    _profilesPopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(102, 44, 198, 26) pullsDown:NO];
    _profilesPopUp.target = self;
    _profilesPopUp.action = @selector(onSelectProfile:);
    [profView addSubview:_profilesPopUp];

    _applyProfileBtn = [NSButton buttonWithTitle:CIYL(@"Apply (↵)") target:self action:@selector(onApplyProfileClicked:)];
    _applyProfileBtn.toolTip = CIYL(@"Applies selected profile immediately to the keyboard (Mode 19 Customize)");
    _applyProfileBtn.frame = NSMakeRect(306, 44, 128, 26);
    _applyProfileBtn.bezelStyle = NSBezelStyleRounded;
    _applyProfileBtn.keyEquivalent = @"\r"; // Enter-Taste aktiviert Senden
    [profView addSubview:_applyProfileBtn];

    _saveProfileBtn = [NSButton buttonWithTitle:CIYL(@"Save & Send") target:self action:@selector(onSaveAndSendProfile:)];
    _saveProfileBtn.toolTip = CIYL(@"Saves current color adjustments in the profile and sends them directly to the keyboard");
    _saveProfileBtn.frame = NSMakeRect(440, 44, 134, 26);
    _saveProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_saveProfileBtn];

    NSButton *newProfileBtn = [NSButton buttonWithTitle:CIYL(@"+ New...") target:self action:@selector(onNewProfile:)];
    newProfileBtn.toolTip = CIYL(@"Create and save a new profile from the current key colors");
    newProfileBtn.frame = NSMakeRect(580, 44, 60, 26);
    newProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:newProfileBtn];

    _renameProfileBtn = [NSButton buttonWithTitle:CIYL(@"Rename") target:self action:@selector(onRenameProfile:)];
    _renameProfileBtn.toolTip = CIYL(@"Rename selected profile");
    _renameProfileBtn.frame = NSMakeRect(646, 44, 84, 26);
    _renameProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_renameProfileBtn];

    _deleteProfileBtn = [NSButton buttonWithTitle:CIYL(@"Delete") target:self action:@selector(onDeleteProfile:)];
    _deleteProfileBtn.toolTip = CIYL(@"Remove selected profile");
    _deleteProfileBtn.frame = NSMakeRect(736, 44, 64, 26);
    _deleteProfileBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_deleteProfileBtn];

    NSPopUpButton *actionsBtn = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(806, 44, 66, 26) pullsDown:YES];
    [actionsBtn addItemWithTitle:CIYL(@"More ▾")];
    NSMenuItem *exportItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Export profiles...") action:@selector(onExportProfiles:) keyEquivalent:@""];
    exportItem.target = self;
    [actionsBtn.menu addItem:exportItem];
    NSMenuItem *importItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Import profiles...") action:@selector(onImportProfiles:) keyEquivalent:@""];
    importItem.target = self;
    [actionsBtn.menu addItem:importItem];
    [actionsBtn.menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *fallbackItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Set as default fallback") action:@selector(onSetDefaultFallback:) keyEquivalent:@""];
    fallbackItem.target = self;
    [actionsBtn.menu addItem:fallbackItem];
    NSMenuItem *scheduleActionItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Schedule profile...") action:@selector(onOpenScheduleSheet:) keyEquivalent:@""];
    scheduleActionItem.target = self;
    [actionsBtn.menu addItem:scheduleActionItem];
    NSMenuItem *resetItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Reset to defaults") action:@selector(onResetProfiles:) keyEquivalent:@""];
    resetItem.target = self;
    [actionsBtn.menu addItem:resetItem];
    [profView addSubview:actionsBtn];

    // Zeile 2: Auto-Trigger bei App-Fokus (y = 10)
    _autoTriggerCheckbox = [NSButton checkboxWithTitle:CIYL(@"⚡ Auto-Trigger on app focus") target:self action:@selector(onToggleAutoTrigger:)];
    _autoTriggerCheckbox.frame = NSMakeRect(16, 11, 206, 22);
    _autoTriggerCheckbox.toolTip = CIYL(@"Switches profiles automatically when linked games or apps become active (no keylogger, 100% unprivileged)");
    [profView addSubview:_autoTriggerCheckbox];

    NSTextField *lblLink = [self labelWithText:CIYL(@"Linked app:") fontSize:12 weight:NSFontWeightMedium];
    lblLink.frame = NSMakeRect(228, 13, 96, 18);
    [profView addSubview:lblLink];

    _linkedAppLabel = [self labelWithText:CIYL(@"No app linked (Manual)") fontSize:12 weight:NSFontWeightRegular];
    _linkedAppLabel.frame = NSMakeRect(326, 13, 240, 18);
    _linkedAppLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [profView addSubview:_linkedAppLabel];

    _assignAppBtn = [NSButton buttonWithTitle:CIYL(@"Select app...") target:self action:@selector(onAssignApp:)];
    _assignAppBtn.toolTip = CIYL(@"Link an application (e.g. from /Applications) to this profile");
    _assignAppBtn.frame = NSMakeRect(572, 9, 106, 26);
    _assignAppBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_assignAppBtn];

    _clearAppBtn = [NSButton buttonWithTitle:CIYL(@"✕ Unlink") target:self action:@selector(onClearApp:)];
    _clearAppBtn.toolTip = CIYL(@"Remove app link for this profile");
    _clearAppBtn.frame = NSMakeRect(682, 9, 74, 26);
    _clearAppBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_clearAppBtn];

    _setDefaultBtn = [NSButton buttonWithTitle:CIYL(@"☆ Fallback") target:self action:@selector(onSetDefaultFallback:)];
    _setDefaultBtn.toolTip = CIYL(@"Use this profile as the default fallback when an unlinked app is focused");
    _setDefaultBtn.frame = NSMakeRect(760, 9, 112, 26);
    _setDefaultBtn.bezelStyle = NSBezelStyleRounded;
    [profView addSubview:_setDefaultBtn];

    // Sektion: Interaktiver Tastatur-Editor
    CGFloat kbBoxY = 44;
    CGFloat kbBoxH = profBoxY - kbBoxY - 8;
    NSBox *boxKeyboard = [[NSBox alloc] initWithFrame:NSMakeRect(24, kbBoxY, container.bounds.size.width - 48, kbBoxH)];
    boxKeyboard.title = CIYL(@"Key editor (65% ISO-DE layout)");
    boxKeyboard.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [container addSubview:boxKeyboard];

    NSView *kbView = boxKeyboard.contentView;

    // Farb- und Tasten-Leiste
    _selectedKeyLabel = [self labelWithText:CIYL(@"Selected key: [ W ] (Slot 14)") fontSize:12 weight:NSFontWeightBold];
    _selectedKeyLabel.frame = NSMakeRect(16, kbBoxH - 46, 168, 22);
    _selectedKeyLabel.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_selectedKeyLabel];

    NSTextField *lblColorPick = [self labelWithText:CIYL(@"Color:") fontSize:12 weight:NSFontWeightMedium];
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
    _colorWell.toolTip = CIYL(@"Choose a custom color (macOS color picker)...");
    _colorWell.target = self;
    _colorWell.action = @selector(onColorWellChanged:);
    _colorWell.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_colorWell];

    NSButton *fillAllBtn = [NSButton buttonWithTitle:CIYL(@"All...") target:self action:@selector(onFillAllKeys:)];
    fillAllBtn.toolTip = CIYL(@"Fill all 69 keys in the editor with a base color");
    fillAllBtn.frame = NSMakeRect(btnX + 38, kbBoxH - 48, 52, 24);
    fillAllBtn.bezelStyle = NSBezelStyleRounded;
    fillAllBtn.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:fillAllBtn];

    // Zonen-Auswahl (v1.4)
    _zonePopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(btnX + 94, kbBoxH - 48, 76, 24) pullsDown:YES];
    [_zonePopUp addItemWithTitle:CIYL(@"Zone ▾")];
    NSMenuItem *wasdItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"WASD") action:@selector(onApplyZone:) keyEquivalent:@""];
    wasdItem.target = self;
    wasdItem.tag = 0;
    [_zonePopUp.menu addItem:wasdItem];

    NSMenuItem *arrowsItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Arrow keys") action:@selector(onApplyZone:) keyEquivalent:@""];
    arrowsItem.target = self;
    arrowsItem.tag = 1;
    [_zonePopUp.menu addItem:arrowsItem];

    NSMenuItem *numItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Numbers 1–0") action:@selector(onApplyZone:) keyEquivalent:@""];
    numItem.target = self;
    numItem.tag = 2;
    [_zonePopUp.menu addItem:numItem];

    NSMenuItem *modItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Modifiers") action:@selector(onApplyZone:) keyEquivalent:@""];
    modItem.target = self;
    modItem.tag = 3;
    [_zonePopUp.menu addItem:modItem];

    NSMenuItem *alphaItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Letters (Alpha)") action:@selector(onApplyZone:) keyEquivalent:@""];
    alphaItem.target = self;
    alphaItem.tag = 4;
    [_zonePopUp.menu addItem:alphaItem];

    NSMenuItem *navZoneItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Nav column") action:@selector(onApplyZone:) keyEquivalent:@""];
    navZoneItem.target = self;
    navZoneItem.tag = 5;
    [_zonePopUp.menu addItem:navZoneItem];

    _zonePopUp.autoresizingMask = NSViewMinYMargin;
    [kbView addSubview:_zonePopUp];

    // Verlauf-Generator (v1.4)
    _gradientBtn = [NSButton buttonWithTitle:CIYL(@"Gradient...") target:self action:@selector(onOpenGradientSheet:)];
    _gradientBtn.toolTip = CIYL(@"Generate a two-color linear gradient across U coordinates or rows");
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
    _brightnessSlider.toolTip = CIYL(@"Profile brightness (1 to 5)");
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
    NSTextField *footer = [self labelWithText:CIYL(@"CIY X79 Controller • Profile-focused IOKit USB control") fontSize:11 weight:NSFontWeightRegular];
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
    [_batteryTargetPopUp addItemWithTitle:CIYL(@"Numbers 1–0")];
    _batteryTargetPopUp.lastItem.tag = 0;
    [_batteryTargetPopUp addItemWithTitle:CIYL(@"Space bar")];
    _batteryTargetPopUp.lastItem.tag = 1;
    [_batteryTargetPopUp addItemWithTitle:CIYL(@"Nav column")];
    _batteryTargetPopUp.lastItem.tag = 2;
    [_batteryTargetPopUp addItemWithTitle:CIYL(@"Custom...")];
    _batteryTargetPopUp.lastItem.tag = 3;
    _batteryTargetPopUp.target = self;
    _batteryTargetPopUp.action = @selector(onBatteryTargetChanged:);
    _batteryTargetPopUp.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [container addSubview:_batteryTargetPopUp];

    _batterySourcePopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(rightEdge - 90 - 118 - 124, 12, 120, 24) pullsDown:NO];
    [_batterySourcePopUp addItemWithTitle:CIYL(@"Internal (Mac)")];
    _batterySourcePopUp.lastItem.tag = 0;
    [_batterySourcePopUp addItemWithTitle:CIYL(@"Peripheral (BT)")];
    _batterySourcePopUp.lastItem.tag = 1;
    [_batterySourcePopUp addItemWithTitle:CIYL(@"UPS")];
    _batterySourcePopUp.lastItem.tag = 2;
    [_batterySourcePopUp addItemWithTitle:CIYL(@"All sources")];
    _batterySourcePopUp.lastItem.tag = 3;
    _batterySourcePopUp.target = self;
    _batterySourcePopUp.action = @selector(onBatterySourceChanged:);
    _batterySourcePopUp.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [container addSubview:_batterySourcePopUp];

    _batteryMirrorCheckbox = [NSButton checkboxWithTitle:CIYL(@"⚡ Mirror battery") target:self action:@selector(onToggleBatteryMirror:)];
    _batteryMirrorCheckbox.frame = NSMakeRect(rightEdge - 90 - 118 - 124 - 120, 14, 116, 20);
    _batteryMirrorCheckbox.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    _batteryMirrorCheckbox.toolTip = CIYL(@"Temporarily mirrors the battery level to the CIY X79 (without overwriting the profile)");
    [container addSubview:_batteryMirrorCheckbox];
}

#pragma mark - Profilverwaltung

- (void)loadProfiles {
    NSArray *saved = [[self userDefaults] arrayForKey:kCustomProfilesKey];
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
                name = (trimmed.length > 0) ? TruncateProfileName(trimmed, 40) : CIYL(@"Unnamed");
            } else {
                name = CIYL(@"Unnamed");
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

            [CIYProfileTags sanitizeTagsInProfile:prof];
            [CIYProfileScheduler sanitizeScheduleInProfile:prof];
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

    NSString *lastActiveId = [[self userDefaults] stringForKey:kActiveProfileIdKey];
    NSUInteger selIdx = 0;
    if (lastActiveId) {
        for (NSUInteger i = 0; i < _profiles.count; i++) {
            if ([_profiles[i][@"id"] isEqualToString:lastActiveId]) {
                selIdx = i;
                break;
            }
        }
    }

    [self refreshProfileFilterPopUp];
    [self refreshProfilesPopUp];
    if (selIdx < _profiles.count) {
        _currentActiveProfileId = _profiles[selIdx][@"id"];
        [self selectPopUpForProfileIndex:selIdx];
        [self loadProfileIntoView:_profiles[selIdx]];
        [_keyboardView clearUndoHistory];
    }

    _autoTriggerEnabled = [[self userDefaults] boolForKey:kAutoTriggerEnabledKey];
    _fallbackProfileId = [[self userDefaults] stringForKey:kFallbackProfileIdKey];
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
        [[self userDefaults] setObject:_fallbackProfileId forKey:kFallbackProfileIdKey];
    }

    [self setHasUnsavedEdits:NO];
    [self updateButtonsState];
    [self updateAppTriggerUI];
}

- (void)addDefaultProfiles {
    // 1. WASD Gaming (Rot / Blau)
    [self addBuiltinProfileNamed:CIYL(@"🎮 WASD Gaming (Red / Base Blue)")
                         baseHex:@"#0066FF"
                        wasdHex:@"#FF0000"
                       arrowHex:@"#0066FF"];

    // 2. WASD Stealth (Grün / Aus)
    [self addBuiltinProfileNamed:CIYL(@"🎮 WASD Stealth (Green / Base Off)")
                         baseHex:@"#000000"
                        wasdHex:@"#00FF00"
                       arrowHex:@"#000000"];

    // 3. Pfeiltasten (Gelb / Blau)
    [self addBuiltinProfileNamed:CIYL(@"🎯 Arrow keys (Yellow / Base Blue)")
                         baseHex:@"#0066FF"
                        wasdHex:@"#0066FF"
                       arrowHex:@"#FFFF00"];

    // 4. WASD + Pfeile (Rot & Gelb / Blau)
    [self addBuiltinProfileNamed:CIYL(@"🕹 WASD + Arrows (Red & Yellow / Blue)")
                         baseHex:@"#0066FF"
                        wasdHex:@"#FF0000"
                       arrowHex:@"#FFFF00"];

    // 5. Vollrot (Alles Rot)
    [self addBuiltinProfileNamed:CIYL(@"🔴 Full red (All red)")
                         baseHex:@"#FF0000"
                        wasdHex:@"#FF0000"
                       arrowHex:@"#FF0000"];

    // 6. Cyberpunk (Cyan / Lila)
    [self addBuiltinProfileNamed:CIYL(@"🌌 Cyberpunk (Cyan / Purple)")
                         baseHex:@"#990099"
                        wasdHex:@"#00FFFF"
                       arrowHex:@"#FFFF00"];

    // 7. Büro / Clean (Dezent Weiß)
    [self addBuiltinProfileNamed:CIYL(@"💼 Office / Clean (Subtle White)")
                         baseHex:@"#CCCCCC"
                        wasdHex:@"#FFFFFF"
                       arrowHex:@"#FFFFFF"];

    // 8. Beleuchtung Aus
    [self addBuiltinProfileNamed:CIYL(@"⚫ Lighting off")
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
    [[self userDefaults] setObject:_profiles forKey:kCustomProfilesKey];
    if (_currentActiveProfileId) {
        [[self userDefaults] setObject:_currentActiveProfileId forKey:kActiveProfileIdKey];
    }
    [[self userDefaults] synchronize];
}

- (NSInteger)activeProfileIndex {
    if (!_currentActiveProfileId || !_profiles) return -1;
    for (NSUInteger i = 0; i < _profiles.count; i++) {
        if ([_profiles[i][@"id"] isEqualToString:_currentActiveProfileId]) {
            return (NSInteger)i;
        }
    }
    return -1;
}

- (NSInteger)selectedProfileIndex {
    NSInteger profIdx = [CIYProfileTags profileIndexForRow:_profilesPopUp.indexOfSelectedItem inPopUpIndices:_visibleProfileIndices];
    if (profIdx < 0 || (NSUInteger)profIdx >= _profiles.count) {
        return -1;
    }
    return profIdx;
}

- (nullable NSMutableDictionary *)selectedProfile {
    return (NSMutableDictionary *)[CIYProfileTags profileForRow:_profilesPopUp.indexOfSelectedItem
                                                 inPopUpIndices:_visibleProfileIndices
                                                       profiles:_profiles];
}

- (void)selectPopUpForProfileIndex:(NSInteger)targetIdx {
    if (targetIdx < 0 || !_visibleProfileIndices) return;
    NSInteger row = [CIYProfileTags rowForProfileIndex:targetIdx inPopUpIndices:_visibleProfileIndices];
    if (row >= 0 && row < _profilesPopUp.numberOfItems) {
        [_profilesPopUp selectItemAtIndex:row];
    }
}

- (void)refreshProfileFilterPopUp {
    if (!_profileFilterPopUp) return;
    NSString *currentFilter = _selectedFilterTag;
    [_profileFilterPopUp removeAllItems];
    [_profileFilterPopUp addItemWithTitle:CIYL(@"All")];

    NSArray<NSString *> *allTags = [CIYProfileTags allTagsInProfiles:_profiles];
    for (NSString *tag in allTags) {
        [_profileFilterPopUp addItemWithTitle:tag];
    }

    if (currentFilter && currentFilter.length > 0) {
        NSString *normFilter = [CIYProfileTags normalizedTagKey:currentFilter];
        NSInteger matchIdx = 0;
        for (NSInteger i = 1; i < _profileFilterPopUp.numberOfItems; i++) {
            NSString *title = [_profileFilterPopUp itemTitleAtIndex:i];
            if ([[CIYProfileTags normalizedTagKey:title] isEqualToString:normFilter]) {
                matchIdx = i;
                break;
            }
        }
        if (matchIdx > 0) {
            [_profileFilterPopUp selectItemAtIndex:matchIdx];
        } else {
            _selectedFilterTag = nil;
            [_profileFilterPopUp selectItemAtIndex:0];
        }
    } else {
        [_profileFilterPopUp selectItemAtIndex:0];
    }
}

- (void)onSelectProfileFilter:(NSPopUpButton *)sender {
    NSInteger sel = sender.indexOfSelectedItem;
    if (sel <= 0) {
        _selectedFilterTag = nil;
    } else {
        _selectedFilterTag = [sender itemTitleAtIndex:sel];
    }
    [self refreshProfilesPopUp];

    NSInteger activeIdx = [self activeProfileIndex];
    [self selectPopUpForProfileIndex:activeIdx];
    [self updateButtonsState];
}

- (void)refreshProfilesPopUp {
    NSInteger activeIdx = [self activeProfileIndex];
    BOOL prepended = NO;
    _visibleProfileIndices = [CIYProfileTags popUpIndicesForProfiles:_profiles
                                                          filterTag:_selectedFilterTag
                                                 activeProfileIndex:activeIdx
                                                  activeIsPrepended:&prepended];
    _activeProfilePrependedInPopUp = prepended;

    [_profilesPopUp removeAllItems];
    for (NSUInteger row = 0; row < _visibleProfileIndices.count; row++) {
        NSInteger profIdx = _visibleProfileIndices[row].integerValue;
        NSDictionary *prof = _profiles[(NSUInteger)profIdx];
        NSString *name = prof[@"name"] ?: CIYL(@"Unnamed");
        BOOL isPrependedRow = (prepended && row == 0);
        NSString *title = isPrependedRow ? [NSString stringWithFormat:CIYL(@"● %@ (hidden)"), name] : name;

        id rawTags = prof[kCIYProfileTagsKey];
        if ([rawTags isKindOfClass:[NSArray class]] && ((NSArray *)rawTags).count > 0) {
            NSString *tagsStr = [((NSArray *)rawTags) componentsJoinedByString:@", "];
            title = [NSString stringWithFormat:@"%@  [%@]", title, tagsStr];
        }

        NSString *schedBadge = [CIYProfileScheduler shortScheduleSummaryForProfile:prof];
        if (schedBadge) {
            title = [NSString stringWithFormat:@"%@  (⏰ %@)", title, schedBadge];
        }
        [_profilesPopUp.menu addItemWithTitle:title action:nil keyEquivalent:@""];
    }
    [self syncMiniWindowProfiles];
}

- (void)syncMiniWindowProfiles {
    if (!_miniController) return;
    NSMutableArray<NSString *> *names = [NSMutableArray arrayWithCapacity:_profiles.count];
    for (NSDictionary *prof in _profiles) {
        NSString *name = prof[@"name"] ?: CIYL(@"Unnamed");
        [names addObject:name];
    }
    NSInteger activeIdx = [self activeProfileIndex];
    [_miniController setProfileNames:names selectedIndex:activeIdx];
}

- (void)updateButtonsState {
    NSInteger idx = [self selectedProfileIndex];
    BOOL hasSelection = (idx >= 0 && (NSUInteger)idx < _profiles.count);

    _applyProfileBtn.enabled = hasSelection;
    _saveProfileBtn.enabled = hasSelection;
    _renameProfileBtn.enabled = hasSelection;
    _deleteProfileBtn.enabled = (hasSelection && _profiles.count > 1);
    _editTagsBtn.enabled = hasSelection;
    [self updateAppTriggerUI];
    if (_miniController) {
        [_miniController setSelectedIndex:[self activeProfileIndex]];
    }
}

#pragma mark - Auto-Trigger (App-Fokus & Profilwechsel)

- (void)updateAppTriggerUI {
    _autoTriggerCheckbox.state = _autoTriggerEnabled ? NSControlStateValueOn : NSControlStateValueOff;

    NSDictionary *prof = [self selectedProfile];
    if (prof) {
        NSString *appName = prof[@"appName"];
        NSString *bundleId = prof[@"appBundleId"];
        BOOL isFallback = (_fallbackProfileId && [prof[@"id"] isEqualToString:_fallbackProfileId]);

        if (appName.length > 0 || bundleId.length > 0) {
            NSString *display = appName.length > 0 ? appName : bundleId;
            _linkedAppLabel.stringValue = [NSString stringWithFormat:@"%@%@ (%@)", isFallback ? CIYL(@"[★ Fallback] ") : @"", display, bundleId ?: @""];
            _linkedAppLabel.textColor = [NSColor controlAccentColor];
            _clearAppBtn.enabled = YES;
        } else {
            _linkedAppLabel.stringValue = isFallback ? CIYL(@"[★ Standard Fallback] No app linked") : CIYL(@"No app linked (Manual)");
            _linkedAppLabel.textColor = isFallback ? [NSColor systemYellowColor] : [NSColor secondaryLabelColor];
            _clearAppBtn.enabled = NO;
        }

        _assignAppBtn.enabled = YES;
        _setDefaultBtn.enabled = !isFallback;
        _setDefaultBtn.title = isFallback ? CIYL(@"★ Fallback") : CIYL(@"☆ Set as fallback");
    } else {
        _linkedAppLabel.stringValue = @"-";
        _linkedAppLabel.textColor = [NSColor secondaryLabelColor];
        _clearAppBtn.enabled = NO;
        _assignAppBtn.enabled = NO;
        _setDefaultBtn.enabled = NO;
    }

    [self updateStatusItemMenu];
}

- (void)onToggleAutoTrigger:(NSButton *)sender {
    _autoTriggerEnabled = (sender.state == NSControlStateValueOn);
    [[self userDefaults] setBool:_autoTriggerEnabled forKey:kAutoTriggerEnabledKey];
    [[self userDefaults] synchronize];
    [self updateAppTriggerUI];

    if (_autoTriggerEnabled) {
        _statusLabel.stringValue = CIYL(@"● Auto-trigger active (switches profile on app focus)");
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        NSString *ownBundleId = [[NSBundle mainBundle] bundleIdentifier];
        if (frontApp && ![frontApp isEqual:[NSRunningApplication currentApplication]]) {
            NSString *bid = frontApp.bundleIdentifier;
            if (!bid || bid.length == 0) {
                if (frontApp.localizedName.length > 0) {
                    bid = [NSString stringWithFormat:@"local.%@", frontApp.localizedName];
                }
            }
            if (bid && !(ownBundleId && [bid isEqualToString:ownBundleId])) {
                _pendingAppBundleId = [bid copy];
                _pendingAppName = [(frontApp.localizedName ?: bid) copy];
                [self executeAutoSwitch];
            }
        }
    } else {
        [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
        _pendingAppBundleId = nil;
        _pendingAppName = nil;
        _statusLabel.stringValue = CIYL(@"● Auto-trigger disabled (manual profile selection)");
        _statusLabel.textColor = [NSColor secondaryLabelColor];
        if (_scheduler && _scheduler.globalEnabled) {
            [_scheduler reevaluate];
        }
    }
}

- (void)toggleAutoTriggerMenu:(id)sender {
    (void)sender;
    _autoTriggerEnabled = !_autoTriggerEnabled;
    [[self userDefaults] setBool:_autoTriggerEnabled forKey:kAutoTriggerEnabledKey];
    [[self userDefaults] synchronize];
    [self updateAppTriggerUI];

    if (_autoTriggerEnabled) {
        _statusLabel.stringValue = CIYL(@"● Auto-trigger active (switches profile on app focus)");
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        NSString *ownBundleId = [[NSBundle mainBundle] bundleIdentifier];
        if (frontApp && ![frontApp isEqual:[NSRunningApplication currentApplication]]) {
            NSString *bid = frontApp.bundleIdentifier;
            if (!bid || bid.length == 0) {
                if (frontApp.localizedName.length > 0) {
                    bid = [NSString stringWithFormat:@"local.%@", frontApp.localizedName];
                }
            }
            if (bid && !(ownBundleId && [bid isEqualToString:ownBundleId])) {
                _pendingAppBundleId = [bid copy];
                _pendingAppName = [(frontApp.localizedName ?: bid) copy];
                [self executeAutoSwitch];
            }
        }
    } else {
        [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
        _pendingAppBundleId = nil;
        _pendingAppName = nil;
        _statusLabel.stringValue = CIYL(@"● Auto-trigger disabled (manual profile selection)");
        _statusLabel.textColor = [NSColor secondaryLabelColor];
        if (_scheduler && _scheduler.globalEnabled) {
            [_scheduler reevaluate];
        }
    }
}

- (void)onAssignApp:(id)sender {
    (void)sender;
    NSInteger idx = [self selectedProfileIndex];
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;
    NSString *targetId = _profiles[(NSUInteger)idx][@"id"];

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = CIYL(@"Select application for automatic profile");
    panel.prompt = CIYL(@"Link");
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;
    panel.canChooseFiles = YES;
    panel.directoryURL = [NSURL fileURLWithPath:@"/Applications"];
    panel.allowedContentTypes = @[UTTypeApplication];

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            NSBundle *appBundle = [NSBundle bundleWithURL:panel.URL];
            NSString *rawAppName = [panel.URL.lastPathComponent stringByDeletingPathExtension];
            NSString *appName = SanitizeAppMetadata(rawAppName) ?: CIYL(@"App");
            NSString *bundleId = SanitizeAppMetadata(appBundle.bundleIdentifier);
            if (!bundleId || bundleId.length == 0) {
                bundleId = SanitizeAppMetadata([NSString stringWithFormat:@"local.%@", appName]) ?: @"local.App";
            }

            NSMutableDictionary *prof = nil;
            for (NSMutableDictionary *p in self->_profiles) {
                if ([p[@"id"] isEqualToString:targetId]) {
                    prof = p;
                    break;
                }
            }
            if (!prof) return;

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

            self->_statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Profile “%@” linked to app “%@”"), prof[@"name"] ?: @"", appName];
            self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        }
    }];
}

- (void)onClearApp:(id)sender {
    (void)sender;
    NSMutableDictionary *prof = [self selectedProfile];
    if (!prof) return;

    [prof removeObjectForKey:@"appBundleId"];
    [prof removeObjectForKey:@"appName"];

    [self saveProfilesToDefaults];
    [self updateAppTriggerUI];

    _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● App link removed for profile “%@”"), prof[@"name"] ?: @""];
    _statusLabel.textColor = [NSColor secondaryLabelColor];
}

- (void)onSetDefaultFallback:(id)sender {
    (void)sender;
    NSDictionary *prof = [self selectedProfile];
    if (!prof) return;

    _fallbackProfileId = prof[@"id"];
    [[self userDefaults] setObject:_fallbackProfileId forKey:kFallbackProfileIdKey];
    [[self userDefaults] synchronize];

    [self updateAppTriggerUI];
    _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Profile “%@” set as default fallback"), prof[@"name"] ?: @""];
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
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Auto-trigger paused: profile “%@” has unsaved changes"), [self currentProfileName]];
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

    // 2. Falls kein Treffer: Aktives Zeitplan-Profil (falls Zeitsteuerung aktiv), sonst Fallback-Profil verwenden
    if (targetIdx < 0 && _scheduler && _scheduler.globalEnabled) {
        NSString *schedId = [_scheduler activeScheduledProfileId];
        if (schedId) {
            for (NSUInteger i = 0; i < _profiles.count; i++) {
                if ([_profiles[i][@"id"] isEqualToString:schedId]) {
                    targetIdx = (NSInteger)i;
                    break;
                }
            }
        }
    }

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
    _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Auto-trigger: “%@” active (%@)"), name, _pendingAppName ?: CIYL(@"App")];
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
}

- (BOOL)foregroundAppHasLinkedProfile {
    if (!_autoTriggerEnabled) return NO;

    NSRunningApplication *frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
    NSString *ownBundleId = [[NSBundle mainBundle] bundleIdentifier];
    BOOL isFrontAppExternal = (frontApp != nil &&
                               ![frontApp isEqual:[NSRunningApplication currentApplication]] &&
                               !(ownBundleId && frontApp.bundleIdentifier && [frontApp.bundleIdentifier isEqualToString:ownBundleId]));

    NSString *effectiveBundleId = nil;
    if (isFrontAppExternal) {
        effectiveBundleId = frontApp.bundleIdentifier;
        if (!effectiveBundleId || effectiveBundleId.length == 0) {
            if (frontApp.localizedName.length > 0) {
                effectiveBundleId = [NSString stringWithFormat:@"local.%@", frontApp.localizedName];
            }
        }
    } else {
        // CIYX79Control selbst im Vordergrund (oder frontApp == nil) und _pendingAppBundleId gesetzt
        if (_pendingAppBundleId) {
            BOOL processRunning = NO;
            for (NSRunningApplication *app in [[NSWorkspace sharedWorkspace] runningApplications]) {
                if (app.isTerminated) continue;
                NSString *bid = app.bundleIdentifier;
                if (!bid || bid.length == 0) {
                    if (app.localizedName.length > 0) {
                        bid = [NSString stringWithFormat:@"local.%@", app.localizedName];
                    }
                }
                if (bid && [bid isEqualToString:_pendingAppBundleId]) {
                    processRunning = YES;
                    break;
                }
            }
            if (!processRunning) {
                _pendingAppBundleId = nil;
                _pendingAppName = nil;
                return NO;
            }
            effectiveBundleId = _pendingAppBundleId;
        } else {
            return NO;
        }
    }

    if (!effectiveBundleId) return NO;

    for (NSDictionary *prof in _profiles) {
        NSString *linkedId = prof[@"appBundleId"];
        if (linkedId && [linkedId isEqualToString:effectiveBundleId]) {
            if (!isFrontAppExternal) {
                if ([prof[@"id"] isEqualToString:_currentActiveProfileId]) {
                    return YES;
                }
                return NO;
            }
            return YES;
        }
    }
    return NO;
}

#pragma mark - CIYProfileSchedulerDelegate

- (void)scheduler:(CIYProfileScheduler *)scheduler didDetermineTargetProfileId:(nullable NSString *)profileId {
    (void)scheduler;
    if (!profileId) return;

    // Ungespeicherte Bearbeitungen des Benutzers im Editor niemals automatisch überschreiben!
    if (_hasUnsavedEdits) {
        if (![profileId isEqualToString:_currentActiveProfileId]) {
            _scheduleSwitchDeferredByEdits = YES;
            _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Schedule paused: profile “%@” has unsaved changes"), [self currentProfileName]];
            _statusLabel.textColor = [NSColor systemOrangeColor];
        }
        return;
    }

    // Koexistenz mit App-Fokus:
    // Wenn Auto-Trigger aktiv ist und die aktuelle Vordergrund-App explizit einem App-Profil zugeordnet ist,
    // behält dieses spezifische App-Profil Vorrang.
    if ([self foregroundAppHasLinkedProfile]) {
        return;
    }

    // Kein redundanter USB-Transfer, wenn das Zielprofil bereits aktiv ist
    if ([profileId isEqualToString:_currentActiveProfileId]) {
        return;
    }

    NSInteger targetIdx = -1;
    for (NSUInteger i = 0; i < _profiles.count; i++) {
        if ([_profiles[i][@"id"] isEqualToString:profileId]) {
            targetIdx = (NSInteger)i;
            break;
        }
    }

    if (targetIdx < 0) return;

    [self activateProfileAtIndex:targetIdx];

    NSString *name = [self currentProfileName];
    _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Schedule: “%@” active"), name];
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
}

- (NSString *)currentProfileName {
    NSDictionary *prof = [self selectedProfile];
    if (prof) {
        return prof[@"name"] ?: CIYL(@"Profile");
    }
    return CIYL(@"Profile");
}

- (BOOL)canvasMatchesSavedProfile {
    NSDictionary *prof = [self selectedProfile];
    if (!prof) return NO;
    return [_keyboardView matchesProfileSlots:prof[@"slots"]];
}

- (void)loadProfileIntoView:(NSDictionary *)prof {
    NSDictionary<NSString *, NSString *> *slots = prof[@"slots"];
    if (!slots || ![slots isKindOfClass:[NSDictionary class]]) return;

    NSColor *offColor = [NSColor colorWithSRGBRed:0.0 green:0.0 blue:0.0 alpha:1.0];
    NSMutableDictionary<NSNumber *, NSColor *> *keyColors = [NSMutableDictionary dictionaryWithCapacity:CIY_LED_COUNT];
    for (int i = 0; i < CIY_LED_COUNT; i++) {
        keyColors[@(i)] = offColor;
    }
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

    _scheduleSwitchDeferredByEdits = NO;
    NSDictionary *prof = _profiles[(NSUInteger)idx];
    _currentActiveProfileId = prof[@"id"];
    [self saveProfilesToDefaults];

    [self refreshProfilesPopUp];
    [self selectPopUpForProfileIndex:idx];

    [self loadProfileIntoView:prof];
    [_keyboardView clearUndoHistory];
    [self setHasUnsavedEdits:NO];
    [self updateButtonsState];

    // Direkt das ausgewählte Profil an die Tastatur senden
    if (![KeyboardBackend shared].isConnected) {
        [self recordSendResult:NO];
    }
    [self sendCurrentProfileToHardware];
}

- (void)onSelectProfile:(NSPopUpButton *)sender {
    NSInteger row = sender.indexOfSelectedItem;
    if (row < 0 || !_visibleProfileIndices || (NSUInteger)row >= _visibleProfileIndices.count) return;
    NSInteger profIdx = _visibleProfileIndices[(NSUInteger)row].integerValue;
    [self activateProfileAtIndex:profIdx];
}

- (void)onStatusMenuSelectProfile:(NSMenuItem *)sender {
    [self activateProfileAtIndex:sender.tag];
}

- (void)onApplyProfileClicked:(id)sender {
    (void)sender;
    if (![KeyboardBackend shared].isConnected) {
        [self recordSendResult:NO];
    }
    [self sendCurrentProfileToHardware];
}

- (void)onSaveAndSendProfile:(id)sender {
    (void)sender;
    NSInteger idx = [self selectedProfileIndex];
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *name = prof[@"name"] ?: CIYL(@"Profile");

    // Aktuelle Editor-Farben im ausgewählten Profil speichern
    NSDictionary<NSNumber *, NSColor *> *allColors = [_keyboardView allKeyColors];
    NSMutableDictionary<NSString *, NSString *> *slotHex = [NSMutableDictionary dictionaryWithCapacity:allColors.count];
    for (NSNumber *slotNum in allColors) {
        slotHex[slotNum.stringValue] = HexFromColor(allColors[slotNum]);
    }
    prof[@"slots"] = slotHex;

    BOOL hadDeferredScheduleSwitch = _scheduleSwitchDeferredByEdits;
    NSString *savedProfileId = prof[@"id"];
    _scheduleSwitchDeferredByEdits = NO;
    _currentActiveProfileId = savedProfileId;

    [self saveProfilesToDefaults];
    [_keyboardView clearUndoHistory];
    [self setHasUnsavedEdits:NO];
    [self updateButtonsState];

    if (hadDeferredScheduleSwitch && _scheduler && _scheduler.globalEnabled) {
        [_scheduler reevaluate];
        if (savedProfileId && ![_currentActiveProfileId isEqualToString:savedProfileId]) {
            return;
        }
    }

    // Sofort an Tastatur übertragen
    [self sendCurrentProfileToHardware];

    _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Profile “%@” saved & sent"), name];
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
}

- (void)onNewProfile:(id)sender {
    (void)sender;
    if (_profiles.count >= kMaxProfilesCount) {
        NSAlert *limitAlert = [[NSAlert alloc] init];
        limitAlert.messageText = CIYL(@"Profile limit reached");
        limitAlert.informativeText = [NSString stringWithFormat:CIYL(@"A maximum of %lu profiles can be stored. Please delete an unneeded profile first."), (unsigned long)kMaxProfilesCount];
        [limitAlert addButtonWithTitle:CIYL(@"OK")];
        [limitAlert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = CIYL(@"Create new lighting profile");
    alert.informativeText = CIYL(@"Enter a name for the new profile:");
    [alert addButtonWithTitle:CIYL(@"Save & Send")];
    [alert addButtonWithTitle:CIYL(@"Cancel")];

    NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
    input.placeholderString = CIYL(@"e.g. Coding, Cyberpunk, CS:GO");
    input.stringValue = @"";
    alert.accessoryView = input;
    [alert.window setInitialFirstResponder:input];

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            if (self->_profiles.count >= kMaxProfilesCount) return;
            NSString *name = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (name.length == 0) {
                name = [NSString stringWithFormat:CIYL(@"Profile %lu"), (unsigned long)(self->_profiles.count + 1)];
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
            newProf[kCIYProfileScheduleEnabledKey] = @(NO);
            [CIYProfileScheduler sanitizeScheduleInProfile:newProf];

            [self->_profiles addObject:newProf];
            self->_currentActiveProfileId = newId;
            self->_selectedFilterTag = nil;
            [self saveProfilesToDefaults];
            [self refreshProfileFilterPopUp];
            [self refreshProfilesPopUp];

            [self selectPopUpForProfileIndex:(NSInteger)(self->_profiles.count - 1)];
            [self->_keyboardView clearUndoHistory];
            self->_scheduleSwitchDeferredByEdits = NO;
            [self setHasUnsavedEdits:NO];
            [self updateButtonsState];

            [self sendCurrentProfileToHardware];
            [self->_scheduler rescheduleTimerOnly];
        }
    }];
}

- (void)onRenameProfile:(id)sender {
    (void)sender;
    NSInteger idx = [self selectedProfileIndex];
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    NSString *targetId = _profiles[(NSUInteger)idx][@"id"];
    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *currentName = prof[@"name"] ?: @"";

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = CIYL(@"Rename profile");
    alert.informativeText = CIYL(@"Enter the new name for this profile:");
    [alert addButtonWithTitle:CIYL(@"Rename")];
    [alert addButtonWithTitle:CIYL(@"Cancel")];

    NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
    input.stringValue = currentName;
    alert.accessoryView = input;
    [alert.window setInitialFirstResponder:input];

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            NSString *name = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (name.length > 0) {
                if (name.length > 40) name = TruncateProfileName(name, 40);
                NSInteger currentIdx = -1;
                for (NSUInteger i = 0; i < self->_profiles.count; i++) {
                    if ([self->_profiles[i][@"id"] isEqualToString:targetId]) {
                        self->_profiles[i][@"name"] = name;
                        currentIdx = (NSInteger)i;
                        break;
                    }
                }
                [self saveProfilesToDefaults];
                [self refreshProfilesPopUp];
                if (currentIdx >= 0) {
                    [self selectPopUpForProfileIndex:currentIdx];
                }
                [self updateButtonsState];
                self->_statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Profile renamed to “%@”"), name];
                self->_statusLabel.textColor = [NSColor secondaryLabelColor];
            }
        }
    }];
}

- (void)onDeleteProfile:(id)sender {
    (void)sender;
    NSInteger idx = [self selectedProfileIndex];
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;

    if (_profiles.count <= 1) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = CIYL(@"Profile cannot be deleted");
        alert.informativeText = CIYL(@"At least one profile must remain.");
        [alert addButtonWithTitle:CIYL(@"OK")];
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

    NSString *targetId = _profiles[(NSUInteger)idx][@"id"];
    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *name = prof[@"name"] ?: CIYL(@"Profile");

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithFormat:CIYL(@"Delete profile “%@”?"), name];
    alert.informativeText = CIYL(@"Do you really want to permanently delete this lighting profile?");
    [alert addButtonWithTitle:CIYL(@"Delete")];
    [alert addButtonWithTitle:CIYL(@"Cancel")];
    alert.alertStyle = NSAlertStyleCritical;

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            NSInteger deleteIdx = -1;
            for (NSUInteger i = 0; i < self->_profiles.count; i++) {
                if ([self->_profiles[i][@"id"] isEqualToString:targetId]) {
                    deleteIdx = (NSInteger)i;
                    break;
                }
            }
            if (deleteIdx < 0) return;

            NSString *deletedId = self->_profiles[(NSUInteger)deleteIdx][@"id"];
            [self->_profiles removeObjectAtIndex:(NSUInteger)deleteIdx];

            NSUInteger nextIdx = (deleteIdx > 0) ? (NSUInteger)(deleteIdx - 1) : 0;
            self->_currentActiveProfileId = self->_profiles[nextIdx][@"id"];
            if ([self->_fallbackProfileId isEqualToString:deletedId]) {
                self->_fallbackProfileId = self->_profiles[nextIdx][@"id"];
                [[self userDefaults] setObject:self->_fallbackProfileId forKey:kFallbackProfileIdKey];
            }

            [self saveProfilesToDefaults]; // Erst speichern, nachdem activeProfileId aktualisiert wurde!

            [self refreshProfileFilterPopUp];
            [self refreshProfilesPopUp];
            [self selectPopUpForProfileIndex:(NSInteger)nextIdx];
            [self loadProfileIntoView:self->_profiles[nextIdx]];
            [self->_keyboardView clearUndoHistory];

            self->_scheduleSwitchDeferredByEdits = NO;
            [self setHasUnsavedEdits:NO];
            [self updateButtonsState];
            [self sendCurrentProfileToHardware];
            [self->_scheduler rescheduleTimerOnly];
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

    NSMenu *menu = [[NSMenu alloc] initWithTitle:CIYL(@"Color all keys")];
    NSArray<NSDictionary *> *colors = KeyPalette();
    for (NSDictionary *cDict in colors) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:CIYL(@"All keys: %@"), cDict[@"name"]]
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

    BOOL changed = [_keyboardView resetAllKeysToColor:color];
    if (_currentSelectedSlot >= 0 && _colorWell) {
        _colorWell.color = color;
    }
    if (!changed) return;

    BOOL matches = [self canvasMatchesSavedProfile];
    [self setHasUnsavedEdits:!matches];
    [self updateButtonsState];

    NSString *profName = [self currentProfileName];
    if (matches) {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● No unsaved changes (profile “%@”)"), profName];
        _statusLabel.textColor = [NSColor systemCyanColor];
    } else {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● All keys colored (profile “%@” unsaved)"), profName];
        _statusLabel.textColor = [NSColor systemOrangeColor];
    }
    // WICHTIG: Kein USB-Senden bei Zwischenschritten im Editor
}

#pragma mark - CIYKeyboardViewDelegate & Farbwahl

- (void)keyboardViewDidSelectKey:(int)slot label:(NSString *)label {
    _currentSelectedSlot = slot;
    _selectedKeyLabel.stringValue = [NSString stringWithFormat:CIYL(@"Selected key: [ %@ ] (Slot %d)"), label, slot];
    NSColor *c = [_keyboardView allKeyColors][@(slot)];
    if (c && _colorWell) {
        _colorWell.color = c;
    }
}

- (void)keyboardViewDidUndo:(CIYKeyboardView *)view {
    (void)view;
    if (_currentSelectedSlot >= 0 && _colorWell) {
        NSColor *c = [_keyboardView allKeyColors][@(_currentSelectedSlot)];
        if (c) {
            _colorWell.color = c;
        }
    }

    BOOL matches = [self canvasMatchesSavedProfile];
    NSString *prevActiveId = [_currentActiveProfileId copy];
    [self setHasUnsavedEdits:!matches];
    [self updateButtonsState];

    if (prevActiveId == _currentActiveProfileId || (prevActiveId && [_currentActiveProfileId isEqualToString:prevActiveId])) {
        NSString *profName = [self currentProfileName];
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Undo (profile “%@”)"), profName];
        _statusLabel.textColor = matches ? [NSColor systemCyanColor] : [NSColor systemOrangeColor];
    }
}

- (void)keyboardViewDidRedo:(CIYKeyboardView *)view {
    (void)view;
    if (_currentSelectedSlot >= 0 && _colorWell) {
        NSColor *c = [_keyboardView allKeyColors][@(_currentSelectedSlot)];
        if (c) {
            _colorWell.color = c;
        }
    }

    BOOL matches = [self canvasMatchesSavedProfile];
    NSString *prevActiveId = [_currentActiveProfileId copy];
    [self setHasUnsavedEdits:!matches];
    [self updateButtonsState];

    if (prevActiveId == _currentActiveProfileId || (prevActiveId && [_currentActiveProfileId isEqualToString:prevActiveId])) {
        NSString *profName = [self currentProfileName];
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Redo (profile “%@”)"), profName];
        _statusLabel.textColor = matches ? [NSColor systemCyanColor] : [NSColor systemOrangeColor];
    }
}

- (void)onKeyColorChosen:(NSButton *)sender {
    if (_currentSelectedSlot < 0) return;

    NSArray<NSDictionary *> *colors = KeyPalette();
    if (sender.tag >= 0 && (NSUInteger)sender.tag < colors.count) {
        NSColor *c = colors[(NSUInteger)sender.tag][@"c"];
        BOOL changed = [_keyboardView setColor:c forSlot:_currentSelectedSlot];
        if (_colorWell) _colorWell.color = c;
        if (!changed) return;

        BOOL matches = [self canvasMatchesSavedProfile];
        [self setHasUnsavedEdits:!matches];
        [self updateButtonsState];

        NSString *profName = [self currentProfileName];
        if (matches) {
            _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● No unsaved changes (profile “%@”)"), profName];
            _statusLabel.textColor = [NSColor systemCyanColor];
        } else {
            _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Key edited (profile “%@” unsaved)"), profName];
            _statusLabel.textColor = [NSColor systemOrangeColor];
        }
        // WICHTIG: Kein USB-Senden bei Zwischenschritten im Editor!
    }
}

- (void)onColorWellChanged:(NSColorWell *)sender {
    if (_currentSelectedSlot < 0) return;
    NSColor *chosen = sender.color;
    BOOL changed = [_keyboardView setColor:chosen forSlot:_currentSelectedSlot continuous:YES];
    if (!changed) return;

    BOOL matches = [self canvasMatchesSavedProfile];
    [self setHasUnsavedEdits:!matches];
    [self updateButtonsState];

    NSString *profName = [self currentProfileName];
    if (matches) {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● No unsaved changes (profile “%@”)"), profName];
        _statusLabel.textColor = [NSColor systemCyanColor];
    } else {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Custom color selected (profile “%@” unsaved)"), profName];
        _statusLabel.textColor = [NSColor systemOrangeColor];
    }
}

- (void)onBrightnessChanged:(NSSlider *)sender {
    _currentBrightness = (uint8_t)sender.integerValue;
    sender.toolTip = [NSString stringWithFormat:CIYL(@"Brightness: %d / 5"), _currentBrightness];
    [self sendCurrentProfileToHardware];
}

#pragma mark - Zonen- & Farbverlauf-Aktionen (v1.4)

- (void)onApplyZone:(NSMenuItem *)sender {
    NSArray<NSNumber *> *slots = SlotsForZone(sender.tag);
    if (slots.count == 0) return;
    NSColor *color = _colorWell.color;
    BOOL changed = [_keyboardView applyColor:color toSlots:slots];
    if (_currentSelectedSlot >= 0 && _colorWell) {
        NSColor *c = [_keyboardView allKeyColors][@(_currentSelectedSlot)];
        if (c) _colorWell.color = c;
    }
    if (!changed) return;

    BOOL matches = [self canvasMatchesSavedProfile];
    [self setHasUnsavedEdits:!matches];
    [self updateButtonsState];

    NSString *profName = [self currentProfileName];
    if (matches) {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● No unsaved changes (profile “%@”)"), profName];
        _statusLabel.textColor = [NSColor systemCyanColor];
    } else {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Zone “%@” colored (profile “%@” unsaved)"), sender.title, profName];
        _statusLabel.textColor = [NSColor systemOrangeColor];
    }
}

- (void)onOpenGradientSheet:(id)sender {
    (void)sender;
    _gradientSheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 380, 220)
                                                 styleMask:(NSWindowStyleMaskTitled)
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    _gradientSheet.title = CIYL(@"Gradient generator (linear gradient)");

    NSView *content = _gradientSheet.contentView;

    NSTextField *lblStart = [self labelWithText:CIYL(@"Start color:") fontSize:12 weight:NSFontWeightMedium];
    lblStart.frame = NSMakeRect(24, 162, 90, 20);
    [content addSubview:lblStart];

    _gradStartWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(120, 160, 48, 26)];
    _gradStartWell.color = [NSColor colorWithSRGBRed:0.0 green:0.9 blue:1.0 alpha:1.0]; // Cyan
    [content addSubview:_gradStartWell];

    NSTextField *lblEnd = [self labelWithText:CIYL(@"End color:") fontSize:12 weight:NSFontWeightMedium];
    lblEnd.frame = NSMakeRect(24, 122, 90, 20);
    [content addSubview:lblEnd];

    _gradEndWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(120, 120, 48, 26)];
    _gradEndWell.color = [NSColor colorWithSRGBRed:1.0 green:0.0 blue:0.8 alpha:1.0]; // Magenta
    [content addSubview:_gradEndWell];

    NSTextField *lblDir = [self labelWithText:CIYL(@"Direction:") fontSize:12 weight:NSFontWeightMedium];
    lblDir.frame = NSMakeRect(24, 82, 90, 20);
    [content addSubview:lblDir];

    _gradDirPopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(120, 80, 230, 26) pullsDown:NO];
    [_gradDirPopUp addItemWithTitle:CIYL(@"Horizontal (left → right)")];
    [_gradDirPopUp addItemWithTitle:CIYL(@"Vertical (top → bottom)")];
    [content addSubview:_gradDirPopUp];

    NSButton *applyBtn = [NSButton buttonWithTitle:CIYL(@"Apply to editor") target:self action:@selector(onApplyGradient:)];
    applyBtn.frame = NSMakeRect(190, 20, 166, 32);
    applyBtn.bezelStyle = NSBezelStyleRounded;
    applyBtn.keyEquivalent = @"\r";
    [content addSubview:applyBtn];

    NSButton *copyBtn = [NSButton buttonWithTitle:CIYL(@"Copy ciy_ctl command") target:self action:@selector(onCopyGradientCommand:)];
    copyBtn.frame = NSMakeRect(80, 20, 106, 32);
    copyBtn.bezelStyle = NSBezelStyleRounded;
    [content addSubview:copyBtn];

    NSButton *cancelBtn = [NSButton buttonWithTitle:CIYL(@"Cancel") target:self action:@selector(onCancelGradientSheet:)];
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

    BOOL changed = [_keyboardView applyKeyColors:newColors registerUndo:YES];
    if (_currentSelectedSlot >= 0 && _colorWell) {
        NSColor *c = [_keyboardView allKeyColors][@(_currentSelectedSlot)];
        if (c) _colorWell.color = c;
    }
    if (changed) {
        BOOL matches = [self canvasMatchesSavedProfile];
        [self setHasUnsavedEdits:!matches];
        [self updateButtonsState];

        NSString *profName = [self currentProfileName];
        if (matches) {
            _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● No unsaved changes (profile “%@”)"), profName];
            _statusLabel.textColor = [NSColor systemCyanColor];
        } else {
            _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Gradient applied (profile “%@” unsaved)"), profName];
            _statusLabel.textColor = [NSColor systemOrangeColor];
        }
    }

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
    _statusLabel.stringValue = CIYL(@"● Gradient command copied to clipboard");
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
    [[self userDefaults] setBool:enabled forKey:kBatteryMirrorEnabledKey];

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
    [[self userDefaults] setInteger:sender.selectedItem.tag forKey:kBatterySourceKey];
    [CIYBatteryMonitor sharedMonitor].preferredSource = (CIYBatterySourceType)sender.selectedItem.tag;
    if (_batteryMirrorCheckbox.state == NSControlStateValueOn) {
        [self updateBatteryDisplay];
    }
}

- (void)onBatteryTargetChanged:(NSPopUpButton *)sender {
    if (sender.selectedItem.tag == 3) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = CIYL(@"Custom battery key sequence");
        alert.informativeText = CIYL(@"Enter the matrix slots separated by commas (e.g. 14,9,15,21 for WASD or 88,83,89,95 for arrows):");
        NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
        input.stringValue = _customBatterySlots ?: @"14,9,15,21";
        alert.accessoryView = input;
        [alert addButtonWithTitle:CIYL(@"Apply")];
        [alert addButtonWithTitle:CIYL(@"Cancel")];
        if ([alert runModal] == NSAlertFirstButtonReturn) {
            NSString *val = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (val.length > 0) {
                _customBatterySlots = val;
                [[self userDefaults] setObject:_customBatterySlots forKey:kBatteryCustomSlotsKey];
            }
        }
    }
    [[self userDefaults] setInteger:sender.selectedItem.tag forKey:kBatteryTargetKey];
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
            if (ok || ![KeyboardBackend shared].lastOperationWasCancelled) {
                [self recordSendResult:ok];
            }
            if (ok) {
                NSString *profName = [self currentProfileName];
                if (self->_hasUnsavedEdits) {
                    self->_statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Preview “%@” (unsaved) on keyboard"), profName];
                    self->_statusLabel.textColor = [NSColor systemOrangeColor];
                } else {
                    self->_statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Profile “%@” active on keyboard"), profName];
                    self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
                }
            } else if (![KeyboardBackend shared].lastOperationWasCancelled) {
                self->_statusLabel.stringValue = CIYL(@"● Send failed (check USB)");
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
        if (!_isSleeping && _scheduler && !_scheduler.isRunning) {
            [_scheduler start];
        }
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
    _isSleeping = YES;
    _isWakingUp = YES;
    // Anstehende Auto-Switches und Hardware-Syncs stornieren, damit während des Suspend-Vorgangs keine Transfers laufen
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(executeAutoSwitch) object:nil];
    [self cancelHardwareSync];
    // Scheduler-Timer stoppen und invalidieren (AGENTS.md Regel 3)
    [_scheduler stop];
    [self dismissScheduleSheet];
    [self dismissTagSheet];
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
    _isSleeping = NO;
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
        if (self->_isSleeping) {
            [self updateConnectionStatus];
            return;
        }
        [self updateConnectionStatus];
        if (ok) {
            self->_wakeRetryCount = 0;
            // Prüfen, ob sich während des Ruhezustands das Zeitplan-Profil geändert hat (kein doppelter USB-Transfer!)
            BOOL wasStoppedForSleep = (self->_scheduler && !self->_scheduler.isRunning);
            BOOL profileChanged = NO;
            if (wasStoppedForSleep && self->_scheduler.globalEnabled && !self->_hasUnsavedEdits) {
                if (![self foregroundAppHasLinkedProfile]) {
                    NSString *targetId = [self->_scheduler activeScheduledProfileId];
                    if (targetId && ![targetId isEqualToString:self->_currentActiveProfileId]) {
                        for (NSUInteger i = 0; i < self->_profiles.count; i++) {
                            if ([self->_profiles[i][@"id"] isEqualToString:targetId]) {
                                [self activateProfileAtIndex:(NSInteger)i];
                                profileChanged = YES;
                                break;
                            }
                        }
                    }
                }
            }

            if (!profileChanged) {
                // Tastatur mit dem aktuell aktiven Profil synchronisieren
                [self sendCurrentProfileToHardware];
            }
            [self->_scheduler start];

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
        } else {
            // Unplugged-Wake (Retries erschöpft): Scheduler dennoch starten
            [self->_scheduler start];
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
    BOOL wasEdited = _hasUnsavedEdits;
    _hasUnsavedEdits = edited;
    [self.window setDocumentEdited:edited];
    if (wasEdited && !edited && _scheduleSwitchDeferredByEdits) {
        _scheduleSwitchDeferredByEdits = NO;
        [_scheduler reevaluate];
    }
}

- (void)updateConnectionStatus {
    BOOL connected = [KeyboardBackend shared].isConnected;
    if (connected) {
        _statusLabel.stringValue = CIYL(@"● CIY X79 connected (VID: 0x258A / PID: 0x0049)");
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
    } else {
        _statusLabel.stringValue = CIYL(@"● Keyboard not found (check USB)");
        _statusLabel.textColor = [NSColor systemRedColor];
    }
    [self updateStatusItemMenu];
    if (_miniController) {
        [_miniController setConnected:connected];
    }
}

#pragma mark - Profil Export & Import

- (void)onExportProfiles:(id)sender {
    (void)sender;
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.title = CIYL(@"Export CIY X79 lighting profiles");
    panel.prompt = CIYL(@"Export");
    panel.nameFieldStringValue = @"CIYX79_Profile.json";
    panel.allowedContentTypes = @[UTTypeJSON];

    NSButton *includeAppsCheckbox = [NSButton checkboxWithTitle:CIYL(@"Also export app links & schedules (auto-trigger)") target:nil action:nil];
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
                    [copyProf removeObjectForKey:kCIYProfileScheduleEnabledKey];
                    [copyProf removeObjectForKey:kCIYProfileScheduleHourKey];
                    [copyProf removeObjectForKey:kCIYProfileScheduleMinuteKey];
                    [copyProf removeObjectForKey:kCIYProfileScheduleDaysMaskKey];
                }
                [CIYProfileTags sanitizeTagsInProfile:copyProf];
                [exportList addObject:copyProf];
            }

            NSError *err = nil;
            NSData *jsonData = [NSJSONSerialization dataWithJSONObject:exportList
                                                               options:NSJSONWritingPrettyPrinted
                                                                 error:&err];
            if (jsonData && !err) {
                BOOL ok = [jsonData writeToURL:panel.URL atomically:YES];
                if (ok) {
                    self->_statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● %lu profile(s) exported"), (unsigned long)exportList.count];
                    self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
                    return;
                }
            }
            NSAlert *errAlert = [[NSAlert alloc] init];
            errAlert.messageText = CIYL(@"Export failed");
            errAlert.informativeText = err.localizedDescription ?: CIYL(@"The file could not be written.");
            [errAlert beginSheetModalForWindow:self.window completionHandler:nil];
        }
    }];
}

- (void)onImportProfiles:(id)sender {
    (void)sender;
    if (_profiles.count >= kMaxProfilesCount) {
        [self showImportError:[NSString stringWithFormat:CIYL(@"The limit of %lu profiles has already been reached. Please delete unneeded profiles first."), (unsigned long)kMaxProfilesCount]];
        return;
    }

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = CIYL(@"Import CIY X79 lighting profiles");
    panel.prompt = CIYL(@"Import");
    panel.allowedContentTypes = @[UTTypeJSON];
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK && panel.URL) {
            if (self->_profiles.count >= kMaxProfilesCount) {
                [self showImportError:[NSString stringWithFormat:CIYL(@"The limit of %lu profiles has already been reached."), (unsigned long)kMaxProfilesCount]];
                return;
            }
            size_t rawSize = 0;
            CIYValidationResult valRes;
            char *rawBuf = ciy_read_text_file_with_result([panel.URL.path fileSystemRepresentation], &rawSize, &valRes);
            if (!rawBuf || rawSize == 0 || rawSize > 512 * 1024) {
                if (rawBuf) free(rawBuf);
                NSLog(@"CIY X79: import read failed (%s): %s", valRes.error_field, valRes.error_message);
                [self showImportError:CIYLocalizedFileReadErrorMessage(valRes.read_error)];
                return;
            }
            NSData *data = [NSData dataWithBytesNoCopy:rawBuf length:rawSize freeWhenDone:YES];
            NSError *err = nil;
            id json = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:&err];
            if (!json || err) {
                [self showImportError:CIYL(@"Invalid JSON format.")];
                return;
            }

            NSArray *items = nil;
            if ([json isKindOfClass:[NSArray class]]) {
                items = json;
            } else if ([json isKindOfClass:[NSDictionary class]]) {
                items = @[json];
            } else {
                [self showImportError:CIYL(@"JSON contains no profiles.")];
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
                    NSString *name = CIYL(@"Imported profile");
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
                        if (dict[kCIYProfileScheduleEnabledKey] != nil) {
                            newProf[kCIYProfileScheduleEnabledKey] = dict[kCIYProfileScheduleEnabledKey];
                            newProf[kCIYProfileScheduleHourKey] = dict[kCIYProfileScheduleHourKey];
                            newProf[kCIYProfileScheduleMinuteKey] = dict[kCIYProfileScheduleMinuteKey];
                            newProf[kCIYProfileScheduleDaysMaskKey] = dict[kCIYProfileScheduleDaysMaskKey];
                        }
                        [CIYProfileScheduler sanitizeScheduleInProfile:newProf];

                        id rawTags = dict[kCIYProfileTagsKey];
                        NSArray<NSString *> *cleanTags = [CIYProfileTags sanitizeTags:rawTags];
                        if (cleanTags.count > 0) {
                            newProf[kCIYProfileTagsKey] = cleanTags;
                        }

                        [self->_profiles addObject:newProf];
                        lastId = newId;
                        importedCount++;
                    }
                }
            }

            if (importedCount > 0) {
                self->_currentActiveProfileId = lastId;
                self->_selectedFilterTag = nil;
                [self saveProfilesToDefaults];
                [self refreshProfileFilterPopUp];
                [self refreshProfilesPopUp];
                [self selectPopUpForProfileIndex:(NSInteger)(self->_profiles.count - 1)];
                [self loadProfileIntoView:self->_profiles.lastObject];
                [self->_keyboardView clearUndoHistory];
                self->_scheduleSwitchDeferredByEdits = NO;
                [self setHasUnsavedEdits:NO];
                [self updateButtonsState];
                [self sendCurrentProfileToHardware];
                [self->_scheduler rescheduleTimerOnly];
                [self updateStatusItemMenu];

                self->_statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● %lu profile(s) imported successfully"), (unsigned long)importedCount];
                self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
                if (limitReached) {
                    [self showImportError:[NSString stringWithFormat:CIYL(@"%lu profile(s) were imported. Additional profiles were skipped because the maximum of %lu profiles has been reached."), (unsigned long)importedCount, (unsigned long)kMaxProfilesCount]];
                }
            } else {
                [self showImportError:CIYL(@"No valid lighting profiles found in the file.")];
            }
        }
    }];
}

- (void)showImportError:(NSString *)msg {
    NSAlert *errAlert = [[NSAlert alloc] init];
    errAlert.messageText = CIYL(@"Import failed");
    errAlert.informativeText = msg;
    [errAlert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)onResetProfiles:(id)sender {
    (void)sender;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = CIYL(@"Reset profiles to factory defaults?");
    alert.informativeText = CIYL(@"All custom profiles will be replaced by the built-in default profiles.");
    [alert addButtonWithTitle:CIYL(@"Reset")];
    [alert addButtonWithTitle:CIYL(@"Cancel")];
    alert.alertStyle = NSAlertStyleWarning;

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn) {
            [self->_profiles removeAllObjects];
            [self addDefaultProfiles];
            self->_currentActiveProfileId = self->_profiles.firstObject[@"id"];
            self->_fallbackProfileId = (self->_profiles.count > 6) ? self->_profiles[6][@"id"] : self->_profiles[0][@"id"];
            [[self userDefaults] setObject:self->_fallbackProfileId forKey:kFallbackProfileIdKey];
            self->_selectedFilterTag = nil;
            [self saveProfilesToDefaults];
            [self refreshProfileFilterPopUp];
            [self refreshProfilesPopUp];
            [self selectPopUpForProfileIndex:0];
            [self loadProfileIntoView:self->_profiles.firstObject];
            [self->_keyboardView clearUndoHistory];
            self->_scheduleSwitchDeferredByEdits = NO;
            [self setHasUnsavedEdits:NO];
            [self updateButtonsState];
            [self sendCurrentProfileToHardware];
            [self->_scheduler rescheduleTimerOnly];

            self->_statusLabel.stringValue = CIYL(@"● Profiles reset to defaults");
            self->_statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        }
    }];
}

- (void)onOpenHelp:(id)sender {
    (void)sender;
    BOOL german = ([CIYLocalization effectiveLanguage] == CIYLanguageGerman);
    NSArray<NSString *> *candidates = german
        ? @[@"README", @"README_EN"]
        : @[@"README_EN", @"README"];

    NSString *readmePath = nil;
    for (NSString *name in candidates) {
        readmePath = [[NSBundle mainBundle] pathForResource:name ofType:@"md"];
        if (readmePath) break;
    }
    if (!readmePath) {
        readmePath = [[[NSBundle mainBundle] resourcePath]
                      stringByAppendingPathComponent:@"README.md"];
    }
    BOOL opened = readmePath ? [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:readmePath]] : NO;
    if (!opened) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = CIYL(@"CIY X79 RGB Control (macOS)");
        alert.informativeText = CIYL(@"Further information and hardware details can be found in the included README.md documentation.");
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
    }
}

#pragma mark - macOS Menüleiste

- (void)setupMainMenu {
    NSMenu *mainMenu = [[NSMenu alloc] init];

    // 1. App Menü (CIY X79 Control)
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"CIY X79 Control"];
    [appMenu addItemWithTitle:CIYL(@"About CIY X79 Control") action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *hideItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Hide CIY X79 Control") action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:hideItem];

    NSMenuItem *hideOthers = [[NSMenuItem alloc] initWithTitle:CIYL(@"Hide Others") action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    hideOthers.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    [appMenu addItem:hideOthers];

    [appMenu addItemWithTitle:CIYL(@"Show All") action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *languageItem = [[NSMenuItem alloc] initWithTitle:@"Language / Sprache" action:nil keyEquivalent:@""];
    languageItem.tag = kLanguageMenuTag;
    languageItem.submenu = [self buildLanguageMenu];
    [appMenu addItem:languageItem];

    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Quit CIY X79 Control") action:@selector(terminate:) keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    appMenuItem.submenu = appMenu;
    [mainMenu addItem:appMenuItem];

    // 2. Ablage (File)
    NSMenuItem *fileMenuItem = [[NSMenuItem alloc] init];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:CIYL(@"File")];
    [fileMenu addItemWithTitle:CIYL(@"New profile...") action:@selector(onNewProfile:) keyEquivalent:@"n"];
    [fileMenu addItemWithTitle:CIYL(@"Save & send profile") action:@selector(onSaveAndSendProfile:) keyEquivalent:@"s"];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:CIYL(@"Export profiles...") action:@selector(onExportProfiles:) keyEquivalent:@"e"];
    [fileMenu addItemWithTitle:CIYL(@"Import profiles...") action:@selector(onImportProfiles:) keyEquivalent:@"o"];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:CIYL(@"Close window") action:@selector(performClose:) keyEquivalent:@"w"];
    fileMenuItem.submenu = fileMenu;
    [mainMenu addItem:fileMenuItem];

    // 3. Bearbeiten (Edit)
    NSMenuItem *editMenuItem = [[NSMenuItem alloc] init];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:CIYL(@"Edit")];

    NSMenuItem *undoItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Undo") action:@selector(undo:) keyEquivalent:@"z"];
    undoItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    [editMenu addItem:undoItem];

    NSMenuItem *redoItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Redo") action:@selector(redo:) keyEquivalent:@"z"];
    redoItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [editMenu addItem:redoItem];

    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:CIYL(@"Fill all keys...") action:@selector(onFillAllKeys:) keyEquivalent:@""];
    editMenuItem.submenu = editMenu;
    [mainMenu addItem:editMenuItem];

    // 4. Tastatur (Hardware)
    NSMenuItem *hwMenuItem = [[NSMenuItem alloc] init];
    NSMenu *hwMenu = [[NSMenu alloc] initWithTitle:CIYL(@"Keyboard")];
    [hwMenu addItemWithTitle:CIYL(@"Send active profile to keyboard") action:@selector(onApplyProfileClicked:) keyEquivalent:@"r"];
    [hwMenu addItemWithTitle:CIYL(@"Reconnect") action:@selector(onReconnect:) keyEquivalent:@"u"];
    [hwMenu addItem:[NSMenuItem separatorItem]];
    [hwMenu addItemWithTitle:CIYL(@"Toggle auto-trigger on app focus") action:@selector(toggleAutoTriggerMenu:) keyEquivalent:@"t"];
    [hwMenu addItemWithTitle:CIYL(@"Link app to current profile...") action:@selector(onAssignApp:) keyEquivalent:@""];
    [hwMenu addItemWithTitle:CIYL(@"Remove app link") action:@selector(onClearApp:) keyEquivalent:@""];
    [hwMenu addItemWithTitle:CIYL(@"Set current profile as default fallback") action:@selector(onSetDefaultFallback:) keyEquivalent:@""];
    [hwMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *scheduleItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Schedule profile...") action:@selector(onOpenScheduleSheet:) keyEquivalent:@"t"];
    scheduleItem.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    scheduleItem.target = self;
    [hwMenu addItem:scheduleItem];
    _scheduleActiveMenuItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Profile scheduling active") action:@selector(toggleScheduleActiveMenu:) keyEquivalent:@""];
    _scheduleActiveMenuItem.target = self;
    _scheduleActiveMenuItem.state = (_scheduler && _scheduler.globalEnabled) ? NSControlStateValueOn : NSControlStateValueOff;
    [hwMenu addItem:_scheduleActiveMenuItem];

    NSMenuItem *quickSwitchItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Switch profile") action:nil keyEquivalent:@""];
    _quickSwitchMenu = [[NSMenu alloc] initWithTitle:CIYL(@"Switch profile")];
    quickSwitchItem.submenu = _quickSwitchMenu;
    [hwMenu addItem:[NSMenuItem separatorItem]];
    [hwMenu addItem:quickSwitchItem];

    hwMenuItem.submenu = hwMenu;
    [mainMenu addItem:hwMenuItem];

    // 5. Fenster (Window)
    NSMenuItem *windowMenuItem = [[NSMenuItem alloc] init];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:CIYL(@"Window")];
    NSMenuItem *openWinItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Open main window") action:@selector(onOpenMainWindow:) keyEquivalent:@""];
    openWinItem.target = self;
    [windowMenu addItem:openWinItem];
    NSMenuItem *miniModeItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Mini Mode") action:@selector(onToggleMiniMode:) keyEquivalent:@"m"];
    miniModeItem.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    miniModeItem.target = self;
    [windowMenu addItem:miniModeItem];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItemWithTitle:CIYL(@"Minimize") action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:CIYL(@"Zoom") action:@selector(performZoom:) keyEquivalent:@""];
    windowMenuItem.submenu = windowMenu;
    [mainMenu addItem:windowMenuItem];
    [NSApp setWindowsMenu:windowMenu];

    // 6. Hilfe (Help)
    NSMenuItem *helpMenuItem = [[NSMenuItem alloc] init];
    NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:CIYL(@"Help")];
    [helpMenu addItemWithTitle:CIYL(@"Documentation & guide") action:@selector(onOpenHelp:) keyEquivalent:@"?"];
    helpMenuItem.submenu = helpMenu;
    [mainMenu addItem:helpMenuItem];
    [NSApp setHelpMenu:helpMenu];

    [NSApp setMainMenu:mainMenu];
    [self refreshLanguageMenuStates];
    [self updateQuickSwitchMenu];
}

- (NSMenu *)buildLanguageMenu {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Language / Sprache"];

    NSMenuItem *englishItem = [[NSMenuItem alloc] initWithTitle:@"English"
                                                         action:@selector(onSelectLanguage:)
                                                  keyEquivalent:@""];
    englishItem.tag = CIYLanguageEnglish;
    englishItem.target = self;
    [menu addItem:englishItem];

    NSMenuItem *germanItem = [[NSMenuItem alloc] initWithTitle:@"Deutsch"
                                                        action:@selector(onSelectLanguage:)
                                                 keyEquivalent:@""];
    germanItem.tag = CIYLanguageGerman;
    germanItem.target = self;
    [menu addItem:germanItem];

    CIYLanguage preferred = [CIYLocalization preferredLanguage];
    englishItem.state = (preferred == CIYLanguageEnglish) ? NSControlStateValueOn : NSControlStateValueOff;
    germanItem.state = (preferred == CIYLanguageGerman) ? NSControlStateValueOn : NSControlStateValueOff;
    return menu;
}

- (void)refreshLanguageMenuStates {
    CIYLanguage preferred = [CIYLocalization preferredLanguage];
    CIYUpdateLanguageMenuStates([NSApp mainMenu], preferred);
    CIYUpdateLanguageMenuStates(_statusItem.menu, preferred);
}

- (void)loadLanguagePreference {
    NSUserDefaults *defaults = [self userDefaults];
    NSString *storedCode = [defaults stringForKey:CIYLanguagePreferenceKey];
    NSArray *savedProfiles = [defaults arrayForKey:kCustomProfilesKey];
    BOOL hasProfiles = [savedProfiles isKindOfClass:[NSArray class]] && savedProfiles.count > 0;

    CIYLanguage language = [CIYLocalization resolveLanguageFromStoredCode:storedCode
                                                      hasStoredProfiles:hasProfiles];
    [CIYLocalization setPreferredLanguage:language];
    [CIYLocalization loadEffectiveLanguage];
}

- (void)onSelectLanguage:(NSMenuItem *)sender {
    CIYLanguage selected = (sender.tag == CIYLanguageGerman) ? CIYLanguageGerman : CIYLanguageEnglish;
    if (selected == [CIYLocalization preferredLanguage]) {
        return;
    }

    [CIYLocalization setPreferredLanguage:selected];
    [self refreshLanguageMenuStates];

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = CIYL(@"Language change requires restart");
    NSString *info = CIYL(@"The UI language will change after restarting the app.");
    if (_hasUnsavedEdits) {
        info = [info stringByAppendingFormat:@"\n\n%@", CIYL(@"Unsaved key edits will be lost if you restart now.")];
    }
    alert.informativeText = info;
    [alert addButtonWithTitle:CIYL(@"OK")];

    if (self.window.isVisible) {
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
    } else {
        [alert runModal];
    }
}

#pragma mark - Menüleisten-Statusobjekt (Tray Icon)

- (void)refreshStatusItemIcon {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self refreshStatusItemIcon];
        });
        return;
    }
    if (!_statusItem) return;

    NSString *name = [self currentProfileName];
    NSDictionary *currentProf = [self selectedProfile];

    NSColor *profColor = [CIYStatusItemPresenter dominantColorFromSlots:currentProf[@"slots"]];
    BOOL connected = [KeyboardBackend shared].isConnected;

    NSImage *icon = [CIYStatusItemPresenter iconForProfileName:name
                                                  profileColor:profColor
                                                     connected:connected];
    _statusItem.button.image = icon;
    _statusItem.button.title = @"";
    _statusItem.button.toolTip = [CIYStatusItemPresenter toolTipForProfileName:name connected:connected];
    _statusItem.button.accessibilityLabel = [CIYStatusItemPresenter accessibilityDescriptionForProfileName:name connected:connected];
}

- (void)setupStatusItem {
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    [self refreshStatusItemIcon];
    [self updateStatusItemMenu];
}

- (void)updateQuickSwitchMenu {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateQuickSwitchMenu];
        });
        return;
    }
    if (!_quickSwitchMenu) return;

    [_quickSwitchMenu removeAllItems];
    for (NSUInteger i = 0; i < _profiles.count; i++) {
        NSDictionary *prof = _profiles[i];
        NSString *name = prof[@"name"] ?: [NSString stringWithFormat:CIYL(@"Profile %lu"), (unsigned long)(i + 1)];
        NSString *profId = prof[@"id"];
        BOOL isActive = (profId && [profId isEqualToString:_currentActiveProfileId]);

        NSString *itemTitle = name;
        NSString *appName = prof[@"appName"];
        NSString *schedBadge = [CIYProfileScheduler shortScheduleSummaryForProfile:prof];
        if (appName.length > 0) {
            if (schedBadge) {
                itemTitle = [NSString stringWithFormat:@"%@  [%@]  (⏰ %@)", name, appName, schedBadge];
            } else {
                itemTitle = [NSString stringWithFormat:@"%@  [%@]", name, appName];
            }
        } else if (_fallbackProfileId && [profId isEqualToString:_fallbackProfileId]) {
            if (schedBadge) {
                itemTitle = [NSString stringWithFormat:@"%@  [Fallback]  (⏰ %@)", name, schedBadge];
            } else {
                itemTitle = [NSString stringWithFormat:@"%@  [Fallback]", name];
            }
        } else if (schedBadge) {
            itemTitle = [NSString stringWithFormat:@"%@  (⏰ %@)", name, schedBadge];
        }

        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:itemTitle action:@selector(onStatusMenuSelectProfile:) keyEquivalent:@""];
        item.target = self;
        item.tag = (NSInteger)i;
        item.state = isActive ? NSControlStateValueOn : NSControlStateValueOff;

        NSString *keyEq = [CIYStatusItemPresenter keyEquivalentForIndex:(NSInteger)i];
        if (keyEq.length > 0) {
            item.keyEquivalent = keyEq;
            item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        }

        [_quickSwitchMenu addItem:item];
    }
}

- (void)updateStatusItemMenu {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateStatusItemMenu];
        });
        return;
    }
    [self updateQuickSwitchMenu];
    if (!_statusItem) return;

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"CIY X79 Status"];

    // 1. Verbindungs-Status Header (nicht auswählbar)
    BOOL connected = [KeyboardBackend shared].isConnected;
    NSString *statusTitle = connected ? CIYL(@"CIY X79: Connected") : CIYL(@"CIY X79: Disconnected (check USB)");
    NSMenuItem *statusHeader = [[NSMenuItem alloc] initWithTitle:statusTitle action:nil keyEquivalent:@""];
    statusHeader.enabled = NO;
    [menu addItem:statusHeader];

    [menu addItem:[NSMenuItem separatorItem]];

    // 2. Fenster öffnen
    NSMenuItem *openWindowItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Open main window...") action:@selector(onOpenMainWindow:) keyEquivalent:@""];
    openWindowItem.target = self;
    [menu addItem:openWindowItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 3. Profile
    NSMenuItem *profHeader = [[NSMenuItem alloc] initWithTitle:CIYL(@"Profiles:") action:nil keyEquivalent:@""];
    profHeader.enabled = NO;
    [menu addItem:profHeader];

    for (NSUInteger i = 0; i < _profiles.count; i++) {
        NSDictionary *prof = _profiles[i];
        NSString *name = prof[@"name"] ?: [NSString stringWithFormat:CIYL(@"Profile %lu"), (unsigned long)(i + 1)];
        NSString *profId = prof[@"id"];
        BOOL isActive = (profId && [profId isEqualToString:_currentActiveProfileId]);

        NSString *itemTitle = name;
        NSString *appName = prof[@"appName"];
        NSString *schedBadge = [CIYProfileScheduler shortScheduleSummaryForProfile:prof];
        if (appName.length > 0) {
            if (schedBadge) {
                itemTitle = [NSString stringWithFormat:@"%@  [%@]  (⏰ %@)", name, appName, schedBadge];
            } else {
                itemTitle = [NSString stringWithFormat:@"%@  [%@]", name, appName];
            }
        } else if (_fallbackProfileId && [profId isEqualToString:_fallbackProfileId]) {
            if (schedBadge) {
                itemTitle = [NSString stringWithFormat:@"%@  [Fallback]  (⏰ %@)", name, schedBadge];
            } else {
                itemTitle = [NSString stringWithFormat:@"%@  [Fallback]", name];
            }
        } else if (schedBadge) {
            itemTitle = [NSString stringWithFormat:@"%@  (⏰ %@)", name, schedBadge];
        }

        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:itemTitle action:@selector(onStatusMenuSelectProfile:) keyEquivalent:@""];
        item.target = self;
        item.tag = (NSInteger)i;
        item.state = isActive ? NSControlStateValueOn : NSControlStateValueOff;

        NSString *keyEq = [CIYStatusItemPresenter keyEquivalentForIndex:(NSInteger)i];
        if (keyEq.length > 0) {
            item.keyEquivalent = keyEq;
            item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        }

        [menu addItem:item];
    }

    [menu addItem:[NSMenuItem separatorItem]];

    // 4. Auto-Trigger Toggle
    NSMenuItem *triggerItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"⚡ Auto-Trigger (app focus)") action:@selector(toggleAutoTriggerMenu:) keyEquivalent:@""];
    triggerItem.target = self;
    triggerItem.state = _autoTriggerEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:triggerItem];

    // 4b. Profile Scheduling Toggle
    NSMenuItem *schedItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"⏰ Profile scheduling active") action:@selector(toggleScheduleActiveMenu:) keyEquivalent:@""];
    schedItem.target = self;
    schedItem.state = (_scheduler && _scheduler.globalEnabled) ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:schedItem];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *languageItem = [[NSMenuItem alloc] initWithTitle:@"Language / Sprache" action:nil keyEquivalent:@""];
    languageItem.tag = kLanguageMenuTag;
    languageItem.submenu = [self buildLanguageMenu];
    [menu addItem:languageItem];

    [menu addItem:[NSMenuItem separatorItem]];

    // 5. Beenden
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:CIYL(@"Quit CIY X79 Control") action:@selector(terminate:) keyEquivalent:@"q"];
    quitItem.target = NSApp;
    [menu addItem:quitItem];

    _statusItem.menu = menu;
    [self refreshLanguageMenuStates];
    [self refreshStatusItemIcon];
}

- (void)onOpenMainWindow:(id)sender {
    (void)sender;
    if (self.window.isMiniaturized) {
        [self.window deminiaturize:nil];
    }
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)onToggleMiniMode:(id)sender {
    (void)sender;
    if (!_miniController) {
        _miniController = [[CIYMiniWindowController alloc] init];
        _miniController.delegate = self;
        [self syncMiniWindowProfiles];
        [_miniController setConnected:[KeyboardBackend shared].isConnected];
        if (_hasLastSendResult) {
            [_miniController noteSendResult:_lastSendOk atDate:_lastSendDate];
        }
    }
    [_miniController toggle];
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    if (menuItem.action == @selector(onStatusMenuSelectProfile:)) {
        NSInteger idx = menuItem.tag;
        if (idx < 0 || (NSUInteger)idx >= _profiles.count) return NO;
        NSDictionary *prof = _profiles[(NSUInteger)idx];
        NSString *profId = prof[@"id"];
        menuItem.state = (profId && [profId isEqualToString:_currentActiveProfileId]) ? NSControlStateValueOn : NSControlStateValueOff;
        if (menuItem.menu == _quickSwitchMenu && self.window.attachedSheet != nil) return NO;
        return YES;
    }
    if (menuItem.action == @selector(onToggleMiniMode:)) {
        menuItem.state = (_miniController && _miniController.isVisible) ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }
    if (menuItem.action == @selector(toggleScheduleActiveMenu:)) {
        menuItem.state = (_scheduler && _scheduler.globalEnabled) ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }
    if (menuItem.action == @selector(onOpenScheduleSheet:)) {
        if (self.window.attachedSheet != nil) return NO;
        NSInteger idx = [self selectedProfileIndex];
        return (idx >= 0 && (NSUInteger)idx < _profiles.count);
    }
    return YES;
}

#pragma mark - CIYMiniWindowControllerDelegate

- (void)miniWindowController:(CIYMiniWindowController *)controller didSelectProfileAtIndex:(NSInteger)index {
    (void)controller;
    [self activateProfileAtIndex:index];
}

- (void)miniWindowControllerDidRequestSend:(CIYMiniWindowController *)controller {
    (void)controller;
    [self onApplyProfileClicked:nil];
}

- (void)miniWindowControllerDidClose:(CIYMiniWindowController *)controller {
    (void)controller;
}

#pragma mark - Helpers

- (void)recordSendResult:(BOOL)ok {
    _hasLastSendResult = YES;
    _lastSendOk = ok;
    _lastSendDate = [NSDate date];
    if (_miniController) {
        [_miniController noteSendResult:ok atDate:_lastSendDate];
    }
}

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

#pragma mark - Profil-Zeitsteuerung UI & Sheet

- (void)dismissScheduleSheet {
    if (_scheduleSheet) {
        [self.window endSheet:_scheduleSheet];
        _scheduleSheet = nil;
    }
    _scheduleSheetProfileId = nil;
    _schedQuickButtons = nil;
}

- (void)onOpenScheduleSheet:(id)sender {
    (void)sender;
    NSInteger idx = [self selectedProfileIndex];
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;
    if (self.window.attachedSheet != nil) return;

    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *profName = prof[@"name"] ?: CIYL(@"Profile");

    if (_scheduleSheet) {
        [self dismissScheduleSheet];
    }

    _scheduleSheetProfileId = [prof[@"id"] copy];

    _scheduleSheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 440, 270)
                                                 styleMask:NSWindowStyleMaskTitled
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    _scheduleSheet.title = [NSString stringWithFormat:CIYL(@"Schedule profile “%@”"), profName];
    NSView *content = _scheduleSheet.contentView;

    _schedEnabledCheckbox = [NSButton checkboxWithTitle:CIYL(@"Enable schedule for this profile") target:self action:@selector(onSchedEnabledToggled:)];
    _schedEnabledCheckbox.frame = NSMakeRect(20, 226, 320, 22);
    _schedEnabledCheckbox.state = [prof[kCIYProfileScheduleEnabledKey] boolValue] ? NSControlStateValueOn : NSControlStateValueOff;
    [content addSubview:_schedEnabledCheckbox];

    NSTextField *lblTime = [self labelWithText:CIYL(@"Switch time:") fontSize:12 weight:NSFontWeightMedium];
    lblTime.frame = NSMakeRect(20, 188, 100, 20);
    [content addSubview:lblTime];

    _schedTimePicker = [[NSDatePicker alloc] initWithFrame:NSMakeRect(130, 185, 96, 26)];
    _schedTimePicker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    _schedTimePicker.datePickerElements = NSDatePickerElementFlagHourMinute;

    NSInteger hour = [prof[kCIYProfileScheduleHourKey] integerValue];
    NSInteger minute = [prof[kCIYProfileScheduleMinuteKey] integerValue];
    NSCalendar *cal = [NSCalendar currentCalendar];
    NSDateComponents *comp = [cal components:NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay fromDate:[NSDate date]];
    comp.hour = hour;
    comp.minute = minute;
    comp.second = 0;
    _schedTimePicker.dateValue = [cal dateFromComponents:comp] ?: [NSDate date];
    [content addSubview:_schedTimePicker];

    NSTextField *lblDays = [self labelWithText:CIYL(@"Active days:") fontSize:12 weight:NSFontWeightMedium];
    lblDays.frame = NSMakeRect(20, 148, 100, 20);
    [content addSubview:lblDays];

    NSButton *btnDaily = [NSButton buttonWithTitle:CIYL(@"Daily") target:self action:@selector(onSchedQuickSelectDaily:)];
    btnDaily.bezelStyle = NSBezelStyleRounded;
    [btnDaily sizeToFit];
    CGFloat wDaily = MAX(btnDaily.frame.size.width, 68.0);
    btnDaily.frame = NSMakeRect(124, 146, wDaily, 24);
    [content addSubview:btnDaily];

    CGFloat xWorkdays = NSMaxX(btnDaily.frame) + 6.0;
    NSButton *btnWorkdays = [NSButton buttonWithTitle:CIYL(@"Mon–Fri") target:self action:@selector(onSchedQuickSelectWorkdays:)];
    btnWorkdays.bezelStyle = NSBezelStyleRounded;
    [btnWorkdays sizeToFit];
    CGFloat wWorkdays = MAX(btnWorkdays.frame.size.width, 68.0);
    btnWorkdays.frame = NSMakeRect(xWorkdays, 146, wWorkdays, 24);
    [content addSubview:btnWorkdays];

    CGFloat xWeekend = NSMaxX(btnWorkdays.frame) + 6.0;
    NSButton *btnWeekend = [NSButton buttonWithTitle:CIYL(@"Weekend") target:self action:@selector(onSchedQuickSelectWeekend:)];
    btnWeekend.bezelStyle = NSBezelStyleRounded;
    [btnWeekend sizeToFit];
    CGFloat wWeekend = MAX(btnWeekend.frame.size.width, 68.0);
    btnWeekend.frame = NSMakeRect(xWeekend, 146, wWeekend, 24);
    [content addSubview:btnWeekend];

    _schedQuickButtons = @[btnDaily, btnWorkdays, btnWeekend];

    NSArray<NSString *> *dayTitles = @[
        CIYL(@"Mon"), CIYL(@"Tue"), CIYL(@"Wed"), CIYL(@"Thu"), CIYL(@"Fri"), CIYL(@"Sat"), CIYL(@"Sun")
    ];
    NSUInteger currentMask = [prof[kCIYProfileScheduleDaysMaskKey] unsignedIntegerValue];
    if (currentMask == 0) currentMask = CIYScheduleDaysAll;

    _schedDayCheckboxes = [NSMutableArray arrayWithCapacity:7];
    CGFloat dayX = 20;
    for (NSUInteger d = 0; d < 7; d++) {
        NSButton *cb = [NSButton checkboxWithTitle:dayTitles[d] target:self action:@selector(onSchedDayCheckboxChanged:)];
        cb.frame = NSMakeRect(dayX, 108, 52, 22);
        cb.state = (currentMask & (1 << d)) ? NSControlStateValueOn : NSControlStateValueOff;
        [content addSubview:cb];
        [_schedDayCheckboxes addObject:cb];
        dayX += 54;
    }

    _schedErrorLabel = [self labelWithText:@"" fontSize:11 weight:NSFontWeightRegular];
    _schedErrorLabel.frame = NSMakeRect(20, 68, 400, 20);
    _schedErrorLabel.textColor = [NSColor systemOrangeColor];
    [content addSubview:_schedErrorLabel];

    NSButton *saveBtn = [NSButton buttonWithTitle:CIYL(@"Save") target:self action:@selector(onSaveScheduleSheet:)];
    saveBtn.frame = NSMakeRect(314, 20, 106, 32);
    saveBtn.bezelStyle = NSBezelStyleRounded;
    saveBtn.keyEquivalent = @"\r";
    [content addSubview:saveBtn];

    NSButton *cancelBtn = [NSButton buttonWithTitle:CIYL(@"Cancel") target:self action:@selector(onCancelScheduleSheet:)];
    cancelBtn.frame = NSMakeRect(204, 20, 106, 32);
    cancelBtn.bezelStyle = NSBezelStyleRounded;
    cancelBtn.keyEquivalent = @"\e";
    [content addSubview:cancelBtn];

    [self updateSchedControlsState];
    if (self.window.isMiniaturized) {
        [self.window deminiaturize:nil];
    }
    if (!self.window.isVisible) {
        [self.window makeKeyAndOrderFront:nil];
    }
    [self.window beginSheet:_scheduleSheet completionHandler:nil];
}

- (void)updateSchedControlsState {
    BOOL enabled = (_schedEnabledCheckbox.state == NSControlStateValueOn);
    _schedTimePicker.enabled = enabled;
    for (NSButton *cb in _schedDayCheckboxes) {
        cb.enabled = enabled;
    }
    for (NSButton *btn in _schedQuickButtons) {
        btn.enabled = enabled;
    }
}

- (void)onSchedEnabledToggled:(NSButton *)sender {
    (void)sender;
    [self updateSchedControlsState];
    _schedErrorLabel.stringValue = @"";
}

- (void)onSchedQuickSelectDaily:(id)sender {
    (void)sender;
    for (NSButton *cb in _schedDayCheckboxes) {
        cb.state = NSControlStateValueOn;
    }
    _schedErrorLabel.stringValue = @"";
}

- (void)onSchedQuickSelectWorkdays:(id)sender {
    (void)sender;
    for (NSUInteger i = 0; i < _schedDayCheckboxes.count && i < 7; i++) {
        _schedDayCheckboxes[i].state = (i < 5) ? NSControlStateValueOn : NSControlStateValueOff;
    }
    _schedErrorLabel.stringValue = @"";
}

- (void)onSchedQuickSelectWeekend:(id)sender {
    (void)sender;
    for (NSUInteger i = 0; i < _schedDayCheckboxes.count && i < 7; i++) {
        _schedDayCheckboxes[i].state = (i >= 5) ? NSControlStateValueOn : NSControlStateValueOff;
    }
    _schedErrorLabel.stringValue = @"";
}

- (void)onSchedDayCheckboxChanged:(NSButton *)sender {
    (void)sender;
    _schedErrorLabel.stringValue = @"";
}

- (void)onSaveScheduleSheet:(id)sender {
    (void)sender;
    if (!_scheduleSheetProfileId) {
        [self onCancelScheduleSheet:nil];
        return;
    }

    NSUInteger targetIdx = NSNotFound;
    for (NSUInteger i = 0; i < _profiles.count; i++) {
        if ([_profiles[i][@"id"] isEqualToString:_scheduleSheetProfileId]) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx == NSNotFound) {
        [self onCancelScheduleSheet:nil];
        return;
    }

    BOOL enabled = (_schedEnabledCheckbox.state == NSControlStateValueOn);
    NSUInteger mask = 0;
    for (NSUInteger d = 0; d < _schedDayCheckboxes.count && d < 7; d++) {
        if (_schedDayCheckboxes[d].state == NSControlStateValueOn) {
            mask |= (1 << d);
        }
    }

    if (enabled && mask == 0) {
        _schedErrorLabel.stringValue = CIYL(@"Please select at least one day of the week.");
        return;
    }

    [_scheduleSheet makeFirstResponder:nil];

    NSCalendar *cal = [NSCalendar currentCalendar];
    NSDateComponents *comp = [cal components:NSCalendarUnitHour | NSCalendarUnitMinute fromDate:_schedTimePicker.dateValue];
    NSInteger hour = comp.hour;
    NSInteger minute = comp.minute;

    NSMutableDictionary *prof = _profiles[targetIdx];
    prof[kCIYProfileScheduleEnabledKey] = @(enabled);
    prof[kCIYProfileScheduleHourKey] = @(hour);
    prof[kCIYProfileScheduleMinuteKey] = @(minute);
    prof[kCIYProfileScheduleDaysMaskKey] = @(mask);

    NSString *selectedProfileId = nil;
    NSInteger currentProfIdx = [self selectedProfileIndex];
    if (currentProfIdx >= 0 && (NSUInteger)currentProfIdx < _profiles.count) {
        selectedProfileId = _profiles[(NSUInteger)currentProfIdx][@"id"];
    }

    [self saveProfilesToDefaults];
    [self refreshProfilesPopUp];

    if (selectedProfileId) {
        for (NSUInteger i = 0; i < _profiles.count; i++) {
            if ([_profiles[i][@"id"] isEqualToString:selectedProfileId]) {
                [self selectPopUpForProfileIndex:(NSInteger)i];
                break;
            }
        }
    }
    [self updateButtonsState];
    [self updateStatusItemMenu];

    [self dismissScheduleSheet];

    NSString *name = prof[@"name"] ?: CIYL(@"Profile");
    if (enabled) {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Schedule saved for “%@” (%02ld:%02ld)"), name, (long)hour, (long)minute];
    } else {
        _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Schedule disabled for “%@”"), name];
    }
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];

    [_scheduler reevaluate];
}

- (void)onCancelScheduleSheet:(id)sender {
    (void)sender;
    [self dismissScheduleSheet];
}

- (void)toggleScheduleActiveMenu:(id)sender {
    (void)sender;
    BOOL newState = !_scheduler.globalEnabled;
    _scheduler.globalEnabled = newState;
    [[self userDefaults] setBool:newState forKey:kCIYScheduleEnabledDefaultsKey];
    [[self userDefaults] synchronize];

    [self updateStatusItemMenu];
    [self refreshScheduleMenuStates];

    if (newState) {
        _statusLabel.stringValue = CIYL(@"● Profile scheduling active");
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
        [_scheduler reevaluate];
    } else {
        _statusLabel.stringValue = CIYL(@"● Profile scheduling disabled");
        _statusLabel.textColor = [NSColor secondaryLabelColor];
        [_scheduler reevaluate];
    }
}

- (void)refreshScheduleMenuStates {
    if (_scheduleActiveMenuItem) {
        _scheduleActiveMenuItem.state = (_scheduler && _scheduler.globalEnabled) ? NSControlStateValueOn : NSControlStateValueOff;
    }
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

#pragma mark - Profil-Tags UI & Sheet

- (void)dismissTagSheet {
    if (_tagSheet) {
        [self.window endSheet:_tagSheet];
        _tagSheet = nil;
    }
    _tagSheetProfileId = nil;
    _tagSuggestedCheckboxes = nil;
    _tagCustomField = nil;
    _tagErrorLabel = nil;
}

- (void)onOpenTagSheet:(id)sender {
    (void)sender;
    NSInteger idx = [self selectedProfileIndex];
    if (idx < 0 || (NSUInteger)idx >= _profiles.count) return;
    if (self.window.attachedSheet != nil) return;

    NSMutableDictionary *prof = _profiles[(NSUInteger)idx];
    NSString *profName = prof[@"name"] ?: CIYL(@"Profile");

    if (_tagSheet) {
        [self dismissTagSheet];
    }

    _tagSheetProfileId = [prof[@"id"] copy];

    _tagSheet = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 440, 260)
                                            styleMask:NSWindowStyleMaskTitled
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    _tagSheet.title = [NSString stringWithFormat:CIYL(@"Edit tags for “%@”"), profName];
    NSView *content = _tagSheet.contentView;

    // Vorschlag-Tags Label
    NSTextField *lblSuggested = [self labelWithText:CIYL(@"Suggested tags:") fontSize:12 weight:NSFontWeightMedium];
    lblSuggested.frame = NSMakeRect(20, 218, 300, 18);
    [content addSubview:lblSuggested];

    // Aktuell gesetzte Tags des Profils
    NSArray<NSString *> *existingTags = prof[kCIYProfileTagsKey];
    NSMutableSet<NSString *> *existingKeys = [NSMutableSet set];
    NSMutableArray<NSString *> *customTagList = [NSMutableArray array];

    NSArray<NSString *> *suggestions = [CIYProfileTags suggestedTags];
    NSMutableSet<NSString *> *suggKeys = [NSMutableSet set];
    for (NSString *sugg in suggestions) {
        [suggKeys addObject:[CIYProfileTags normalizedTagKey:sugg]];
    }

    if ([existingTags isKindOfClass:[NSArray class]]) {
        for (NSString *t in existingTags) {
            NSString *k = [CIYProfileTags normalizedTagKey:t];
            [existingKeys addObject:k];
            if (![suggKeys containsObject:k]) {
                [customTagList addObject:t];
            }
        }
    }

    // Checkboxen für Vorschläge
    _tagSuggestedCheckboxes = [NSMutableArray arrayWithCapacity:suggestions.count];
    CGFloat xPos = 20;
    CGFloat yPos = 188;
    for (NSUInteger i = 0; i < suggestions.count; i++) {
        NSString *suggName = suggestions[i];
        NSButton *cb = [NSButton checkboxWithTitle:suggName target:nil action:nil];
        cb.frame = NSMakeRect(xPos, yPos, 90, 20);
        NSString *k = [CIYProfileTags normalizedTagKey:suggName];
        cb.state = [existingKeys containsObject:k] ? NSControlStateValueOn : NSControlStateValueOff;
        [content addSubview:cb];
        [_tagSuggestedCheckboxes addObject:cb];
        xPos += 98;
    }

    // Eigene Tags Label
    NSTextField *lblCustom = [self labelWithText:CIYL(@"Custom tags (comma-separated):") fontSize:12 weight:NSFontWeightMedium];
    lblCustom.frame = NSMakeRect(20, 150, 400, 18);
    [content addSubview:lblCustom];

    // Textfeld für eigene Tags
    _tagCustomField = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 120, 400, 24)];
    _tagCustomField.placeholderString = CIYL(@"e.g. Coding, Cyberpunk, CS:GO");
    _tagCustomField.stringValue = [customTagList componentsJoinedByString:@", "];
    [content addSubview:_tagCustomField];

    // Info-Hinweis
    NSTextField *lblInfo = [self labelWithText:[NSString stringWithFormat:CIYL(@"Max. %lu tags, max. %lu characters per tag"), (unsigned long)kCIYMaxTagsPerProfile, (unsigned long)kCIYMaxTagLength]
                                      fontSize:11
                                        weight:NSFontWeightRegular];
    lblInfo.textColor = [NSColor secondaryLabelColor];
    lblInfo.frame = NSMakeRect(20, 96, 400, 16);
    [content addSubview:lblInfo];

    // Fehler-Label (für Validierungswarnungen)
    _tagErrorLabel = [self labelWithText:@"" fontSize:11 weight:NSFontWeightMedium];
    _tagErrorLabel.textColor = [NSColor systemRedColor];
    _tagErrorLabel.frame = NSMakeRect(20, 68, 400, 18);
    [content addSubview:_tagErrorLabel];

    // Buttons: Abbrechen & Speichern
    NSButton *cancelBtn = [NSButton buttonWithTitle:CIYL(@"Cancel") target:self action:@selector(onCancelTagSheet:)];
    cancelBtn.frame = NSMakeRect(220, 16, 96, 32);
    cancelBtn.bezelStyle = NSBezelStyleRounded;
    cancelBtn.keyEquivalent = @"\e"; // Esc
    [content addSubview:cancelBtn];

    NSButton *saveBtn = [NSButton buttonWithTitle:CIYL(@"Save") target:self action:@selector(onSaveTagSheet:)];
    saveBtn.frame = NSMakeRect(324, 16, 96, 32);
    saveBtn.bezelStyle = NSBezelStyleRounded;
    saveBtn.keyEquivalent = @"\r"; // Enter
    [content addSubview:saveBtn];

    [self.window beginSheet:_tagSheet completionHandler:nil];
}

- (void)onCancelTagSheet:(id)sender {
    (void)sender;
    [self dismissTagSheet];
}

- (void)onSaveTagSheet:(id)sender {
    (void)sender;
    if (!_tagSheetProfileId) {
        [self dismissTagSheet];
        return;
    }

    NSInteger targetIdx = -1;
    for (NSUInteger i = 0; i < _profiles.count; i++) {
        if ([_profiles[i][@"id"] isEqualToString:_tagSheetProfileId]) {
            targetIdx = (NSInteger)i;
            break;
        }
    }

    if (targetIdx < 0) {
        [self dismissTagSheet];
        return;
    }

    // 1. Ausgewählte Vorschläge sammeln
    NSMutableArray<NSString *> *allRaw = [NSMutableArray array];
    NSArray<NSString *> *suggestions = [CIYProfileTags suggestedTags];
    for (NSUInteger i = 0; i < _tagSuggestedCheckboxes.count && i < suggestions.count; i++) {
        if (_tagSuggestedCheckboxes[i].state == NSControlStateValueOn) {
            [allRaw addObject:suggestions[i]];
        }
    }

    // 2. Eigene Tags parsen (kommagetrennt)
    NSString *customInput = _tagCustomField.stringValue;
    if (customInput.length > 0) {
        NSArray<NSString *> *parts = [customInput componentsSeparatedByString:@","];
        for (NSString *p in parts) {
            NSString *trimmed = [p stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (trimmed.length > 0) {
                [allRaw addObject:trimmed];
            }
        }
    }

    // 3. Auf Duplikate und Limit prüfen
    NSMutableSet<NSString *> *seenKeys = [NSMutableSet set];
    NSUInteger uniqueCount = 0;
    for (NSString *raw in allRaw) {
        NSString *s = [CIYProfileTags sanitizeTag:raw];
        if (!s) continue;
        NSString *k = [CIYProfileTags normalizedTagKey:s];
        if (k.length > 0 && ![seenKeys containsObject:k]) {
            [seenKeys addObject:k];
            uniqueCount++;
        }
    }

    if (uniqueCount > kCIYMaxTagsPerProfile) {
        _tagErrorLabel.stringValue = [NSString stringWithFormat:CIYL(@"A maximum of %lu tags can be assigned."), (unsigned long)kCIYMaxTagsPerProfile];
        return;
    }

    // 4. Tags bereinigen und im Profil speichern
    NSArray<NSString *> *finalTags = [CIYProfileTags sanitizeTags:allRaw];
    NSMutableDictionary *prof = _profiles[(NSUInteger)targetIdx];
    if (finalTags.count > 0) {
        prof[kCIYProfileTagsKey] = finalTags;
    } else {
        [prof removeObjectForKey:kCIYProfileTagsKey];
    }

    NSString *selectedProfileId = _tagSheetProfileId;
    [self saveProfilesToDefaults];
    [self refreshProfileFilterPopUp];
    [self refreshProfilesPopUp];

    for (NSUInteger i = 0; i < _profiles.count; i++) {
        if ([_profiles[i][@"id"] isEqualToString:selectedProfileId]) {
            [self selectPopUpForProfileIndex:(NSInteger)i];
            break;
        }
    }

    [self updateButtonsState];
    [self dismissTagSheet];

    NSString *name = prof[@"name"] ?: CIYL(@"Profile");
    _statusLabel.stringValue = [NSString stringWithFormat:CIYL(@"● Tags updated for “%@”"), name];
    _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag {
    (void)sender;
    (void)flag;
    if (self.window.isMiniaturized) {
        [self.window deminiaturize:nil];
    }
    if (!self.window.isVisible) {
        [self.window makeKeyAndOrderFront:nil];
    }
    [NSApp activateIgnoringOtherApps:YES];
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    if (_miniController) {
        [_miniController teardown];
        _miniController = nil;
    }
    if (_scheduler) {
        [_scheduler stop];
    }
    [self dismissScheduleSheet];
    [self dismissTagSheet];
    [self cancelHardwareSync];
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

- (void)dealloc {
#if defined(CIY_ENABLE_TEST_HOOKS)
    if (_testDefaultsSuiteName && _userDefaults) {
        [_userDefaults removePersistentDomainForName:_testDefaultsSuiteName];
        _testDefaultsSuiteName = nil;
    }
#endif
}

#if defined(CIY_ENABLE_TEST_HOOKS)
- (instancetype)initForTestingWithKeyboardView:(nullable CIYKeyboardView *)keyboardView {
    self = [super init];
    if (self) {
        _testDefaultsSuiteName = [NSString stringWithFormat:@"com.julien.ciy-x79-control.test.%@", [NSUUID UUID].UUIDString];
        _userDefaults = [[NSUserDefaults alloc] initWithSuiteName:_testDefaultsSuiteName];
        [_userDefaults removePersistentDomainForName:_testDefaultsSuiteName];
        _hwQueue = dispatch_queue_create("com.julien.ciy-x79-control.test.hardware", DISPATCH_QUEUE_SERIAL);
        _keyboardView = keyboardView ?: [[CIYKeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
        _scheduler = [[CIYProfileScheduler alloc] init];
        _profiles = [NSMutableArray array];
        _batteryMirrorCheckbox = [[NSButton alloc] init];
        _batteryMirrorCheckbox.state = NSControlStateValueOff;
        _batteryStatusLabel = [[NSTextField alloc] init];
        _statusLabel = [[NSTextField alloc] init];
        _currentBrightness = 5;
    }
    return self;
}

- (void)ciy_triggerSystemWillSleep {
    [self onSystemWillSleep:[NSNotification notificationWithName:NSWorkspaceWillSleepNotification object:nil]];
}

- (void)ciy_triggerSystemDidWake {
    [self onSystemDidWake:[NSNotification notificationWithName:NSWorkspaceDidWakeNotification object:nil]];
}

- (void)ciy_triggerAppWillTerminate {
    [self applicationWillTerminate:[NSNotification notificationWithName:NSApplicationWillTerminateNotification object:nil]];
}

- (BOOL)ciy_isSleeping {
    return _isSleeping;
}

- (BOOL)ciy_isWakingUp {
    return _isWakingUp;
}

- (BOOL)ciy_isWakeSyncScheduled {
    return _wakeSyncScheduled;
}

- (void)ciy_scheduleHardwareSyncWithDelay:(NSTimeInterval)delay {
    [self cancelHardwareSync];
    _wakeRetryCount = 0;
    _wakeSyncScheduled = YES;
    [self performSelector:@selector(performHardwareSync) withObject:nil afterDelay:delay];
}

- (void)ciy_cancelHardwareSync {
    [self cancelHardwareSync];
}

- (void)ciy_setProfilesForTesting:(NSArray<NSDictionary *> *)profiles activeId:(nullable NSString *)activeId {
    _profiles = [NSMutableArray array];
    NSMutableArray<NSNumber *> *indices = [NSMutableArray array];
    for (NSUInteger i = 0; i < profiles.count; i++) {
        [_profiles addObject:[profiles[i] mutableCopy]];
        [indices addObject:@(i)];
    }
    _visibleProfileIndices = [indices copy];
    if (!_profilesPopUp) {
        _profilesPopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 100, 24) pullsDown:NO];
    }
    [_profilesPopUp removeAllItems];
    for (NSDictionary *p in _profiles) {
        [_profilesPopUp addItemWithTitle:p[@"name"] ?: @"Profile"];
    }
    _currentActiveProfileId = [activeId copy];
    NSInteger activeIdx = 0;
    if (activeId) {
        for (NSUInteger i = 0; i < _profiles.count; i++) {
            if ([_profiles[i][@"id"] isEqualToString:activeId]) {
                activeIdx = (NSInteger)i;
                break;
            }
        }
    }
    if (_profiles.count > 0) {
        [_profilesPopUp selectItemAtIndex:activeIdx];
    }
}

- (NSArray<NSDictionary *> *)ciy_profiles {
    return [_profiles copy];
}

- (NSUserDefaults *)ciy_userDefaults {
    return [self userDefaults];
}

- (void)ciy_saveCurrentProfile {
    [self onSaveAndSendProfile:nil];
}

- (void)ciy_waitForPendingHardwareOperations {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2.0];
    while (_sendInFlight && [deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    if (_hwQueue) {
        dispatch_sync(_hwQueue, ^{});
    }
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
}

- (void)ciy_setBatteryMirrorCheckboxState:(NSControlStateValue)state {
    _batteryMirrorCheckbox.state = state;
}

- (CIYKeyboardView *)ciy_keyboardView {
    return _keyboardView;
}

- (CIYProfileScheduler *)ciy_scheduler {
    return _scheduler;
}
#endif

@end
