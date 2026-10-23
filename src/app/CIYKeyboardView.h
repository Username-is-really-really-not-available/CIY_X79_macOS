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

@end

NS_ASSUME_NONNULL_END
