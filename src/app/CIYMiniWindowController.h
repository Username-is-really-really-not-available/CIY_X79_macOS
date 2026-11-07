#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class CIYMiniWindowController;

/// Delegate-Protokoll zur Entkopplung von Mini-Window-UI und App-Logik.
@protocol CIYMiniWindowControllerDelegate <NSObject>
@optional
- (void)miniWindowController:(CIYMiniWindowController *)controller didSelectProfileAtIndex:(NSInteger)index;
- (void)miniWindowControllerDidRequestSend:(CIYMiniWindowController *)controller;
- (void)miniWindowControllerDidClose:(CIYMiniWindowController *)controller;
@end

/// Schlanker Controller für das kompakte Floating-Hilfsfenster (Roadmap 2.2).
/// Reine View-Schicht: hält keine Profildaten und greift niemals direkt auf USB/Hardware zu.
@interface CIYMiniWindowController : NSObject

@property (nonatomic, weak, nullable) id<CIYMiniWindowControllerDelegate> delegate;
@property (nonatomic, readonly) BOOL isVisible;

/// Aktualisiert die Liste der Profile im Dropdown und setzt den ausgewählten Index,
/// ohne eine Delegate-Aktion auszulösen.
- (void)setProfileNames:(NSArray<NSString *> *)names selectedIndex:(NSInteger)index;

/// Aktualisiert die Auswahl im Dropdown, ohne eine Delegate-Aktion auszulösen.
- (void)setSelectedIndex:(NSInteger)index;

/// Aktualisiert die Verbindungsanzeige (Hardware-Status).
- (void)setConnected:(BOOL)connected;

/// Aktualisiert die Anzeige des letzten Sendeergebnisses mit Zeitstempel.
- (void)noteSendResult:(BOOL)ok atDate:(nullable NSDate *)date;

/// Zeigt das Mini-Fenster an (erstellt UI-Elemente bei Bedarf lazy).
- (void)show;

/// Blendet das Mini-Fenster aus (orderOut:).
- (void)hide;

/// Schaltet die Sichtbarkeit des Mini-Fensters um.
- (void)toggle;

/// Gibt alle UI-Elemente, Targets und Delegaten idempotent frei.
- (void)teardown;

@end

NS_ASSUME_NONNULL_END
