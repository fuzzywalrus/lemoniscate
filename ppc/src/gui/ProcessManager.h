/*
 * ProcessManager.h - Server process lifecycle management
 *
 * Maps to: MobiusAdmin ProcessManager.swift
 *
 * Tiger-compatible Obj-C 1.0:
 * - Uses NSTask (Tiger API) to manage the lemoniscate server binary
 * - NSPipe for stdout/stderr capture
 * - NSNotifications for status updates
 * - Manual retain/release
 */

#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#import <Cocoa/Cocoa.h>

/* Server status enum */
typedef enum {
    ServerStatusStopped = 0,
    ServerStatusStarting,
    ServerStatusRunning,
    ServerStatusError
} ServerStatus;

/* Notification names */
extern NSString * const PMServerStatusChangedNotification;
extern NSString * const PMLogLineReceivedNotification;

/* Log line source */
typedef enum {
    LogSourceStdout = 0,
    LogSourceStderr
} LogSource;

@interface ProcessManager : NSObject
{
    NSTask *_task;
    NSPipe *_stdoutPipe;
    NSPipe *_stderrPipe;
    ServerStatus _status;
    NSString *_errorMessage;
    NSString *_binaryPath;
}

- (id)init;
- (void)dealloc;

/* Server lifecycle */
- (void)startWithConfigDir:(NSString *)configDir port:(int)port;
- (void)stop;
- (void)restartWithConfigDir:(NSString *)configDir port:(int)port;
- (void)reloadConfiguration;

/* Accessors */
- (ServerStatus)status;
- (NSString *)statusLabel;
- (NSString *)errorMessage;
- (BOOL)isRunning;

/* Binary location */
- (NSString *)binaryPath;
- (void)setBinaryPath:(NSString *)path;
- (BOOL)hasBinary;

@end

#endif /* PROCESS_MANAGER_H */
