/*
 * TigerCompat.h - Mac OS X 10.4 Tiger compatibility shims
 *
 * Provides types and methods introduced in 10.5 Leopard:
 * - NSInteger / NSUInteger typedefs
 * - NSString stringByReplacingOccurrencesOfString:withString:
 * - NSString componentsSeparatedByCharactersInSet:
 * - NSCharacterSet newlineCharacterSet
 * - StatusDotView (replaces 10.5 NSBox setFillColor:/setCornerRadius:)
 */

#ifndef TIGER_COMPAT_H
#define TIGER_COMPAT_H

#import <Cocoa/Cocoa.h>

/* NSInteger/NSUInteger — added in 10.5, backfill for 10.4 */
#ifndef NSINTEGER_DEFINED
#define NSINTEGER_DEFINED
typedef int NSInteger;
typedef unsigned int NSUInteger;
#define NSIntegerMax  INT_MAX
#define NSIntegerMin  INT_MIN
#define NSUIntegerMax UINT_MAX
#endif

/* NSString methods added in 10.5 */
@interface NSString (TigerCompat)
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
                                        withString:(NSString *)replacement;
- (NSArray *)componentsSeparatedByCharactersInSet:(NSCharacterSet *)separator;
@end

/* NSCharacterSet class method added in 10.5 */
@interface NSCharacterSet (TigerCompat)
+ (NSCharacterSet *)newlineCharacterSet;
@end

/*
 * StatusDotView - colored circle view for status indicators.
 * Replaces 10.5 NSBox with NSBoxCustom/setFillColor:/setCornerRadius:.
 */
@interface StatusDotView : NSView
{
    NSColor *_dotColor;
}
- (void)setDotColor:(NSColor *)color;
- (NSColor *)dotColor;
@end

#endif /* TIGER_COMPAT_H */
