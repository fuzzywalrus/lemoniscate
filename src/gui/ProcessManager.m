/*
 * ProcessManager.m - Server process lifecycle management
 *
 * Maps to: MobiusAdmin ProcessManager.swift
 *
 * Tiger Obj-C 1.0. Uses NSTask to launch/stop the lemoniscate binary.
 * Captures stdout/stderr via NSPipe and posts notifications for log lines.
 */

#import "ProcessManager.h"
#include <signal.h>

NSString * const PMServerStatusChangedNotification = @"PMServerStatusChanged";
NSString * const PMLogLineReceivedNotification = @"PMLogLineReceived";

@implementation ProcessManager

- (id)init
{
    self = [super init];
    if (self) {
        _task = nil;
        _stdoutPipe = nil;
        _stderrPipe = nil;
        _status = ServerStatusStopped;
        _errorMessage = nil;
        _binaryPath = nil;
    }
    return self;
}

- (void)dealloc
{
    [self stop];
    [_errorMessage release];
    [_binaryPath release];
    [super dealloc];
}

/* --- Accessors --- */

- (ServerStatus)status { return _status; }

- (NSString *)statusLabel
{
    switch (_status) {
        case ServerStatusStopped:  return @"Stopped";
        case ServerStatusStarting: return @"Starting...";
        case ServerStatusRunning:  return @"Running";
        case ServerStatusError:    return _errorMessage ? _errorMessage : @"Error";
    }
    return @"Unknown";
}

- (NSString *)errorMessage { return _errorMessage; }

- (BOOL)isRunning
{
    return _status == ServerStatusRunning || _status == ServerStatusStarting;
}

- (NSString *)binaryPath { return _binaryPath; }

- (void)setBinaryPath:(NSString *)path
{
    [path retain];
    [_binaryPath release];
    _binaryPath = path;
}

- (BOOL)hasBinary
{
    if (!_binaryPath) return NO;
    return [[NSFileManager defaultManager] isExecutableFileAtPath:_binaryPath];
}

/* --- Status management --- */

- (void)setStatus:(ServerStatus)newStatus
{
    _status = newStatus;
    [[NSNotificationCenter defaultCenter]
        postNotificationName:PMServerStatusChangedNotification
                      object:self];
}

- (void)setError:(NSString *)msg
{
    [msg retain];
    [_errorMessage release];
    _errorMessage = msg;
    [self setStatus:ServerStatusError];
}

/* --- Server lifecycle --- */

- (void)startWithConfigDir:(NSString *)configDir port:(int)port
{
    if (_task != nil) return;

    if (![self hasBinary]) {
        [self setError:@"Server binary not found"];
        return;
    }

    /* Validate port */
    if (port < 1 || port > 65535) {
        [self setError:@"Port must be between 1 and 65535"];
        return;
    }

    [self setStatus:ServerStatusStarting];

    /* Set up NSTask */
    NSTask *task = [[NSTask alloc] init];
    [task setLaunchPath:_binaryPath];
    [task setArguments:[NSArray arrayWithObjects:
        @"--config", configDir,
        @"--bind", [NSString stringWithFormat:@"%d", port],
        @"--log-level", @"info",
        nil]];

    /* Set up pipes for log capture */
    NSPipe *stdoutPipe = [NSPipe pipe];
    NSPipe *stderrPipe = [NSPipe pipe];
    [task setStandardOutput:stdoutPipe];
    [task setStandardError:stderrPipe];

    _stdoutPipe = [stdoutPipe retain];
    _stderrPipe = [stderrPipe retain];

    /* Register for read notifications on stdout */
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(stdoutDataAvailable:)
               name:NSFileHandleReadCompletionNotification
             object:[_stdoutPipe fileHandleForReading]];
    [[_stdoutPipe fileHandleForReading] readInBackgroundAndNotify];

    /* Register for read notifications on stderr */
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(stderrDataAvailable:)
               name:NSFileHandleReadCompletionNotification
             object:[_stderrPipe fileHandleForReading]];
    [[_stderrPipe fileHandleForReading] readInBackgroundAndNotify];

    /* Register for task termination */
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(taskDidTerminate:)
               name:NSTaskDidTerminateNotification
             object:task];

    /* Launch */
    NS_DURING
        [task launch];
        _task = task;
        [self setStatus:ServerStatusRunning];
    NS_HANDLER
        [self setError:[NSString stringWithFormat:@"Failed to launch: %@",
                        [localException reason]]];
        [task release];
    NS_ENDHANDLER
}

- (void)stop
{
    if (_task == nil) {
        [self setStatus:ServerStatusStopped];
        return;
    }

    /* Remove observers */
    [[NSNotificationCenter defaultCenter] removeObserver:self
        name:NSTaskDidTerminateNotification object:_task];
    [[NSNotificationCenter defaultCenter] removeObserver:self
        name:NSFileHandleReadCompletionNotification
        object:[_stdoutPipe fileHandleForReading]];
    [[NSNotificationCenter defaultCenter] removeObserver:self
        name:NSFileHandleReadCompletionNotification
        object:[_stderrPipe fileHandleForReading]];

    if ([_task isRunning]) {
        [_task terminate];

        /* Wait briefly for graceful exit (up to 3 seconds) */
        int attempts = 0;
        while ([_task isRunning] && attempts < 30) {
            [NSThread sleepForTimeInterval:0.1];
            attempts++;
        }

        /* Force kill if still running */
        if ([_task isRunning]) {
            kill([_task processIdentifier], SIGKILL);
        }
    }

    [self cleanupTask];
    [self setStatus:ServerStatusStopped];
}

- (void)restartWithConfigDir:(NSString *)configDir port:(int)port
{
    [self stop];
    [self startWithConfigDir:configDir port:port];
}

- (void)reloadConfiguration
{
    if (_task == nil || ![_task isRunning]) return;
    kill([_task processIdentifier], SIGHUP);
}

/* --- Pipe reading callbacks --- */

- (void)stdoutDataAvailable:(NSNotification *)note
{
    NSData *data = [[note userInfo]
        objectForKey:NSFileHandleNotificationDataItem];
    if (data && [data length] > 0) {
        [self processLogData:data source:LogSourceStdout];
        /* Continue reading */
        [[_stdoutPipe fileHandleForReading] readInBackgroundAndNotify];
    }
}

- (void)stderrDataAvailable:(NSNotification *)note
{
    NSData *data = [[note userInfo]
        objectForKey:NSFileHandleNotificationDataItem];
    if (data && [data length] > 0) {
        [self processLogData:data source:LogSourceStderr];
        /* Continue reading */
        [[_stderrPipe fileHandleForReading] readInBackgroundAndNotify];
    }
}

- (void)processLogData:(NSData *)data source:(LogSource)source
{
    NSString *text = [[NSString alloc] initWithData:data
                                           encoding:NSUTF8StringEncoding];
    if (!text) {
        [text release];
        return;
    }

    NSArray *lines = [text componentsSeparatedByString:@"\n"];
    unsigned i;
    for (i = 0; i < [lines count]; i++) {
        NSString *line = [lines objectAtIndex:i];
        if ([line length] == 0) continue;

        NSDictionary *info = [NSDictionary dictionaryWithObjectsAndKeys:
            line, @"text",
            [NSNumber numberWithInt:source], @"source",
            [NSDate date], @"timestamp",
            nil];

        [[NSNotificationCenter defaultCenter]
            postNotificationName:PMLogLineReceivedNotification
                          object:self
                        userInfo:info];
    }
    [text release];
}

/* --- Task termination --- */

- (void)taskDidTerminate:(NSNotification *)note
{
    int exitStatus = [_task terminationStatus];
    [self cleanupTask];

    if (exitStatus == 0 || exitStatus == SIGTERM || exitStatus == SIGINT) {
        [self setStatus:ServerStatusStopped];
    } else {
        [self setError:[NSString stringWithFormat:
            @"Server exited with code %d", exitStatus]];
    }
}

- (void)cleanupTask
{
    [_stdoutPipe release];
    _stdoutPipe = nil;
    [_stderrPipe release];
    _stderrPipe = nil;
    [_task release];
    _task = nil;
}

@end
