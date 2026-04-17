/*
 * client.h - Hotline client library (Objective-C)
 *
 * Maps to: hotline/client.go
 *
 * Objective-C 1.0 compatible (Tiger/PPC):
 * - No @property/@synthesize (use explicit accessors)
 * - No ARC (manual retain/release)
 * - No blocks (use delegate pattern instead of Go channels)
 * - No @autoreleasepool (use NSAutoreleasePool)
 *
 * The client wraps the C wire format library (libhotline.a) in an
 * idiomatic Cocoa interface with delegate callbacks for received
 * transactions.
 */

#ifndef HOTLINE_CLIENT_H
#define HOTLINE_CLIENT_H

#import <Foundation/Foundation.h>
#include "hotline/types.h"
#include "hotline/transaction.h"
#include "hotline/field.h"

@class HLClient;

/* --- HLClientPrefs --- */
/* Maps to: Go ClientPrefs struct */

@interface HLClientPrefs : NSObject
{
    NSString *_username;
    int       _iconID;
    NSString *_tracker;
    BOOL      _enableBell;
}

- (id)initWithUsername:(NSString *)username;
- (void)dealloc;

/* Accessors (Tiger Obj-C 1.0 style) */
- (NSString *)username;
- (void)setUsername:(NSString *)username;
- (int)iconID;
- (void)setIconID:(int)iconID;
- (NSString *)tracker;
- (void)setTracker:(NSString *)tracker;
- (BOOL)enableBell;
- (void)setEnableBell:(BOOL)flag;

/* Maps to: Go ClientPrefs.IconBytes() */
- (void)iconBytes:(uint8_t[2])outBytes;

@end


/* --- HLClientDelegate protocol --- */
/* Replaces Go's Handlers map[TranType]ClientHandler and Logger interface.
 * Delegate callbacks are invoked on the receive thread. */

@protocol HLClientDelegate

/* Called when a transaction is received from the server.
 * Maps to: Go Client.Handlers dispatch in HandleTransaction()
 * Return an array of HLTransaction* to send back, or nil. */
- (NSArray *)client:(HLClient *)client
    didReceiveTransaction:(hl_transaction_t *)transaction;

/* Called when the connection is lost or an error occurs.
 * Maps to: Go HandleTransactions() returning an error */
- (void)client:(HLClient *)client didDisconnectWithError:(NSError *)error;

@end

/* Optional logging callbacks as an informal protocol.
 * Using NSObject category instead of @optional for Tiger Obj-C 1.0
 * compatibility (Xcode < 2.4 doesn't support @optional).
 * HLClient checks with respondsToSelector: before calling. */
@interface NSObject (HLClientLogging)
- (void)client:(HLClient *)client logInfo:(NSString *)message;
- (void)client:(HLClient *)client logError:(NSString *)message;
@end


/* --- HLClient --- */
/* Maps to: Go Client struct */

@interface HLClient : NSObject
{
    int              _socketFD;       /* Go: Connection net.Conn */
    HLClientPrefs   *_prefs;         /* Go: Pref *ClientPrefs */
    id<HLClientDelegate> _delegate;  /* Go: Handlers + Logger */

    NSMutableDictionary *_activeTasks; /* Go: activeTasks map[[4]byte]*Transaction */
    NSMutableArray      *_userList;    /* Go: UserList []User */

    NSLock       *_sendLock;     /* Go: mu sync.Mutex */
    volatile BOOL _shouldStop;   /* Go: done chan struct{} */
    volatile BOOL _receiveRunning;  /* YES while receiveLoop is executing */
    volatile BOOL _keepaliveRunning;/* YES while keepaliveLoop is executing */

    /* Read buffer for the receive loop's transaction scanner */
    uint8_t       _readBuf[131072]; /* 128KB accumulation buffer */
    size_t        _readBufLen;
}

/* --- Init / Dealloc --- */

/* Maps to: Go NewClient() */
- (id)initWithUsername:(NSString *)username
              delegate:(id<HLClientDelegate>)delegate;
- (void)dealloc;

/* --- Connection lifecycle --- */

/* Maps to: Go Client.Connect()
 * Connects to server, performs handshake, sends login, starts
 * keepalive and receive threads.
 * address is "host:port" format.
 * Returns nil on success, or an NSError on failure. */
- (NSError *)connectToAddress:(NSString *)address
                        login:(NSString *)login
                     password:(NSString *)password;

/* Maps to: Go Client.Disconnect()
 * Stops threads and closes the socket. */
- (void)disconnect;

/* Maps to: Go Client.Send()
 * Thread-safe. Serializes and sends a transaction.
 * Returns nil on success, or an NSError. */
- (NSError *)sendTransaction:(hl_transaction_t *)t;

/* --- Accessors --- */

- (HLClientPrefs *)prefs;
- (NSArray *)userList;
- (BOOL)isConnected;

@end

#endif /* HOTLINE_CLIENT_H */
