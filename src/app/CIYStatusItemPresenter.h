#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

/// Presenter für das Statusleisten-Symbol (NSStatusItem) der CIY X79 RGB Steuerung (Roadmap 2.3).
/// Reine Darstellungs- und Formatierungslogik: besitzt keinen Hardwarezugriff,
/// startet keine Timer und ist isoliert unit-testbar.
@interface CIYStatusItemPresenter : NSObject

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/// Extrahiert die Initiale aus dem Profilnamen (überspringt führende Emojis / Sonderzeichen).
/// Fällt auf @"•" zurück, wenn kein alphanumerisches Zeichen vorhanden ist oder name nil/leer ist.
+ (NSString *)initialForProfileName:(nullable NSString *)name;

/// Ermittelt deterministisch die dominante (häufigste) nicht-schwarze Farbe aus den Slots.
/// Bei Gleichstand der Häufigkeit gewinnt der niedrigste numerische Slot-Index.
/// Fällt auf [NSColor controlAccentColor] zurück, wenn keine nicht-schwarze Farbe existiert
/// oder slots nil/leer/ungültig ist.
+ (NSColor *)dominantColorFromSlots:(nullable NSDictionary *)slots;

/// Liefert das Tastaturkürzel ("1".."7") für die ersten 7 Profile (Index 0..6),
/// sonst nil/leer für nachfolgende Profile.
+ (nullable NSString *)keyEquivalentForIndex:(NSInteger)index;

/// Liefert den Tooltip-Text für das Menüleisten-Icon ("CIY X79 Control – <Name>").
+ (NSString *)toolTipForProfileName:(nullable NSString *)name connected:(BOOL)connected;

/// Liefert die Barrierefreiheits-Beschreibung ("CIY X79, profile “<Name>”, connected/disconnected").
+ (NSString *)accessibilityDescriptionForProfileName:(nullable NSString *)name connected:(BOOL)connected;

/// Erzeugt ein adaptives 18x18-Icon für das NSStatusItem via drawingHandler.
/// Zeichnet den Profil-Farbkreis mit Initiale und den Verbindungsstatus:
/// - verbunden: ausgefüllter grüner Punkt
/// - getrennt: hohler roter Ring (formkodiert für Barrierefreiheit/Farbenblindheit)
/// Das Icon ist kein Template (template = NO) und aktualisiert sich bei Appearance-Wechsel.
+ (NSImage *)iconForProfileName:(nullable NSString *)name
                   profileColor:(nullable NSColor *)color
                      connected:(BOOL)connected;

@end

NS_ASSUME_NONNULL_END
