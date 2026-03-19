/*
 * AppController.h - Lemoniscate Server Admin GUI
 *
 * Maps to: MobiusAdmin AppState.swift + ContentView.swift
 *
 * Tiger-compatible Obj-C 1.0:
 * - Programmatic UI (no XIB/NIB)
 * - NSSplitView (left settings, right tabbed content)
 * - NSTabView for Server/Logs tabs
 * - Manual retain/release
 */

#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#import <Cocoa/Cocoa.h>
#import "ProcessManager.h"

@interface AppController : NSObject
{
    /* Process management */
    ProcessManager *_processManager;

    /* Config state */
    NSString *_configDir;
    int _serverPort;
    NSString *_serverName;
    NSString *_serverDescription;

    /* Main window */
    NSWindow *_mainWindow;
    NSSplitView *_splitView;

    /* Left panel: settings */
    NSScrollView *_settingsScrollView;

    /* General section */
    NSTextField *_serverNameField;
    NSTextField *_descriptionField;
    NSTextField *_bannerFileField;
    NSTextField *_fileRootField;
    NSTextField *_configDirField;
    NSButton *_chooseConfigDirButton;
    NSButton *_chooseFileRootButton;
    NSButton *_chooseBannerButton;

    /* Network section */
    NSTextField *_portField;
    NSButton *_bonjourCheckbox;

    /* Tracker section */
    NSButton *_trackerCheckbox;

    /* Files section */
    NSButton *_preserveForkCheckbox;

    /* Limits section */
    NSTextField *_maxDownloadsField;
    NSTextField *_maxDLPerClientField;
    NSTextField *_maxConnPerIPField;

    /* Right panel: tab view */
    NSTabView *_tabView;

    /* Server tab */
    NSImageView *_statusIndicator;
    NSTextField *_statusLabel;
    NSTextField *_portInfoLabel;
    NSButton *_startButton;
    NSButton *_stopButton;
    NSButton *_restartButton;

    /* Log tab */
    NSTextView *_logTextView;
    NSScrollView *_logScrollView;
    NSButton *_clearLogsButton;
    NSButton *_autoScrollCheckbox;
    BOOL _autoScroll;

    /* Footer */
    NSImageView *_footerStatusDot;
    NSTextField *_footerStatusLabel;
    NSTextField *_footerPortLabel;
}

/* App lifecycle */
- (id)init;
- (void)dealloc;
- (void)applicationDidFinishLaunching:(NSNotification *)note;
- (void)applicationWillTerminate:(NSNotification *)note;
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app;

/* Actions */
- (void)startServer:(id)sender;
- (void)stopServer:(id)sender;
- (void)restartServer:(id)sender;
- (void)chooseConfigDir:(id)sender;
- (void)chooseFileRoot:(id)sender;
- (void)chooseBannerFile:(id)sender;
- (void)saveSettings:(id)sender;
- (void)clearLogs:(id)sender;

@end

#endif /* APP_CONTROLLER_H */
