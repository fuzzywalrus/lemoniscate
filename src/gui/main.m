/*
 * main.m - Lemoniscate Server Admin GUI entry point
 *
 * Tiger-compatible: Creates NSApplication programmatically.
 * No NIB loading — AppController builds all UI in code.
 */

#import <Cocoa/Cocoa.h>
#import "AppController.h"

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    [NSApplication sharedApplication];

    AppController *controller = [[AppController alloc] init];
    [NSApp setDelegate:controller];

    [NSApp run];

    [controller release];
    [pool release];
    return 0;
}
