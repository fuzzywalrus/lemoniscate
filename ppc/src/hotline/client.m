/*
 * client.m - Hotline client library (Objective-C)
 *
 * Maps to: hotline/client.go
 *
 * Objective-C 1.0 for Mac OS X 10.4 Tiger (PPC).
 * Manual retain/release, NSAutoreleasePool, no blocks.
 */

#import "hotline/client.h"
#include "hotline/handshake.h"
#include "hotline/user.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libkern/OSAtomic.h>

#define HL_KEEPALIVE_INTERVAL 300  /* seconds, maps to Go keepaliveInterval */
#define HL_CONNECT_TIMEOUT    5    /* seconds, maps to Go net.DialTimeout 5s */

/* ===== Helper: NSError from errno ===== */

static NSError *errorWithErrno(NSString *desc)
{
    NSDictionary *info = [NSDictionary dictionaryWithObject:desc
                                                     forKey:NSLocalizedDescriptionKey];
    return [NSError errorWithDomain:@"HLClientError"
                               code:errno
                           userInfo:info];
}

static NSError *errorWithMessage(NSString *desc)
{
    NSDictionary *info = [NSDictionary dictionaryWithObject:desc
                                                     forKey:NSLocalizedDescriptionKey];
    return [NSError errorWithDomain:@"HLClientError"
                               code:-1
                           userInfo:info];
}

/* ===== Helper: write full buffer ===== */

static int write_all(int fd, const uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        total += (size_t)w;
    }
    return 0;
}

/* ===== Helper: TCP connect with timeout ===== */

static int tcp_connect(const char *host, int port, int timeout_secs)
{
    struct hostent *he = gethostbyname(host);
    if (!he) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    /* Set send/receive timeout */
    struct timeval tv;
    tv.tv_sec = timeout_secs;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* Keep timeout set — caller clears after handshake + login completes */
    return fd;
}

/* ===== NSData key wrapper for activeTasks dictionary ===== */
/* Go uses [4]byte as map key; we wrap in NSData for NSDictionary */

static NSData *taskKeyFromID(const uint8_t id[4])
{
    return [NSData dataWithBytes:id length:4];
}


/* ====================================================================
 * HLClientPrefs
 * Maps to: Go ClientPrefs struct
 * ==================================================================== */

@implementation HLClientPrefs

- (id)initWithUsername:(NSString *)username
{
    self = [super init];
    if (self) {
        _username = [username copy];
        _iconID = 128;  /* Default Hotline icon */
        _tracker = nil;
        _enableBell = NO;
    }
    return self;
}

- (void)dealloc
{
    [_username release];
    [_tracker release];
    [super dealloc];
}

- (NSString *)username { return _username; }
- (void)setUsername:(NSString *)username
{
    [username retain];
    [_username release];
    _username = username;
}

- (int)iconID { return _iconID; }
- (void)setIconID:(int)iconID { _iconID = iconID; }

- (NSString *)tracker { return _tracker; }
- (void)setTracker:(NSString *)tracker
{
    [tracker retain];
    [_tracker release];
    _tracker = tracker;
}

- (BOOL)enableBell { return _enableBell; }
- (void)setEnableBell:(BOOL)flag { _enableBell = flag; }

/* Maps to: Go ClientPrefs.IconBytes() */
- (void)iconBytes:(uint8_t[2])outBytes
{
    hl_write_u16(outBytes, (uint16_t)_iconID);
}

@end


/* ====================================================================
 * HLClient
 * Maps to: Go Client struct
 * ==================================================================== */

@implementation HLClient

/* --- Init / Dealloc --- */

- (id)initWithUsername:(NSString *)username
              delegate:(id<HLClientDelegate>)delegate
{
    /* Maps to: Go NewClient() */
    self = [super init];
    if (self) {
        _socketFD = -1;
        _prefs = [[HLClientPrefs alloc] initWithUsername:username];
        _delegate = delegate; /* weak reference, not retained (delegate pattern) */
        _activeTasks = [[NSMutableDictionary alloc] init];
        _userList = [[NSMutableArray alloc] init];
        _sendLock = [[NSLock alloc] init];
        _shouldStop = NO;
        _receiveRunning = NO;
        _keepaliveRunning = NO;
        _readBufLen = 0;
    }
    return self;
}

- (void)dealloc
{
    [self disconnect];
    [_prefs release];
    [_activeTasks release];
    [_userList release];
    [_sendLock release];
    [super dealloc];
}

/* --- Logging helpers --- */

- (void)logInfo:(NSString *)msg
{
    if (_delegate && [(NSObject *)_delegate respondsToSelector:@selector(client:logInfo:)]) {
        [(id)_delegate client:self logInfo:msg];
    }
}

- (void)logError:(NSString *)msg
{
    if (_delegate && [(NSObject *)_delegate respondsToSelector:@selector(client:logError:)]) {
        [(id)_delegate client:self logError:msg];
    }
}

/* --- Connection lifecycle --- */

- (NSError *)connectToAddress:(NSString *)address
                        login:(NSString *)login
                     password:(NSString *)password
{
    /* Maps to: Go Client.Connect()
     *
     * 1. Parse host:port
     * 2. TCP connect with timeout
     * 3. Handshake (TRTP/HOTL)
     * 4. Send TranLogin with credentials
     * 5. Start keepalive and receive threads
     */

    /* Parse "host:port" */
    NSArray *parts = [address componentsSeparatedByString:@":"];
    if ([parts count] < 2) {
        return errorWithMessage(@"Invalid address format, expected host:port");
    }
    const char *host = [[parts objectAtIndex:0] UTF8String];
    int port = [[parts objectAtIndex:1] intValue];
    if (port <= 0 || port > 65535) {
        return errorWithMessage(@"Invalid port number");
    }

    /* TCP connect */
    _socketFD = tcp_connect(host, port, HL_CONNECT_TIMEOUT);
    if (_socketFD < 0) {
        return errorWithErrno(@"Failed to connect to server");
    }

    /* Handshake */
    /* Maps to: Go Client.Handshake() */
    if (hl_perform_handshake_client(_socketFD) < 0) {
        close(_socketFD);
        _socketFD = -1;
        return errorWithMessage(@"Handshake failed");
    }

    [self logInfo:[NSString stringWithFormat:@"Connected to %@", address]];

    /* Build and send TranLogin (type 107) */
    /* Maps to: Go Client.Connect() login transaction */
    const char *loginStr = [login UTF8String];
    const char *passwdStr = [password UTF8String];
    const char *usernameStr = [[_prefs username] UTF8String];

    size_t loginLen = strlen(loginStr);
    size_t passwdLen = strlen(passwdStr);
    size_t usernameLen = strlen(usernameStr);

    /* Obfuscate login and password (255-rotation) */
    uint8_t obfuLogin[256];
    uint8_t obfuPassword[256];
    hl_encode_string((const uint8_t *)loginStr, obfuLogin,
                     loginLen > 255 ? 255 : loginLen);
    hl_encode_string((const uint8_t *)passwdStr, obfuPassword,
                     passwdLen > 255 ? 255 : passwdLen);

    uint8_t iconBytes[2];
    [_prefs iconBytes:iconBytes];

    hl_field_t fields[4];
    memset(fields, 0, sizeof(fields));
    int alloc_ok = 1;

    if (hl_field_new(&fields[0], FIELD_USER_NAME,
                     (const uint8_t *)usernameStr, (uint16_t)usernameLen) < 0)
        alloc_ok = 0;
    if (alloc_ok && hl_field_new(&fields[1], FIELD_USER_ICON_ID, iconBytes, 2) < 0)
        alloc_ok = 0;
    if (alloc_ok && hl_field_new(&fields[2], FIELD_USER_LOGIN, obfuLogin,
                     (uint16_t)(loginLen > 255 ? 255 : loginLen)) < 0)
        alloc_ok = 0;
    if (alloc_ok && hl_field_new(&fields[3], FIELD_USER_PASSWORD, obfuPassword,
                     (uint16_t)(passwdLen > 255 ? 255 : passwdLen)) < 0)
        alloc_ok = 0;

    if (!alloc_ok) {
        int i;
        for (i = 0; i < 4; i++) hl_field_free(&fields[i]);
        close(_socketFD);
        _socketFD = -1;
        return errorWithMessage(@"Failed to allocate login fields");
    }

    hl_transaction_t loginTran;
    hl_client_id_t zeroCID = {0, 0};
    if (hl_transaction_new(&loginTran, TRAN_LOGIN, zeroCID, fields, 4) < 0) {
        int i;
        for (i = 0; i < 4; i++) hl_field_free(&fields[i]);
        close(_socketFD);
        _socketFD = -1;
        return errorWithMessage(@"Failed to allocate login transaction");
    }

    NSError *sendErr = [self sendTransaction:&loginTran];

    /* Cleanup */
    int i;
    for (i = 0; i < 4; i++) hl_field_free(&fields[i]);
    hl_transaction_free(&loginTran);

    if (sendErr) {
        close(_socketFD);
        _socketFD = -1;
        return sendErr;
    }

    /* Clear socket timeout now that handshake + login are done */
    struct timeval tv_clear;
    tv_clear.tv_sec = 0;
    tv_clear.tv_usec = 0;
    setsockopt(_socketFD, SOL_SOCKET, SO_SNDTIMEO, &tv_clear, sizeof(tv_clear));
    setsockopt(_socketFD, SOL_SOCKET, SO_RCVTIMEO, &tv_clear, sizeof(tv_clear));

    [self logInfo:@"Login sent"];

    /* Start threads */
    /* Maps to: Go's "go func() { _ = c.keepalive() }()" and
     *          the caller's "go client.HandleTransactions(ctx)" */
    _shouldStop = NO;

    /* Tiger Obj-C 1.0: must use detachNewThreadSelector (10.4+).
     * initWithTarget:selector:object: and -start are 10.5+ only. */
    [NSThread detachNewThreadSelector:@selector(receiveLoop)
                             toTarget:self
                           withObject:nil];

    [NSThread detachNewThreadSelector:@selector(keepaliveLoop)
                             toTarget:self
                           withObject:nil];

    return nil; /* success */
}

- (void)disconnect
{
    /* Maps to: Go Client.Disconnect() */
    _shouldStop = YES;
    /* Ensure _shouldStop is visible to other threads on PPC.
     * OSMemoryBarrier is the correct Tiger/Leopard API (deprecated in 10.12+). */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    OSMemoryBarrier();
#pragma clang diagnostic pop

    if (_socketFD >= 0) {
        /* Closing the socket will unblock the read() in receiveLoop */
        close(_socketFD);
        _socketFD = -1;
    }

    /* Wait for detached threads to exit (they set running=NO before returning).
     * Tiger has no -[NSThread cancel] (10.5+), so we rely on _shouldStop flag
     * and the socket close to unblock read(). Poll briefly. */
    int attempts = 0;
    while ((_receiveRunning || _keepaliveRunning) && attempts < 50) {
        usleep(100000); /* 100ms */
        attempts++;
    }

    [_activeTasks removeAllObjects];
    _readBufLen = 0;

    [self logInfo:@"Disconnected"];
}

/* --- Send --- */

- (NSError *)sendTransaction:(hl_transaction_t *)t
{
    /* Maps to: Go Client.Send() */
    [_sendLock lock];

    /* Track non-reply transactions so we can match replies */
    /* Maps to: Go "if t.IsReply == 0 { c.activeTasks[t.ID] = &t }" */
    if (t->is_reply == 0) {
        NSData *key = taskKeyFromID(t->id);

        /* Store the transaction type so we can identify the reply */
        NSData *typeData = [NSData dataWithBytes:t->type length:2];
        [_activeTasks setObject:typeData forKey:key];
    }

    /* Serialize and send */
    size_t wireSize = hl_transaction_wire_size(t);
    uint8_t *buf = (uint8_t *)malloc(wireSize);
    if (!buf) {
        [_sendLock unlock];
        return errorWithMessage(@"Out of memory serializing transaction");
    }

    int written = hl_transaction_serialize(t, buf, wireSize);
    if (written < 0) {
        free(buf);
        [_sendLock unlock];
        return errorWithMessage(@"Failed to serialize transaction");
    }

    int rc = write_all(_socketFD, buf, (size_t)written);
    free(buf);

    if (rc < 0) {
        [_sendLock unlock];
        return errorWithErrno(@"Failed to send transaction");
    }

    [self logInfo:[NSString stringWithFormat:@"Sent: %s (%d bytes)",
                   hl_transaction_type_name(t->type), written]];

    [_sendLock unlock];
    return nil;
}

/* --- Receive loop (runs on _receiveThread) --- */
/* Maps to: Go Client.HandleTransactions() */

- (void)receiveLoop
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    _receiveRunning = YES;

    while (!_shouldStop) {
        /* Check if buffer is full with no complete transaction (protocol error) */
        if (_readBufLen >= sizeof(_readBuf)) {
            [self logError:@"Receive buffer full with no complete transaction, resetting"];
            _readBufLen = 0;
        }

        /* Read data into accumulation buffer */
        ssize_t n = read(_socketFD,
                         _readBuf + _readBufLen,
                         sizeof(_readBuf) - _readBufLen);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (!_shouldStop) {
                NSError *err = (n == 0)
                    ? errorWithMessage(@"Connection closed by server")
                    : errorWithErrno(@"Read error");
                [_delegate client:self didDisconnectWithError:err];
            }
            break;
        }
        _readBufLen += (size_t)n;

        /* Scan for complete transactions */
        /* Maps to: Go scanner.Split(transactionScanner) loop */
        while (_readBufLen > 0) {
            int tranLen = hl_transaction_scan(_readBuf, _readBufLen);
            if (tranLen < 0) {
                /* Malformed data (e.g. overflow in size field) — discard buffer */
                [self logError:@"Transaction scan error, resetting buffer"];
                _readBufLen = 0;
                break;
            }
            if (tranLen == 0) break; /* need more data */

            /* Parse the transaction */
            hl_transaction_t t;
            int consumed = hl_transaction_deserialize(&t, _readBuf,
                                                       (size_t)tranLen);
            if (consumed < 0) {
                [self logError:@"Failed to parse transaction"];
                /* Remove bad data and keep going */
                _readBufLen = 0;
                break;
            }

            /* Handle reply matching */
            /* Maps to: Go Client.HandleTransaction() reply logic
             * Must lock _sendLock — Go uses c.mu.Lock() in both
             * Send() and HandleTransaction(). */
            if (t.is_reply == 1) {
                [_sendLock lock];
                NSData *key = taskKeyFromID(t.id);
                NSData *origType = [_activeTasks objectForKey:key];
                if (origType) {
                    memcpy(t.type, [origType bytes], 2);
                    [_activeTasks removeObjectForKey:key];
                }
                [_sendLock unlock];

                if (!origType) {
                    [self logError:@"No matching request for reply ID"];
                }
            }

            [self logInfo:[NSString stringWithFormat:@"Recv: %s (reply=%d)",
                           hl_transaction_type_name(t.type), t.is_reply]];

            /* Dispatch to delegate */
            /* Maps to: Go handler dispatch in HandleTransaction() */
            NSArray *responses = [_delegate client:self
                                  didReceiveTransaction:&t];

            /* Send any response transactions */
            if (responses) {
                unsigned idx;
                for (idx = 0; idx < [responses count]; idx++) {
                    NSValue *val = [responses objectAtIndex:idx];
                    hl_transaction_t *resp = (hl_transaction_t *)[val pointerValue];
                    if (resp) {
                        [self sendTransaction:resp];
                    }
                }
            }

            hl_transaction_free(&t);

            /* Shift remaining data to front of buffer */
            /* Maps to: Go scanner advancing past the consumed token */
            if ((size_t)consumed < _readBufLen) {
                memmove(_readBuf, _readBuf + consumed,
                        _readBufLen - (size_t)consumed);
            }
            _readBufLen -= (size_t)consumed;
        }

        /* Drain autorelease pool periodically */
        [pool release];
        pool = [[NSAutoreleasePool alloc] init];
    }

    _receiveRunning = NO;
    [pool release];
}

/* --- Keepalive loop (runs on _keepaliveThread) --- */
/* Maps to: Go Client.keepalive() goroutine */

- (void)keepaliveLoop
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    _keepaliveRunning = YES;

    while (!_shouldStop) {
        /* Maps to: Go "case <-ticker.C:" with 300 second interval */
        /* Sleep in 1-second increments so we can check _shouldStop */
        int elapsed;
        for (elapsed = 0; elapsed < HL_KEEPALIVE_INTERVAL && !_shouldStop; elapsed++) {
            sleep(1);
        }

        if (_shouldStop) break;

        /* Send TranKeepAlive */
        /* Maps to: Go "c.Send(NewTransaction(TranKeepAlive, [2]byte{}))" */
        hl_transaction_t ka;
        hl_client_id_t zeroCID = {0, 0};
        hl_transaction_new(&ka, TRAN_KEEP_ALIVE, zeroCID, NULL, 0);

        [self sendTransaction:&ka];
        hl_transaction_free(&ka);
    }

    _keepaliveRunning = NO;
    [pool release];
}

/* --- Accessors --- */

- (HLClientPrefs *)prefs { return _prefs; }

- (NSArray *)userList { return [[_userList copy] autorelease]; }

- (BOOL)isConnected { return _socketFD >= 0 && !_shouldStop; }

@end
