#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@protocol CIYKeyboardViewDelegate <NSObject>
@optional
- (void)keyboardViewDidSelectKey:(int)slot label:(NSString *)label;
@end

@interface CIYKeyboardView : NSView

@property (nonatomic, weak) id<CIYKeyboardViewDelegate> delegate;
@property (nonatomic, assign) int selectedSlot;

- (void)setColor:(NSColor *)color forSlot:(int)slot;
- (void)resetAllKeysToColor:(NSColor *)color;

/// Gibt ein Dictionary aller Tasten-Slots (0..125) und deren aktueller NSColor zurück.
- (NSDictionary<NSNumber *, NSColor *> *)allKeyColors;

/// Setzt die Farben für alle übergebenen Slots und aktualisiert die Ansicht.
- (void)applyKeyColors:(NSDictionary<NSNumber *, NSColor *> *)colors;

/// Gibt die Tastenbeschriftung für einen Matrix-Slot zurück (z. B. "W", "Esc", "↵").
- (NSString *)labelForSlot:(int)slot;

/// Setzt ein flüchtiges Anzeige-Overlay (z. B. für den Akku-Indikator).
/// Überschreibt die visuelle Tastenfarbe temporär, ohne das Basis-Profil zu verändern.
- (void)setOverlayColors:(nullable NSDictionary<NSNumber *, NSColor *> *)overlayColors;

/// Löscht alle aktiven Overlay-Farben und stellt die Basis-Profilfarben dar.
- (void)clearOverlayColors;

/// Liefert die aktuell aktiven flüchtigen Overlay-Farben oder nil.
- (nullable NSDictionary<NSNumber *, NSColor *> *)overlayColors;

/// Färbt eine geordnete Liste von Slots in der Basisfarbe ein.
- (void)applyColor:(NSColor *)color toSlots:(NSArray<NSNumber *> *)slots;

@end

NS_ASSUME_NONNULL_END
