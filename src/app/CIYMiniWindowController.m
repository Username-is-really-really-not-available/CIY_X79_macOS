#import "CIYMiniWindowController.h"
#import "CIYLocalization.h"

@interface CIYMiniPanel : NSPanel
@end

@implementation CIYMiniPanel
- (BOOL)canBecomeKeyWindow {
    return YES;
}
@end

@interface CIYMiniWindowController () <NSWindowDelegate> {
    CIYMiniPanel *_panel;
    NSPopUpButton *_profilePopUp;
    NSTextField *_statusLabel;
    NSTextField *_lastSendLabel;
    NSButton *_sendButton;

    NSArray<NSString *> *_cachedProfileNames;
    NSInteger _cachedSelectedIndex;
    BOOL _cachedConnected;
    BOOL _hasSendResult;
    BOOL _cachedSendOk;
    NSDate *_cachedSendDate;
    BOOL _isTearingDown;
}
@end

@implementation CIYMiniWindowController

- (instancetype)init {
    self = [super init];
    if (self) {
        _cachedProfileNames = @[];
        _cachedSelectedIndex = -1;
        _cachedConnected = NO;
        _hasSendResult = NO;
        _cachedSendOk = NO;
        _isTearingDown = NO;
    }
    return self;
}

- (void)dealloc {
    [self teardown];
}

#pragma mark - UI Erstellung & Konfiguration

- (void)ensurePanelCreated {
    if (_panel) return;

    NSRect frame = NSMakeRect(200, 200, 300, 135);
    _panel = [[CIYMiniPanel alloc] initWithContentRect:frame
                                             styleMask:(NSWindowStyleMaskTitled |
                                                        NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskUtilityWindow |
                                                        NSWindowStyleMaskNonactivatingPanel)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
    _panel.title = CIYL(@"Mini Mode");
    _panel.level = NSFloatingWindowLevel;
    _panel.floatingPanel = YES;
    _panel.hidesOnDeactivate = NO;
    _panel.releasedWhenClosed = NO;
    _panel.minSize = NSMakeSize(300, 135);
    _panel.maxSize = NSMakeSize(300, 135);
    _panel.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    [_panel setFrameAutosaveName:@"CIYX79MiniWindow"];
    _panel.delegate = self;

    NSView *content = _panel.contentView;
    content.wantsLayer = YES;
    content.layer.backgroundColor = [NSColor colorWithCalibratedWhite:0.11 alpha:1.0].CGColor;

    // 1. Profil-Auswahl
    NSTextField *profileLbl = [self labelWithText:CIYL(@"Profile:") fontSize:12 weight:NSFontWeightMedium];
    profileLbl.frame = NSMakeRect(14, 94, 52, 20);
    profileLbl.textColor = [NSColor secondaryLabelColor];
    [content addSubview:profileLbl];

    _profilePopUp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(68, 92, 218, 26) pullsDown:NO];
    _profilePopUp.target = self;
    _profilePopUp.action = @selector(onProfileSelected:);
    _profilePopUp.accessibilityLabel = CIYL(@"Select profile");
    [content addSubview:_profilePopUp];

    // PopUp mit gecachten Daten füllen
    [_profilePopUp removeAllItems];
    for (NSString *name in _cachedProfileNames) {
        [_profilePopUp.menu addItemWithTitle:name action:nil keyEquivalent:@""];
    }
    if (_cachedSelectedIndex >= 0 && _cachedSelectedIndex < _profilePopUp.numberOfItems) {
        [_profilePopUp selectItemAtIndex:_cachedSelectedIndex];
    } else {
        [_profilePopUp selectItemAtIndex:-1];
    }

    // 2. Hardware-Status
    _statusLabel = [self labelWithText:@"" fontSize:11 weight:NSFontWeightMedium];
    _statusLabel.frame = NSMakeRect(14, 62, 272, 18);
    [content addSubview:_statusLabel];
    [self updateStatusDisplay];

    // 3. Letztes Senden
    _lastSendLabel = [self labelWithText:@"" fontSize:10 weight:NSFontWeightRegular];
    _lastSendLabel.frame = NSMakeRect(14, 42, 272, 16);
    _lastSendLabel.textColor = [NSColor secondaryLabelColor];
    [content addSubview:_lastSendLabel];
    [self updateLastSendDisplay];

    // 4. "Senden (↵)" Button
    _sendButton = [[NSButton alloc] initWithFrame:NSMakeRect(186, 10, 100, 24)];
    _sendButton.title = CIYL(@"Send (↵)");
    _sendButton.bezelStyle = NSBezelStyleRounded;
    _sendButton.keyEquivalent = @"\r";
    _sendButton.target = self;
    _sendButton.action = @selector(onSendClicked:);
    [content addSubview:_sendButton];

    [self updateButtonsState];
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

#pragma mark - Status & Anzeige-Updates

- (void)updateStatusDisplay {
    if (!_statusLabel) return;
    if (_cachedConnected) {
        _statusLabel.stringValue = [NSString stringWithFormat:@"● %@", CIYL(@"Connected")];
        _statusLabel.textColor = [NSColor colorWithCalibratedRed:0.2 green:0.85 blue:0.3 alpha:1.0];
    } else {
        _statusLabel.stringValue = [NSString stringWithFormat:@"● %@", CIYL(@"Disconnected")];
        _statusLabel.textColor = [NSColor systemRedColor];
    }
}

- (void)updateLastSendDisplay {
    if (!_lastSendLabel) return;
    if (!_hasSendResult || !_cachedSendDate) {
        _lastSendLabel.stringValue = [NSString stringWithFormat:CIYL(@"Last sent: %@"), @"–"];
        _lastSendLabel.textColor = [NSColor secondaryLabelColor];
        return;
    }
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.timeStyle = NSDateFormatterMediumStyle;
    formatter.dateStyle = NSDateFormatterNoStyle;
    NSString *timeStr = [formatter stringFromDate:_cachedSendDate];
    if (_cachedSendOk) {
        _lastSendLabel.stringValue = [NSString stringWithFormat:CIYL(@"Last sent: %@"), timeStr];
        _lastSendLabel.textColor = [NSColor secondaryLabelColor];
    } else {
        _lastSendLabel.stringValue = [NSString stringWithFormat:CIYL(@"Last sent: %@"), timeStr];
        _lastSendLabel.textColor = [NSColor systemRedColor];
    }
}

- (void)updateButtonsState {
    if (_sendButton) {
        BOOL hasItems = (_cachedProfileNames.count > 0 &&
                         _cachedSelectedIndex >= 0 &&
                         (NSUInteger)_cachedSelectedIndex < _cachedProfileNames.count);
        _sendButton.enabled = hasItems;
    }
}

#pragma mark - Öffentliche Schnittstelle

- (BOOL)isVisible {
    return _panel != nil && _panel.isVisible;
}

- (void)setProfileNames:(NSArray<NSString *> *)names selectedIndex:(NSInteger)index {
    _cachedProfileNames = [names copy];
    _cachedSelectedIndex = index;
    if (_profilePopUp) {
        [_profilePopUp removeAllItems];
        for (NSString *name in names) {
            [_profilePopUp.menu addItemWithTitle:name action:nil keyEquivalent:@""];
        }
        if (index >= 0 && index < _profilePopUp.numberOfItems) {
            [_profilePopUp selectItemAtIndex:index];
        } else {
            [_profilePopUp selectItemAtIndex:-1];
        }
    }
    [self updateButtonsState];
}

- (void)setSelectedIndex:(NSInteger)index {
    _cachedSelectedIndex = index;
    if (_profilePopUp) {
        if (index >= 0 && index < _profilePopUp.numberOfItems) {
            [_profilePopUp selectItemAtIndex:index];
        } else {
            [_profilePopUp selectItemAtIndex:-1];
        }
    }
    [self updateButtonsState];
}

- (void)setConnected:(BOOL)connected {
    _cachedConnected = connected;
    [self updateStatusDisplay];
}

- (void)noteSendResult:(BOOL)ok atDate:(nullable NSDate *)date {
    _hasSendResult = YES;
    _cachedSendOk = ok;
    _cachedSendDate = date ?: [NSDate date];
    [self updateLastSendDisplay];
}

- (void)show {
    [self ensurePanelCreated];
    [_panel makeKeyAndOrderFront:nil];
}

- (void)hide {
    if (_panel) {
        [_panel orderOut:nil];
    }
}

- (void)toggle {
    if (self.isVisible) {
        [self hide];
    } else {
        [self show];
    }
}

- (void)teardown {
    if (_isTearingDown) return;
    _isTearingDown = YES;
    if (_panel) {
        _panel.delegate = nil;
        _sendButton.target = nil;
        _sendButton.action = NULL;
        _profilePopUp.target = nil;
        _profilePopUp.action = NULL;
        [_panel orderOut:nil];
        [_panel close];
        _panel = nil;
    }
    _profilePopUp = nil;
    _sendButton = nil;
    _statusLabel = nil;
    _lastSendLabel = nil;
    self.delegate = nil;
    _isTearingDown = NO;
}

#pragma mark - Aktionen

- (void)onProfileSelected:(NSPopUpButton *)sender {
    NSInteger idx = sender.indexOfSelectedItem;
    _cachedSelectedIndex = idx;
    [self updateButtonsState];
    if ([self.delegate respondsToSelector:@selector(miniWindowController:didSelectProfileAtIndex:)]) {
        [self.delegate miniWindowController:self didSelectProfileAtIndex:idx];
    }
}

- (void)onSendClicked:(id)sender {
    (void)sender;
    if ([self.delegate respondsToSelector:@selector(miniWindowControllerDidRequestSend:)]) {
        [self.delegate miniWindowControllerDidRequestSend:self];
    }
}

#pragma mark - NSWindowDelegate

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    if (_isTearingDown) return;
    if ([self.delegate respondsToSelector:@selector(miniWindowControllerDidClose:)]) {
        [self.delegate miniWindowControllerDidClose:self];
    }
}

@end
