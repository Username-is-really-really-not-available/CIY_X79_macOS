#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class CIYKeyboardView;

@protocol CIYKeyboardViewDelegate <NSObject>
@optional
- (void)keyboardViewDidSelectKey:(int)slot label:(NSString *)label;
- (void)keyboardViewDidUndo:(CIYKeyboardView *)view;
- (void)keyboardViewDidRedo:(CIYKeyboardView *)view;
@end

@interface CIYKeyboardView : NSView <NSMenuItemValidation>

@property (nonatomic, weak) id<CIYKeyboardViewDelegate> delegate;
@property (nonatomic, assign) int selectedSlot;

/// Setzt die Farbe eines Slots. Gibt YES zurück, wenn sich die RGB-Farbe
/// tatsächlich geändert hat; NO bei ungültigem Slot, nil oder unveränderter Farbe.
- (BOOL)setColor:(NSColor *)color forSlot:(int)slot;
- (BOOL)setColor:(NSColor *)color forSlot:(int)slot continuous:(BOOL)continuous;
- (BOOL)resetAllKeysToColor:(NSColor *)color;

/// Gibt ein Dictionary aller Tasten-Slots (0..125) und deren aktueller NSColor zurück.
- (NSDictionary<NSNumber *, NSColor *> *)allKeyColors;

/// Setzt die Farben für alle übergebenen Slots und aktualisiert die Ansicht (ohne Undo).
/// Gibt YES zurück, wenn sich mindestens eine RGB-Farbe geändert hat.
- (BOOL)applyKeyColors:(NSDictionary<NSNumber *, NSColor *> *)colors;

/// Setzt die Farben für die übergebenen Slots; nicht enthaltene Slots bleiben unverändert.
/// Mit registerUndo:YES werden nur tatsächliche Änderungen als Undo-Aktion erfasst.
/// Gibt YES zurück, wenn sich mindestens eine RGB-Farbe geändert hat.
- (BOOL)applyKeyColors:(NSDictionary<NSNumber *, NSColor *> *)colors registerUndo:(BOOL)registerUndo;

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
/// Gibt YES zurück, wenn sich mindestens eine RGB-Farbe geändert hat.
- (BOOL)applyColor:(NSColor *)color toSlots:(NSArray<NSNumber *> *)slots;

/// Prüft, ob die aktuellen Tastenfarben (Slots 0..124) exakt mit dem gespeicherten
/// Profil-Slots-Dictionary übereinstimmen (fehlende Slots gelten als #000000).
- (BOOL)matchesProfileSlots:(NSDictionary *)savedSlots;

/// Undo/Redo
- (void)recordColorChangeFrom:(NSDictionary<NSNumber *, NSColor *> *)oldColors
                           to:(NSDictionary<NSNumber *, NSColor *> *)newColors
                   continuous:(BOOL)continuous;
- (IBAction)undo:(nullable id)sender;
- (IBAction)redo:(nullable id)sender;
- (BOOL)canUndo;
- (BOOL)canRedo;
- (void)clearUndoHistory;
- (NSUInteger)undoCount;
- (NSUInteger)redoCount;

@end

NS_ASSUME_NONNULL_END
