/*
 * TigerCompat.m - Compatibility helpers
 * (Tiger backfill categories removed — targeting 10.13+)
 */

#import "TigerCompat.h"

/* --- StatusDotView --- */

@implementation StatusDotView

- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        _dotColor = [[NSColor grayColor] retain];
    }
    return self;
}

- (void)setDotColor:(NSColor *)color
{
    [color retain];
    [_dotColor release];
    _dotColor = color;
    [self setNeedsDisplay:YES];
}

- (NSColor *)dotColor
{
    return _dotColor;
}

- (void)drawRect:(NSRect)rect
{
    (void)rect;
    if (_dotColor) {
        [_dotColor set];
        [[NSBezierPath bezierPathWithOvalInRect:[self bounds]] fill];
    }
}

- (void)dealloc
{
    [_dotColor release];
    [super dealloc];
}

@end
