/*
 * AppController.h - Lemoniscate Server Admin GUI
 *
 * Maps to: MobiusAdmin AppState.swift + ContentView.swift
 *
 * Tiger-compatible Obj-C 1.0:
 * - Programmatic UI (no XIB/NIB)
 * - NSSplitView (left settings, right tabbed content)
 * - NSTabView for Server/Logs/Accounts/Online/Files/News tabs
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
    NSTableView *_trackerTableView;
    NSMutableArray *_trackerItems;
    NSTextField *_newTrackerField;
    NSButton *_addTrackerButton;
    NSButton *_removeTrackerButton;

    /* Files section */
    NSButton *_preserveForkCheckbox;
    NSTableView *_ignoreFilesTableView;
    NSMutableArray *_ignoreFileItems;
    NSTextField *_newIgnoreFileField;
    NSButton *_addIgnoreFileButton;
    NSButton *_removeIgnoreFileButton;

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
    NSView *_serverButtonsRow;
    NSButton *_startButton;
    NSButton *_stopButton;
    NSButton *_restartButton;
    NSButton *_reloadButton;
    NSButton *_setupWizardButton;

    /* Log tab */
    NSTextView *_logTextView;
    NSScrollView *_logScrollView;
    NSButton *_clearLogsButton;
    NSButton *_autoScrollCheckbox;
    NSTextField *_logFilterField;
    NSButton *_showStdoutCheckbox;
    NSButton *_showStderrCheckbox;
    NSMutableArray *_logEntries;
    BOOL _showStdout;
    BOOL _showStderr;
    BOOL _autoScroll;

    /* Accounts tab */
    NSSegmentedControl *_accountsSegmentedControl;
    NSView *_accountsUsersView;
    NSView *_accountsBansView;
    NSTextField *_accountsCountLabel;
    NSTableView *_accountsTableView;
    NSMutableArray *_accountsItems;
    NSTextField *_accountLoginField;
    NSTextField *_accountNameField;
    NSTextField *_accountFileRootField;
    NSPopUpButton *_accountTemplatePopup;
    NSButton *_accountNewButton;
    NSButton *_accountDeleteButton;
    NSButton *_accountSaveButton;
    NSMutableSet *_accountAccessKeys;
    NSString *_selectedAccountLogin;
    NSString *_selectedAccountPassword;

    NSMutableArray *_bannedIPs;
    NSMutableArray *_bannedUsers;
    NSMutableArray *_bannedNicks;
    NSTableView *_bannedIPsTableView;
    NSTableView *_bannedUsersTableView;
    NSTableView *_bannedNicksTableView;
    NSTextField *_newBanIPField;
    NSTextField *_newBanUserField;
    NSTextField *_newBanNickField;

    /* Online tab */
    NSTextField *_onlineStatusLabel;
    NSTableView *_onlineTableView;
    NSSplitView *_onlineSplitView;
    NSScrollView *_onlineLogScrollView;
    NSTextView *_onlineLogTextView;
    NSMutableDictionary *_onlineUsersByID;
    NSMutableArray *_onlineUsersItems;
    NSButton *_onlineBanButton;
    NSButton *_onlineRefreshButton;
    NSTimer *_onlineRefreshTimer;
    int _onlinePeakConnections;

    /* Files tab */
    NSTextField *_filesPathLabel;
    NSTableView *_filesTableView;
    NSMutableArray *_filesItems;
    NSString *_filesCurrentPath;

    /* News tab */
    NSSegmentedControl *_newsSegmentedControl;
    NSView *_newsContainerView;
    NSView *_newsMessageBoardView;
    NSView *_newsThreadedView;
    NSTextView *_messageBoardTextView;
    NSButton *_saveMessageBoardButton;
    NSTextField *_messageBoardDirtyLabel;
    BOOL _messageBoardDirty;
    NSTableView *_newsCategoriesTableView;
    NSTableView *_newsArticlesTableView;
    NSMutableArray *_newsCategoryItems;
    NSMutableArray *_newsArticleItems;
    NSMutableDictionary *_newsCategoriesByKey;
    NSTextField *_newsNewCategoryField;
    NSButton *_newsDeleteCategoryButton;
    NSString *_newsSelectedCategoryKey;

    /* Footer */
    NSImageView *_footerStatusDot;
    NSTextField *_footerStatusLabel;
    NSTextField *_footerPortLabel;
    NSTextField *_footerConnectedLabel;
    NSTextField *_footerPeakLabel;
    NSTextField *_footerDLLabel;
    NSTextField *_footerULLabel;
    unsigned long long _footerDownloadBytes;
    unsigned long long _footerUploadBytes;

    /* Native text editor window */
    NSWindow *_textEditorWindow;
    NSTextView *_textEditorTextView;
    NSString *_textEditorFilePath;

    /* Setup wizard window */
    NSWindow *_wizardWindow;
    NSView *_wizardStepContainer;
    NSTextField *_wizardStepLabel;
    NSProgressIndicator *_wizardProgress;
    NSButton *_wizardBackButton;
    NSButton *_wizardNextButton;
    NSButton *_wizardFinishButton;
    NSButton *_wizardFinishStartButton;
    NSButton *_wizardCancelButton;
    NSTextField *_wizardNameField;
    NSTextField *_wizardDescriptionField;
    NSTextField *_wizardPortField;
    NSTextField *_wizardFileRootField;
    NSTextField *_wizardBannerField;
    NSButton *_wizardBonjourCheckbox;
    NSButton *_wizardTrackerCheckbox;
    NSButton *_wizardPreserveForkCheckbox;
    NSTextField *_wizardMaxDownloadsField;
    NSTextField *_wizardMaxDLPerClientField;
    NSTextField *_wizardMaxConnPerIPField;
    NSTextView *_wizardSummaryTextView;
    int _wizardStepIndex;
    BOOL _wizardPresented;

    /* About / update panel */
    NSWindow *_aboutWindow;
    NSTextField *_aboutVersionLabel;
    NSTextField *_aboutUpdateLabel;
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
- (void)reloadServerConfig:(id)sender;
- (void)chooseConfigDir:(id)sender;
- (void)chooseFileRoot:(id)sender;
- (void)chooseBannerFile:(id)sender;
- (void)editAgreementFile:(id)sender;
- (void)editMessageBoardFile:(id)sender;
- (void)showSetupWizard:(id)sender;
- (void)showAboutPanel:(id)sender;
- (void)checkForUpdates:(id)sender;
- (void)addTracker:(id)sender;
- (void)removeTracker:(id)sender;
- (void)addIgnoreFilePattern:(id)sender;
- (void)removeIgnoreFilePattern:(id)sender;
- (void)saveSettings:(id)sender;
- (void)clearLogs:(id)sender;
- (void)logFilterChanged:(id)sender;
- (void)toggleStdoutVisibility:(id)sender;
- (void)toggleStderrVisibility:(id)sender;
- (void)refreshAccountsList:(id)sender;
- (void)accountsSegmentChanged:(id)sender;
- (void)newAccount:(id)sender;
- (void)saveAccount:(id)sender;
- (void)deleteAccount:(id)sender;
- (void)accountTemplateChanged:(id)sender;
- (void)addIPBan:(id)sender;
- (void)removeIPBan:(id)sender;
- (void)addUserBan:(id)sender;
- (void)removeUserBan:(id)sender;
- (void)addNickBan:(id)sender;
- (void)removeNickBan:(id)sender;
- (void)refreshOnlineUsers:(id)sender;
- (void)banSelectedOnlineUser:(id)sender;
- (void)refreshFilesList:(id)sender;
- (void)navigateFilesUp:(id)sender;
- (void)navigateFilesHome:(id)sender;
- (void)filesTableDoubleClick:(id)sender;
- (void)newsSegmentChanged:(id)sender;
- (void)saveMessageBoard:(id)sender;
- (void)refreshThreadedNews:(id)sender;
- (void)addNewsCategory:(id)sender;
- (void)deleteNewsCategory:(id)sender;

@end

#endif /* APP_CONTROLLER_H */
