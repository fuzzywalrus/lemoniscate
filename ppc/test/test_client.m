/*
 * test_client.m - Unit tests for Phase 2: Obj-C client library
 *
 * Tests HLClientPrefs and HLClient initialization and send logic.
 * Integration test against a live Go server is separate.
 *
 * Objective-C 1.0 compatible (Tiger/PPC).
 */

#import <Foundation/Foundation.h>
#import "hotline/client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s ", #name); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

/* ===== Dummy delegate for testing ===== */

@interface TestDelegate : NSObject <HLClientDelegate>
{
    int _transactionCount;
    BOOL _didDisconnect;
    NSString *_lastLogMessage;
}
- (int)transactionCount;
- (BOOL)didDisconnect;
- (NSString *)lastLogMessage;
@end

@implementation TestDelegate

- (id)init
{
    self = [super init];
    if (self) {
        _transactionCount = 0;
        _didDisconnect = NO;
        _lastLogMessage = nil;
    }
    return self;
}

- (void)dealloc
{
    [_lastLogMessage release];
    [super dealloc];
}

- (NSArray *)client:(HLClient *)client
    didReceiveTransaction:(hl_transaction_t *)transaction
{
    (void)client;
    (void)transaction;
    _transactionCount++;
    return nil;
}

- (void)client:(HLClient *)client didDisconnectWithError:(NSError *)error
{
    (void)client;
    (void)error;
    _didDisconnect = YES;
}

- (void)client:(HLClient *)client logInfo:(NSString *)message
{
    (void)client;
    [_lastLogMessage release];
    _lastLogMessage = [message copy];
}

- (void)client:(HLClient *)client logError:(NSString *)message
{
    (void)client;
    [_lastLogMessage release];
    _lastLogMessage = [message copy];
}

- (int)transactionCount { return _transactionCount; }
- (BOOL)didDisconnect { return _didDisconnect; }
- (NSString *)lastLogMessage { return _lastLogMessage; }

@end

/* ===== HLClientPrefs tests ===== */

static void test_prefs_init(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    HLClientPrefs *prefs = [[HLClientPrefs alloc] initWithUsername:@"TestUser"];
    assert([[prefs username] isEqualToString:@"TestUser"]);
    assert([prefs iconID] == 128); /* default icon */
    assert([prefs enableBell] == NO);
    assert([prefs tracker] == nil);

    [prefs release];
    [pool release];
}

static void test_prefs_setters(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    HLClientPrefs *prefs = [[HLClientPrefs alloc] initWithUsername:@"A"];
    [prefs setUsername:@"NewName"];
    assert([[prefs username] isEqualToString:@"NewName"]);

    [prefs setIconID:414];
    assert([prefs iconID] == 414);

    [prefs setTracker:@"hltracker.com"];
    assert([[prefs tracker] isEqualToString:@"hltracker.com"]);

    [prefs setEnableBell:YES];
    assert([prefs enableBell] == YES);

    [prefs release];
    [pool release];
}

static void test_prefs_icon_bytes(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    HLClientPrefs *prefs = [[HLClientPrefs alloc] initWithUsername:@"X"];
    [prefs setIconID:414]; /* 0x019E */

    uint8_t bytes[2];
    [prefs iconBytes:bytes];
    assert(bytes[0] == 0x01);
    assert(bytes[1] == 0x9E);

    [prefs release];
    [pool release];
}

/* ===== HLClient tests ===== */

static void test_client_init(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    TestDelegate *delegate = [[TestDelegate alloc] init];
    HLClient *client = [[HLClient alloc] initWithUsername:@"Bob"
                                                 delegate:delegate];
    assert(client != nil);
    assert([[client prefs] username] != nil);
    assert([[[client prefs] username] isEqualToString:@"Bob"]);
    assert(![client isConnected]);
    assert([[client userList] count] == 0);

    [client release];
    [delegate release];
    [pool release];
}

static void test_client_connect_bad_address(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    TestDelegate *delegate = [[TestDelegate alloc] init];
    HLClient *client = [[HLClient alloc] initWithUsername:@"Test"
                                                 delegate:delegate];

    /* Invalid address format */
    NSError *err = [client connectToAddress:@"noport"
                                      login:@"guest"
                                   password:@""];
    assert(err != nil);
    assert(![client isConnected]);

    [client release];
    [delegate release];
    [pool release];
}

static void test_client_connect_refused(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    TestDelegate *delegate = [[TestDelegate alloc] init];
    HLClient *client = [[HLClient alloc] initWithUsername:@"Test"
                                                 delegate:delegate];

    /* Connect to a port that's almost certainly not running a Hotline server */
    NSError *err = [client connectToAddress:@"127.0.0.1:19999"
                                      login:@"guest"
                                   password:@""];
    assert(err != nil);
    assert(![client isConnected]);

    [client release];
    [delegate release];
    [pool release];
}

static void test_client_disconnect_when_not_connected(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    TestDelegate *delegate = [[TestDelegate alloc] init];
    HLClient *client = [[HLClient alloc] initWithUsername:@"Test"
                                                 delegate:delegate];

    /* Should not crash when disconnecting without a connection */
    [client disconnect];
    assert(![client isConnected]);

    [client release];
    [delegate release];
    [pool release];
}

/* ===== Main ===== */

int main(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    printf("mobius-c Phase 2: Obj-C Client Tests\n");
    printf("====================================\n\n");

    printf("HLClientPrefs tests:\n");
    TEST(test_prefs_init);
    TEST(test_prefs_setters);
    TEST(test_prefs_icon_bytes);

    printf("\nHLClient tests:\n");
    TEST(test_client_init);
    TEST(test_client_connect_bad_address);
    TEST(test_client_connect_refused);
    TEST(test_client_disconnect_when_not_connected);

    printf("\n====================================\n");
    printf("%d/%d tests passed\n", tests_passed, tests_run);

    [pool release];
    return (tests_passed == tests_run) ? 0 : 1;
}
