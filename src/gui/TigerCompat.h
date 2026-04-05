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

/* NSBezelStyle constants renamed in 10.12+ Sierra — backfill for Tiger/Leopard */
#ifndef NSBezelStyleRounded
#define NSBezelStyleRounded NSRoundedBezelStyle
#endif
#ifndef NSBezelStyleHelpButton
#define NSBezelStyleHelpButton NSHelpButtonBezelStyle
#endif
#ifndef NSBezelStyleSmallSquare
#define NSBezelStyleSmallSquare NSSmallSquareBezelStyle
#endif
#ifndef NSButtonTypeMomentaryChange
#define NSButtonTypeMomentaryChange NSMomentaryChangeButton
#endif

/* AppKit constants renamed in 10.12+ Sierra — backfill for Tiger/Leopard */
#ifndef NSButtonTypeSwitch
#define NSButtonTypeSwitch NSSwitchButton
#endif
#ifndef NSButtonTypeRadio
#define NSButtonTypeRadio NSRadioButton
#endif
#ifndef NSButtonTypeMomentaryPushIn
#define NSButtonTypeMomentaryPushIn NSMomentaryPushInButton
#endif
#ifndef NSTextAlignmentRight
#define NSTextAlignmentRight NSRightTextAlignment
#endif
#ifndef NSTextAlignmentCenter
#define NSTextAlignmentCenter NSCenterTextAlignment
#endif
#ifndef NSTextAlignmentLeft
#define NSTextAlignmentLeft NSLeftTextAlignment
#endif
#ifndef NSWindowStyleMaskTitled
#define NSWindowStyleMaskTitled NSTitledWindowMask
#endif
#ifndef NSWindowStyleMaskClosable
#define NSWindowStyleMaskClosable NSClosableWindowMask
#endif
#ifndef NSWindowStyleMaskResizable
#define NSWindowStyleMaskResizable NSResizableWindowMask
#endif
#ifndef NSWindowStyleMaskMiniaturizable
#define NSWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
#endif

/* AppKit constants added in 10.6+ — backfill for Tiger/Leopard */
#ifndef NSModalResponseOK
#define NSModalResponseOK NSOKButton
#endif
#ifndef NSModalResponseCancel
#define NSModalResponseCancel NSCancelButton
#endif
#ifndef NSAlertStyleWarning
#define NSAlertStyleWarning NSWarningAlertStyle
#endif
#ifndef NSAlertStyleCritical
#define NSAlertStyleCritical NSCriticalAlertStyle
#endif
#ifndef NSAlertStyleInformational
#define NSAlertStyleInformational NSInformationalAlertStyle
#endif
#ifndef NSControlStateValueOn
#define NSControlStateValueOn NSOnState
#endif
#ifndef NSControlStateValueOff
#define NSControlStateValueOff NSOffState
#endif

/* NSModalResponse typedef — 10.9+ */
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1090
typedef NSInteger NSModalResponse;
#endif

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
