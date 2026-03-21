/*
 * TigerCompat.h - Compatibility helpers
 *
 * StatusDotView: colored circle view for status indicators.
 * (Tiger backfill categories removed — targeting 10.13+)
 */

#ifndef TIGER_COMPAT_H
#define TIGER_COMPAT_H

#import <Cocoa/Cocoa.h>

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
