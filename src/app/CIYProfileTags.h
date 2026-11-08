#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

extern NSString * const kCIYProfileTagsKey;
extern const NSUInteger kCIYMaxTagsPerProfile;
extern const NSUInteger kCIYMaxTagLength;

@interface CIYProfileTags : NSObject

/// Standard-Vorschläge für Profil-Tags: "Gaming", "Büro", "Coding", "Nacht"
+ (NSArray<NSString *> *)suggestedTags;

/// Normalisiert einen Tag-String für Vergleiche:
/// Trimmt Whitespace/Newlines, fasst mehrere Whitespaces zusammen und konvertiert zu Kleinbuchstaben.
/// Mappt bekannte Aliase auf kanonische Schlüssel ("büro"/"buero" -> "office", "nacht" -> "night").
/// Andere Umlaute und Diakritika bleiben distinkt erhalten (z. B. "Grün" -> "grün", "GRÜN" -> "grün", aber "Grun" -> "grun").
+ (NSString *)normalizedTagKey:(nullable NSString *)tag;

/// Bereinigt einen einzelnen Tag:
/// - Verwirft Nicht-Strings, Steuerzeichen und leere Eingaben (gibt nil zurück)
/// - Normalisiert Whitespace
/// - Kürzt auf maximal kCIYMaxTagLength (20) Unicode-Graphem-Einheiten
+ (nullable NSString *)sanitizeTag:(nullable id)rawTag;

/// Bereinigt eine Liste von Tags (oder ein beliebiges Objekt):
/// - Verwirft Nicht-Strings und ungültige Tags
/// - Dedupliziert case-insensitiv (behält die erste Schreibweise)
/// - Begrenzt das Ergebnis auf maximal kCIYMaxTagsPerProfile (5) Einträge
+ (NSArray<NSString *> *)sanitizeTags:(nullable id)rawTags;

/// Bereinigt das 'tags'-Feld in einem Profil-Dictionary in-place.
/// Entfernt den Key, falls keine gültigen Tags vorhanden sind.
+ (void)sanitizeTagsInProfile:(NSMutableDictionary *)profile;

/// Sammelt alle eindeutigen Tags über alle Profile hinweg, dedupliziert nach normalisiertem Key,
/// sortiert alphabetisch (lokalisiert).
+ (NSArray<NSString *> *)allTagsInProfiles:(NSArray<NSDictionary *> *)profiles;

/// Prüft, ob ein Profil zum angegebenen Filter-Tag passt.
/// Ein nil-Filter, leerer Filter oder "All"/"Alle" passt auf jedes Profil.
+ (BOOL)profile:(NSDictionary *)profile matchesFilterTag:(nullable NSString *)filterTag;

/// Gibt die Indizes der Profile in `profiles` zurück, die zum filterTag passen.
+ (NSArray<NSNumber *> *)filteredProfileIndicesForProfiles:(NSArray<NSDictionary *> *)profiles
                                                 filterTag:(nullable NSString *)filterTag;

/// Berechnet die Index-Zuordnung für das PopUp-Menü:
/// Falls das aktuell aktive Profil nicht im gefilterten Satz enthalten ist, wird es an Index 0 vorangestellt
/// und outActiveIsPrepended auf YES gesetzt.
+ (NSArray<NSNumber *> *)popUpIndicesForProfiles:(NSArray<NSDictionary *> *)profiles
                                       filterTag:(nullable NSString *)filterTag
                              activeProfileIndex:(NSInteger)activeProfileIndex
                               activeIsPrepended:(nullable BOOL *)outActiveIsPrepended;

/// Mappt einen Zeilenindex im PopUp-Menü auf den entsprechenden Index in profiles (-1 wenn ungültig).
+ (NSInteger)profileIndexForRow:(NSInteger)row inPopUpIndices:(NSArray<NSNumber *> *)popUpIndices;

/// Mappt einen Index in profiles auf die entsprechende Zeile im PopUp-Menü (-1 wenn nicht enthalten).
+ (NSInteger)rowForProfileIndex:(NSInteger)profileIndex inPopUpIndices:(NSArray<NSNumber *> *)popUpIndices;

/// Liefert das Profil-Dictionary für eine Zeile im gefilterten PopUp (oder nil bei ungültigem Index).
+ (nullable NSDictionary *)profileForRow:(NSInteger)row
                          inPopUpIndices:(nullable NSArray<NSNumber *> *)popUpIndices
                                profiles:(nullable NSArray<NSDictionary *> *)profiles;

@end

NS_ASSUME_NONNULL_END
