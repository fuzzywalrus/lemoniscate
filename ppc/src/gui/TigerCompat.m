/*
 * TigerCompat.m - Mac OS X 10.4 Tiger compatibility implementations
 */

#import "TigerCompat.h"

/* --- NSString backfills --- */

@implementation NSString (TigerCompat)

- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
                                        withString:(NSString *)replacement
{
    NSMutableString *result = [NSMutableString stringWithString:self];
    [result replaceOccurrencesOfString:target
                            withString:replacement
                               options:0
                                 range:NSMakeRange(0, [result length])];
    return result;
}

- (NSArray *)componentsSeparatedByCharactersInSet:(NSCharacterSet *)separator
{
    NSMutableArray *result = [NSMutableArray array];
    NSScanner *scanner = [NSScanner scannerWithString:self];
    [scanner setCharactersToBeSkipped:nil];
    NSString *chunk = nil;

    while (![scanner isAtEnd]) {
        if ([scanner scanUpToCharactersFromSet:separator intoString:&chunk]) {
            [result addObject:chunk];
        }
        /* Skip past separator characters */
        [scanner scanCharactersFromSet:separator intoString:NULL];
    }
    return result;
}

@end

/* --- NSCharacterSet backfill --- */

@implementation NSCharacterSet (TigerCompat)

+ (NSCharacterSet *)newlineCharacterSet
{
    return [NSCharacterSet characterSetWithCharactersInString:@"\n\r"];
}

@end

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
