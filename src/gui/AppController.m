/*
 * AppController.m - Lemoniscate Server Admin GUI
 *
 * Maps to: MobiusAdmin AppState + ContentView + ServerControlView +
 *          SettingsFormView + LogView
 *
 * Tiger Obj-C 1.0. All UI built programmatically.
 * Layout: HSplitView with scrollable settings left, tab view right.
 */

#import "AppController.h"

/* ===== FlippedView =====
 * NSView subclass with flipped coords so (0,0) is top-left. */

@interface FlippedView : NSView
@end

@implementation FlippedView
- (BOOL)isFlipped { return YES; }
@end

/* ===== Layout constants ===== */
#define LEFT_PANEL_WIDTH  560
#define SECTION_MARGIN    12
#define LABEL_WIDTH       100
#define FIELD_X           (SECTION_MARGIN + LABEL_WIDTH + 6)
#define ROW_RIGHT_PAD     8
#define ROW_HEIGHT        28

/* ===== Helpers ===== */

static NSTextField *makeLabel(NSString *text, float fontSize, BOOL bold)
{
    NSTextField *label = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [label setStringValue:text];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setFont:bold ? [NSFont boldSystemFontOfSize:fontSize]
                        : [NSFont systemFontOfSize:fontSize]];
    [label sizeToFit];
    return label;
}

static NSTextField *makeEditField(float width)
{
    NSTextField *field = [[NSTextField alloc]
        initWithFrame:NSMakeRect(0, 0, width, 22)];
    [field setFont:[NSFont systemFontOfSize:12.0]];
    return field;
}

static NSButton *makeButton(NSString *title, id target, SEL action)
{
    NSButton *btn = [[NSButton alloc]
        initWithFrame:NSMakeRect(0, 0, 80, 28)];
    [btn setTitle:title];
    [btn setBezelStyle:NSRoundedBezelStyle];
    [btn setTarget:target];
    [btn setAction:action];
    [btn sizeToFit];
    NSRect f = [btn frame];
    f.size.width += 20;
    [btn setFrame:f];
    return btn;
}

/* Add a right-aligned label + text field row inside an NSBox content view.
 * Returns next y position. */
static float addRow(NSView *parent, NSString *labelText,
                    NSTextField *field, float y, float fieldWidth)
{
    NSTextField *label = makeLabel(labelText, 11.0, NO);
    [label setAlignment:NSRightTextAlignment];
    [label setFrame:NSMakeRect(6, y + 3, LABEL_WIDTH - 12, 17)];
    [parent addSubview:label];
    [label release];

    [field setFrame:NSMakeRect(LABEL_WIDTH - 4, y,
                               fieldWidth - ROW_RIGHT_PAD, 22)];
    [parent addSubview:field];
    return y + ROW_HEIGHT;
}

/* Add a right-aligned label + text field + browse button row.
 * Returns next y position. */
static float addRowWithButton(NSView *parent, NSString *labelText,
                               NSTextField *field, NSButton *btn,
                               float y, float fieldWidth)
{
    NSTextField *label = makeLabel(labelText, 11.0, NO);
    [label setAlignment:NSRightTextAlignment];
    [label setFrame:NSMakeRect(6, y + 3, LABEL_WIDTH - 12, 17)];
    [parent addSubview:label];
    [label release];

    [btn setFont:[NSFont systemFontOfSize:11.0]];
    [btn sizeToFit];
    NSRect bf = [btn frame];
    float btnWidth = bf.size.width + 12.0f;
    if (btnWidth < 70.0f) btnWidth = 70.0f;
    if (btnWidth > 110.0f) btnWidth = 110.0f;

    float rightPad = ROW_RIGHT_PAD;
    float gap = 4.0f;
    [field setFrame:NSMakeRect(LABEL_WIDTH - 4, y,
                               fieldWidth - btnWidth - gap - rightPad, 22)];
    [parent addSubview:field];

    [btn setFrame:NSMakeRect(LABEL_WIDTH - 4 + fieldWidth - btnWidth - rightPad, y - 1,
                             btnWidth, 24)];
    [parent addSubview:btn];
    return y + ROW_HEIGHT;
}

/* Add a checkbox row. Returns next y. */
static float addCheckbox(NSView *parent, NSButton *checkbox,
                          float y)
{
    [checkbox setFrame:NSMakeRect(LABEL_WIDTH - 4, y, 200, 18)];
    [checkbox setFont:[NSFont systemFontOfSize:11.0]];
    [parent addSubview:checkbox];
    return y + 24;
}

/* Create a section box. */
static NSBox *makeSection(NSString *title, float x, float y,
                           float w, float h)
{
    NSBox *box = [[NSBox alloc]
        initWithFrame:NSMakeRect(x, y, w, h)];
    [box setTitle:title];
    [box setTitleFont:[NSFont boldSystemFontOfSize:11.0]];
    return box;
}

static NSString *trimmedString(NSString *s)
{
    if (!s) return @"";
    return [s stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static NSString *yamlUnquote(NSString *value)
{
    NSString *v = trimmedString(value);
    if ([v length] >= 2 &&
        (([v characterAtIndex:0] == '"' && [v characterAtIndex:[v length] - 1] == '"') ||
         ([v characterAtIndex:0] == '\'' && [v characterAtIndex:[v length] - 1] == '\''))) {
        return [v substringWithRange:NSMakeRange(1, [v length] - 2)];
    }
    return v;
}

static BOOL yamlBoolValue(NSString *value)
{
    NSString *v = [[trimmedString(value) lowercaseString] stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return [v isEqualToString:@"true"] || [v isEqualToString:@"yes"] || [v isEqualToString:@"1"];
}

static int leadingSpaces(NSString *s)
{
    unsigned i;
    int count = 0;
    for (i = 0; i < [s length]; i++) {
        unichar ch = [s characterAtIndex:i];
        if (ch == ' ') count++;
        else break;
    }
    return count;
}

static NSString *yamlQuoted(NSString *s)
{
    NSString *v = s ? s : @"";
    NSMutableString *m = [NSMutableString stringWithString:v];
    [m replaceOccurrencesOfString:@"\\"
                       withString:@"\\\\"
                          options:0
                            range:NSMakeRange(0, [m length])];
    [m replaceOccurrencesOfString:@"\""
                       withString:@"\\\""
                          options:0
                            range:NSMakeRange(0, [m length])];
    return [NSString stringWithFormat:@"\"%@\"", m];
}

static void parseInlineYAMLArray(NSString *value, NSMutableArray *outItems)
{
    if (!value || !outItems) return;
    NSString *v = trimmedString(value);
    if (![v hasPrefix:@"["] || ![v hasSuffix:@"]"]) return;
    if ([v length] < 2) return;

    NSString *inner = trimmedString([v substringWithRange:
        NSMakeRange(1, [v length] - 2)]);
    if ([inner length] == 0) return;

    NSArray *parts = [inner componentsSeparatedByString:@","];
    unsigned i;
    for (i = 0; i < [parts count]; i++) {
        NSString *item = yamlUnquote(trimmedString([parts objectAtIndex:i]));
        if ([item length] == 0) continue;
        if (![outItems containsObject:item]) [outItems addObject:item];
    }
}

static NSString *humanFileSize(unsigned long long size)
{
    double value = (double)size;
    if (value < 1024.0) return [NSString stringWithFormat:@"%llu B", size];
    value /= 1024.0;
    if (value < 1024.0) return [NSString stringWithFormat:@"%.1f KB", value];
    value /= 1024.0;
    if (value < 1024.0) return [NSString stringWithFormat:@"%.1f MB", value];
    value /= 1024.0;
    return [NSString stringWithFormat:@"%.1f GB", value];
}

static unsigned long long parseLogByteCount(NSString *text)
{
    if (!text || [text length] == 0) return 0;

    NSRange bytesRange = [text rangeOfString:@" bytes"];
    if (bytesRange.location == NSNotFound) return 0;

    NSRange openParen = [text rangeOfString:@"("
                                   options:NSBackwardsSearch
                                     range:NSMakeRange(0, bytesRange.location)];
    if (openParen.location == NSNotFound) return 0;

    NSString *inside = [text substringWithRange:
        NSMakeRange(openParen.location + 1,
                    bytesRange.location - openParen.location - 1)];
    inside = trimmedString(inside);
    if ([inside length] == 0) return 0;

    unsigned long long value = 0;
    unsigned i;
    for (i = 0; i < [inside length]; i++) {
        unichar ch = [inside characterAtIndex:i];
        if (ch < '0' || ch > '9') break;
        value = value * 10ULL + (unsigned long long)(ch - '0');
    }
    return value;
}

typedef struct {
    const char *group;
    const char *label;
    const char *key;
} AccountPermissionDef;

static const AccountPermissionDef kAccountPermissionDefs[] = {
    {"Files", "Download Files", "DownloadFile"},
    {"Files", "Download Folders", "DownloadFolder"},
    {"Files", "Upload Files", "UploadFile"},
    {"Files", "Upload Folders", "UploadFolder"},
    {"Files", "Upload Anywhere", "UploadAnywhere"},
    {"Files", "Delete Files", "DeleteFile"},
    {"Files", "Delete Folders", "DeleteFolder"},
    {"Files", "Rename Files", "RenameFile"},
    {"Files", "Rename Folders", "RenameFolder"},
    {"Files", "Move Files", "MoveFile"},
    {"Files", "Move Folders", "MoveFolder"},
    {"Files", "Create Folders", "CreateFolder"},
    {"Files", "Set File Comments", "SetFileComment"},
    {"Files", "Set Folder Comments", "SetFolderComment"},
    {"Files", "View Drop Boxes", "ViewDropBoxes"},
    {"Files", "Make Aliases", "MakeAlias"},

    {"Chat", "Read Chat", "ReadChat"},
    {"Chat", "Send Chat", "SendChat"},
    {"Chat", "Open Private Chat", "OpenChat"},
    {"Chat", "Close Chat", "CloseChat"},

    {"Users", "Create Accounts", "CreateUser"},
    {"Users", "Delete Accounts", "DeleteUser"},
    {"Users", "Read Accounts", "OpenUser"},
    {"Users", "Modify Accounts", "ModifyUser"},
    {"Users", "Change Own Password", "ChangeOwnPass"},
    {"Users", "Disconnect Users", "DisconnectUser"},
    {"Users", "Cannot Be Disconnected", "CannotBeDisconnected"},
    {"Users", "Get Client Info", "GetClientInfo"},
    {"Users", "Show In List", "ShowInList"},

    {"News", "Read Articles", "NewsReadArt"},
    {"News", "Post Articles", "NewsPostArt"},
    {"News", "Delete Articles", "NewsDeleteArt"},
    {"News", "Create Categories", "NewsCreateCat"},
    {"News", "Delete Categories", "NewsDeleteCat"},
    {"News", "Create News Bundles", "NewsCreateFldr"},
    {"News", "Delete News Bundles", "NewsDeleteFldr"},

    {"Messaging", "Send Private Messages", "SendPrivMsg"},
    {"Messaging", "Broadcast", "Broadcast"},

    {"Misc", "Use Any Name", "AnyName"},
    {"Misc", "No Agreement Required", "NoAgreement"}
};

static const unsigned kAccountPermissionDefCount =
    sizeof(kAccountPermissionDefs) / sizeof(kAccountPermissionDefs[0]);

@interface AppController ()
- (NSView *)createSettingsPanel;
- (NSView *)createRightPanel;
- (NSView *)createServerTabView;
- (NSView *)createLogsTabView;
- (NSView *)createAccountsTabView;
- (NSView *)createOnlineTabView;
- (NSView *)createFilesTabView;
- (NSView *)createNewsTabView;
- (void)layoutRightPanel;
- (void)createMainMenu;
- (void)createMainWindow;
- (void)updateServerUI;
- (void)updateFooterStats;
- (void)refreshLogTextView;
- (void)loadSettings;
- (void)loadAccountsListData;
- (void)loadBanListData;
- (void)writeBanListData;
- (NSDictionary *)loadAccountDataForLogin:(NSString *)login;
- (void)populateAccountEditorFromData:(NSDictionary *)acct;
- (void)populateAccountEditorForNewAccount;
- (void)rebuildAccountPermissionUI;
- (void)syncPermissionCheckboxesFromAccess;
- (void)updateAccountPasswordStatus;
- (void)updateAccountTemplateFromAccessKeys;
- (NSMutableSet *)guestAccessTemplate;
- (NSMutableSet *)adminAccessTemplate;
- (void)processOnlineLogLine:(NSString *)text;
- (void)updateOnlineUI;
- (NSString *)resolvedFileRootPath;
- (void)loadFilesAtPath:(NSString *)path;
- (void)loadMessageBoardText;
- (void)loadThreadedNewsCategories;
- (void)writeThreadedNewsCategories;
- (void)refreshThreadedNewsArticles;
- (NSString *)nextThreadedNewsArticleIDForCategory:(NSDictionary *)category;
- (void)openNewsArticleEditorForNew:(BOOL)isNew;
- (void)setMessageBoardDirty:(BOOL)dirty;
- (void)openTextConfigFileNamed:(NSString *)filename title:(NSString *)title;
- (void)saveTextEditor:(id)sender;
- (void)closeTextEditor:(id)sender;
- (void)wizardBack:(id)sender;
- (void)wizardNext:(id)sender;
- (void)wizardFinish:(id)sender;
- (void)wizardFinishAndStart:(id)sender;
- (void)closeWizard:(id)sender;
- (void)rebuildWizardStepUI;
- (BOOL)validateWizardStep:(BOOL)showAlert;
- (void)applyWizardValuesToSettings;
- (void)openProjectURL:(NSString *)urlString;
- (void)openServerRepository:(id)sender;
- (void)openGUIRepository:(id)sender;
- (void)openProjectHomepage:(id)sender;
- (void)openDownloadPage:(id)sender;
- (void)loadConfigFromDisk;
- (void)writeConfigToDisk;
- (void)ensureConfigScaffolding;
@end

@implementation AppController

- (id)init
{
    self = [super init];
    if (self) {
        _processManager = [[ProcessManager alloc] init];
        _configDir = [@"~/Library/Application Support/Lemoniscate/config"
                       stringByExpandingTildeInPath];
        [_configDir retain];
        _serverPort = 5500;
        _serverName = [@"Lemoniscate Server" retain];
        _serverDescription = [@"A Hotline server" retain];
        _autoScroll = YES;
        _showStdout = YES;
        _showStderr = YES;
        _accountsItems = [[NSMutableArray alloc] init];
        _logEntries = [[NSMutableArray alloc] init];
        _accountAccessKeys = [[NSMutableSet alloc] init];
        _accountPermissionCheckboxes = [[NSMutableDictionary alloc] init];
        _trackerItems = [[NSMutableArray alloc] init];
        _ignoreFileItems = [[NSMutableArray alloc] init];
        _bannedIPs = [[NSMutableArray alloc] init];
        _bannedUsers = [[NSMutableArray alloc] init];
        _bannedNicks = [[NSMutableArray alloc] init];
        _selectedAccountLogin = nil;
        _selectedAccountPassword = [@"" retain];
        _onlineUsersByID = [[NSMutableDictionary alloc] init];
        _onlineUsersItems = [[NSMutableArray alloc] init];
        _onlineRefreshTimer = nil;
        _onlinePeakConnections = 0;
        _footerDownloadBytes = 0;
        _footerUploadBytes = 0;
        _filesItems = [[NSMutableArray alloc] init];
        _newsCategoryItems = [[NSMutableArray alloc] init];
        _newsArticleItems = [[NSMutableArray alloc] init];
        _newsCategoriesByKey = [[NSMutableDictionary alloc] init];
        _newsSelectedCategoryKey = nil;
        _newsSelectedArticleID = nil;
        _filesCurrentPath = nil;
        _messageBoardDirty = NO;
        _newsDateFormatPopup = nil;
        _newsDelimiterField = nil;
        _textEditorWindow = nil;
        _textEditorTextView = nil;
        _textEditorFilePath = nil;
        _newAccountWindow = nil;
        _newAccountLoginField = nil;
        _newAccountNameField = nil;
        _newsArticleEditorWindow = nil;
        _newsArticleTitleField = nil;
        _newsArticlePosterField = nil;
        _newsArticleDateField = nil;
        _newsArticleBodyTextView = nil;
        _newsEditingCategoryKey = nil;
        _newsEditingArticleID = nil;
        _wizardWindow = nil;
        _wizardStepContainer = nil;
        _wizardStepLabel = nil;
        _wizardProgress = nil;
        _wizardBackButton = nil;
        _wizardNextButton = nil;
        _wizardFinishButton = nil;
        _wizardFinishStartButton = nil;
        _wizardCancelButton = nil;
        _wizardNameField = nil;
        _wizardDescriptionField = nil;
        _wizardPortField = nil;
        _wizardFileRootField = nil;
        _wizardBannerField = nil;
        _wizardBonjourCheckbox = nil;
        _wizardTrackerCheckbox = nil;
        _wizardPreserveForkCheckbox = nil;
        _wizardMaxDownloadsField = nil;
        _wizardMaxDLPerClientField = nil;
        _wizardMaxConnPerIPField = nil;
        _wizardSummaryTextView = nil;
        _wizardStepIndex = 0;
        _wizardPresented = NO;
        _aboutWindow = nil;
        _aboutVersionLabel = nil;
        _aboutUpdateLabel = nil;

        /* Find server binary (supports Lemoniscate + MobiusAdmin names). */
        NSBundle *bundle = [NSBundle mainBundle];
        NSFileManager *fm = [NSFileManager defaultManager];
        NSString *binPath = nil;
        NSArray *names = [NSArray arrayWithObjects:
            @"lemoniscate-server", @"mobius-hotline-server", @"lemoniscate", nil];
        unsigned idx;
        for (idx = 0; idx < [names count] && !binPath; idx++) {
            NSString *name = [names objectAtIndex:idx];
            NSString *candidate = [bundle pathForResource:name ofType:nil];
            if (candidate && [fm isExecutableFileAtPath:candidate]) {
                binPath = candidate;
            }
        }
        if (!binPath) {
            NSString *macOSDir = [[bundle bundlePath]
                stringByAppendingPathComponent:@"Contents/MacOS"];
            for (idx = 0; idx < [names count] && !binPath; idx++) {
                NSString *candidate = [macOSDir
                    stringByAppendingPathComponent:[names objectAtIndex:idx]];
                if ([fm isExecutableFileAtPath:candidate]) {
                    binPath = candidate;
                }
            }
        }
        if (!binPath) {
            NSString *appDir = [[bundle bundlePath]
                stringByDeletingLastPathComponent];
            for (idx = 0; idx < [names count] && !binPath; idx++) {
                NSString *candidate = [appDir
                    stringByAppendingPathComponent:[names objectAtIndex:idx]];
                if ([fm isExecutableFileAtPath:candidate]) {
                    binPath = candidate;
                }
            }
        }
        if (binPath)
            [_processManager setBinaryPath:binPath];

        [[NSNotificationCenter defaultCenter]
            addObserver:self selector:@selector(serverStatusChanged:)
                   name:PMServerStatusChangedNotification
                 object:_processManager];
        [[NSNotificationCenter defaultCenter]
            addObserver:self selector:@selector(logLineReceived:)
                   name:PMLogLineReceivedNotification
                 object:_processManager];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_accountsItems release];
    [_logEntries release];
    [_accountAccessKeys release];
    [_accountPermissionCheckboxes release];
    [_trackerItems release];
    [_ignoreFileItems release];
    [_bannedIPs release];
    [_bannedUsers release];
    [_bannedNicks release];
    [_selectedAccountLogin release];
    [_selectedAccountPassword release];
    if (_onlineRefreshTimer) {
        [_onlineRefreshTimer invalidate];
        [_onlineRefreshTimer release];
        _onlineRefreshTimer = nil;
    }
    [_onlineUsersByID release];
    [_onlineUsersItems release];
    [_filesItems release];
    [_newsCategoryItems release];
    [_newsArticleItems release];
    [_newsCategoriesByKey release];
    [_newsSelectedCategoryKey release];
    [_newsSelectedArticleID release];
    [_newsEditingCategoryKey release];
    [_newsEditingArticleID release];
    [_textEditorFilePath release];
    [_textEditorWindow release];
    [_newAccountWindow release];
    [_newsArticleEditorWindow release];
    [_wizardWindow release];
    [_aboutWindow release];
    [_filesCurrentPath release];
    [_processManager release];
    [_configDir release];
    [_serverName release];
    [_serverDescription release];
    [super dealloc];
}

/* ===== App lifecycle ===== */

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    (void)note;
    [self loadSettings];
    [self ensureConfigScaffolding];
    [self createMainMenu];
    [self createMainWindow];
    [self loadConfigFromDisk];
    [self refreshAccountsList:nil];
    [self loadBanListData];
    [self refreshFilesList:nil];
    [self loadMessageBoardText];
    [self refreshThreadedNews:nil];
    [self updateServerUI];

    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    if (![d objectForKey:@"setupWizardCompleted"]) {
        NSString *cfgPath = [_configDir stringByAppendingPathComponent:@"config.yaml"];
        BOOL hasConfig = [[NSFileManager defaultManager] fileExistsAtPath:cfgPath];
        [d setBool:hasConfig forKey:@"setupWizardCompleted"];
        [d synchronize];
    }
    if (![d boolForKey:@"setupWizardCompleted"]) {
        [self showSetupWizard:nil];
    }
}

- (void)applicationWillTerminate:(NSNotification *)note
{
    (void)note;
    [self saveSettings:nil];
    [_processManager stop];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app
{
    (void)app;
    return YES;
}

/* ===== Settings persistence ===== */

- (void)loadSettings
{
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    NSString *s;

    s = [d stringForKey:@"configDir"];
    if (s && [s length]) { [s retain]; [_configDir release]; _configDir = s; }

    int port = [d integerForKey:@"serverPort"];
    if (port > 0) _serverPort = port;

    s = [d stringForKey:@"serverName"];
    if (s && [s length]) { [s retain]; [_serverName release]; _serverName = s; }

    s = [d stringForKey:@"serverDescription"];
    if (s && [s length]) {
        [s retain]; [_serverDescription release]; _serverDescription = s;
    }

    s = [d stringForKey:@"binaryPath"];
    if (s && [s length]) [_processManager setBinaryPath:s];
}

- (void)loadConfigFromDisk
{
    NSString *cfgPath = [_configDir stringByAppendingPathComponent:@"config.yaml"];
    NSString *yaml = [NSString stringWithContentsOfFile:cfgPath
                                                encoding:NSUTF8StringEncoding
                                                   error:nil];
    if (!yaml || [yaml length] == 0) return;

    [_trackerItems removeAllObjects];
    [_ignoreFileItems removeAllObjects];
    NSArray *lines = [yaml componentsSeparatedByCharactersInSet:
        [NSCharacterSet newlineCharacterSet]];
    BOOL inTrackersList = NO;
    BOOL inIgnoreFilesList = NO;
    unsigned i;
    for (i = 0; i < [lines count]; i++) {
        NSString *rawLine = [lines objectAtIndex:i];
        NSString *line = trimmedString(rawLine);
        if ([line length] == 0 || [line hasPrefix:@"#"]) continue;

        if (inTrackersList || inIgnoreFilesList) {
            BOOL isListItem = [rawLine hasPrefix:@"  -"] || [rawLine hasPrefix:@"\t-"];
            if (isListItem) {
                NSString *item = trimmedString([line substringFromIndex:1]);
                item = yamlUnquote(item);
                if ([item length] > 0) {
                    if (inTrackersList && ![_trackerItems containsObject:item]) {
                        [_trackerItems addObject:item];
                    } else if (inIgnoreFilesList && ![_ignoreFileItems containsObject:item]) {
                        [_ignoreFileItems addObject:item];
                    }
                }
                continue;
            }
            inTrackersList = NO;
            inIgnoreFilesList = NO;
        }

        NSRange sep = [line rangeOfString:@":"];
        if (sep.location == NSNotFound) continue;

        NSString *key = trimmedString([line substringToIndex:sep.location]);
        NSString *val = yamlUnquote([line substringFromIndex:sep.location + 1]);

        if ([key isEqualToString:@"Name"]) {
            [val retain];
            [_serverName release];
            _serverName = val;
            if (_serverNameField) [_serverNameField setStringValue:_serverName];
        } else if ([key isEqualToString:@"Description"]) {
            [val retain];
            [_serverDescription release];
            _serverDescription = val;
            if (_descriptionField) [_descriptionField setStringValue:_serverDescription];
        } else if ([key isEqualToString:@"BannerFile"]) {
            if (_bannerFileField) [_bannerFileField setStringValue:val];
        } else if ([key isEqualToString:@"FileRoot"]) {
            if (_fileRootField) [_fileRootField setStringValue:val];
        } else if ([key isEqualToString:@"EnableBonjour"]) {
            if (_bonjourCheckbox) {
                [_bonjourCheckbox setState:yamlBoolValue(val) ? NSOnState : NSOffState];
            }
        } else if ([key isEqualToString:@"EnableTrackerRegistration"]) {
            if (_trackerCheckbox) {
                [_trackerCheckbox setState:yamlBoolValue(val) ? NSOnState : NSOffState];
            }
        } else if ([key isEqualToString:@"Trackers"]) {
            NSString *rawVal = trimmedString([line substringFromIndex:sep.location + 1]);
            if ([rawVal length] == 0) {
                inTrackersList = YES;
            } else if (![rawVal isEqualToString:@"[]"]) {
                parseInlineYAMLArray(rawVal, _trackerItems);
            }
        } else if ([key isEqualToString:@"IgnoreFiles"]) {
            NSString *rawVal = trimmedString([line substringFromIndex:sep.location + 1]);
            if ([rawVal length] == 0) {
                inIgnoreFilesList = YES;
            } else if (![rawVal isEqualToString:@"[]"]) {
                parseInlineYAMLArray(rawVal, _ignoreFileItems);
            }
        } else if ([key isEqualToString:@"NewsDateFormat"]) {
            if (_newsDateFormatPopup) {
                NSInteger idx = [_newsDateFormatPopup indexOfItemWithTitle:val];
                if (idx < 0 && [val length] > 0) {
                    [_newsDateFormatPopup addItemWithTitle:val];
                    idx = [_newsDateFormatPopup indexOfItemWithTitle:val];
                }
                if (idx >= 0) [_newsDateFormatPopup selectItemAtIndex:idx];
            }
        } else if ([key isEqualToString:@"NewsDelimiter"]) {
            if (_newsDelimiterField) [_newsDelimiterField setStringValue:val];
        } else if ([key isEqualToString:@"PreserveResourceForks"]) {
            if (_preserveForkCheckbox) {
                [_preserveForkCheckbox setState:yamlBoolValue(val) ? NSOnState : NSOffState];
            }
        } else if ([key isEqualToString:@"MaxDownloads"]) {
            if (_maxDownloadsField) [_maxDownloadsField setIntValue:[val intValue]];
        } else if ([key isEqualToString:@"MaxDownloadsPerClient"]) {
            if (_maxDLPerClientField) [_maxDLPerClientField setIntValue:[val intValue]];
        } else if ([key isEqualToString:@"MaxConnectionsPerIP"]) {
            if (_maxConnPerIPField) [_maxConnPerIPField setIntValue:[val intValue]];
        }
    }

    if (_filesTableView) [self refreshFilesList:nil];
    if (_accountsTableView) [self refreshAccountsList:nil];
    if (_bannedIPsTableView || _bannedUsersTableView || _bannedNicksTableView)
        [self loadBanListData];
    if (_trackerTableView) {
        [_trackerTableView reloadData];
        if (_removeTrackerButton) [_removeTrackerButton setEnabled:NO];
    }
    if (_ignoreFilesTableView) {
        [_ignoreFilesTableView reloadData];
        if (_removeIgnoreFileButton) [_removeIgnoreFileButton setEnabled:NO];
    }
    if (_messageBoardTextView) [self loadMessageBoardText];
    if (_newsCategoriesTableView) [self refreshThreadedNews:nil];
}

- (void)writeConfigToDisk
{
    NSString *name = _serverNameField ? [_serverNameField stringValue] : _serverName;
    NSString *desc = _descriptionField ? [_descriptionField stringValue] : _serverDescription;
    NSString *banner = _bannerFileField ? [_bannerFileField stringValue] : @"";
    NSString *fileRoot = _fileRootField ? [_fileRootField stringValue] : @"";
    BOOL enableBonjour = _bonjourCheckbox ? ([_bonjourCheckbox state] == NSOnState) : YES;
    BOOL enableTracker = _trackerCheckbox ? ([_trackerCheckbox state] == NSOnState) : NO;
    NSString *newsDateFormat = _newsDateFormatPopup ?
        [_newsDateFormatPopup titleOfSelectedItem] : @"Jan02 15:04";
    NSString *newsDelimiter = _newsDelimiterField ?
        [_newsDelimiterField stringValue] : @"__________________________________________________________";
    if (!newsDateFormat || [newsDateFormat length] == 0) {
        newsDateFormat = @"Jan02 15:04";
    }
    if (!newsDelimiter || [newsDelimiter length] == 0) {
        newsDelimiter = @"__________________________________________________________";
    }
    BOOL preserveForks = _preserveForkCheckbox ? ([_preserveForkCheckbox state] == NSOnState) : NO;
    int maxDownloads = _maxDownloadsField ? [_maxDownloadsField intValue] : 0;
    int maxDLPerClient = _maxDLPerClientField ? [_maxDLPerClientField intValue] : 0;
    int maxConnPerIP = _maxConnPerIPField ? [_maxConnPerIPField intValue] : 0;

    if (maxDownloads < 0) maxDownloads = 0;
    if (maxDLPerClient < 0) maxDLPerClient = 0;
    if (maxConnPerIP < 0) maxConnPerIP = 0;

    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:_configDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:_configDir attributes:[NSDictionary dictionary]];
    }

    NSString *cfgPath = [_configDir stringByAppendingPathComponent:@"config.yaml"];
    NSString *qName = [name stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qDesc = [desc stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qBanner = [banner stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qRoot = [fileRoot stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qNewsDate = [newsDateFormat stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qNewsDelim = [newsDelimiter stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];

    NSMutableString *yaml = [NSMutableString stringWithFormat:
        @"Name: \"%@\"\n"
        @"Description: \"%@\"\n"
        @"BannerFile: \"%@\"\n"
        @"FileRoot: \"%@\"\n"
        @"EnableTrackerRegistration: %@\n",
        qName, qDesc, qBanner, qRoot,
        enableTracker ? @"true" : @"false"];

    if ([_trackerItems count] == 0) {
        [yaml appendString:@"Trackers: []\n"];
    } else {
        unsigned i;
        [yaml appendString:@"Trackers:\n"];
        for (i = 0; i < [_trackerItems count]; i++) {
            NSString *tracker = [_trackerItems objectAtIndex:i];
            [yaml appendFormat:@"  - %@\n", yamlQuoted(tracker)];
        }
    }

    if ([_ignoreFileItems count] == 0) {
        [yaml appendString:@"IgnoreFiles: []\n"];
    } else {
        unsigned i;
        [yaml appendString:@"IgnoreFiles:\n"];
        for (i = 0; i < [_ignoreFileItems count]; i++) {
            NSString *pattern = [_ignoreFileItems objectAtIndex:i];
            [yaml appendFormat:@"  - %@\n", yamlQuoted(pattern)];
        }
    }

    [yaml appendFormat:
        @"NewsDateFormat: \"%@\"\n"
        @"NewsDelimiter: \"%@\"\n"
        @"EnableBonjour: %@\n"
        @"Encoding: macintosh\n"
        @"MaxDownloads: %d\n"
        @"MaxDownloadsPerClient: %d\n"
        @"MaxConnectionsPerIP: %d\n"
        @"PreserveResourceForks: %@\n",
        qNewsDate, qNewsDelim,
        enableBonjour ? @"true" : @"false",
        maxDownloads, maxDLPerClient, maxConnPerIP,
        preserveForks ? @"true" : @"false"];

    [yaml writeToFile:cfgPath atomically:YES
             encoding:NSUTF8StringEncoding error:nil];
}

- (void)ensureConfigScaffolding
{
    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:_configDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:_configDir attributes:[NSDictionary dictionary]];
    }

    NSString *filesDir = [_configDir stringByAppendingPathComponent:@"Files"];
    if (![fm fileExistsAtPath:filesDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:filesDir attributes:[NSDictionary dictionary]];
    }

    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    if (![fm fileExistsAtPath:usersDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:usersDir attributes:[NSDictionary dictionary]];
    }

    NSString *agreement = [_configDir stringByAppendingPathComponent:@"Agreement.txt"];
    if (![fm fileExistsAtPath:agreement]) {
        [@"Welcome to this Hotline server.\n"
            writeToFile:agreement atomically:YES
               encoding:NSUTF8StringEncoding error:nil];
    }

    NSString *board = [_configDir stringByAppendingPathComponent:@"MessageBoard.txt"];
    if (![fm fileExistsAtPath:board]) {
        [@"" writeToFile:board atomically:YES
               encoding:NSUTF8StringEncoding error:nil];
    }

    NSString *banlist = [_configDir stringByAppendingPathComponent:@"Banlist.yaml"];
    if (![fm fileExistsAtPath:banlist]) {
        [@"banList: {}\nbannedUsers: {}\nbannedNicks: {}\n"
            writeToFile:banlist atomically:YES
               encoding:NSUTF8StringEncoding error:nil];
    }

    NSString *threadedNews = [_configDir stringByAppendingPathComponent:@"ThreadedNews.yaml"];
    if (![fm fileExistsAtPath:threadedNews]) {
        [@"Categories: {}\n" writeToFile:threadedNews atomically:YES
                                encoding:NSUTF8StringEncoding error:nil];
    }

    NSString *admin = [usersDir stringByAppendingPathComponent:@"admin.yaml"];
    if (![fm fileExistsAtPath:admin]) {
        [@"Login: admin\n"
          @"Name: admin\n"
          @"Password: \"\"\n"
          @"Access:\n"
          @"  DownloadFile: true\n"
          @"  UploadFile: true\n"
          @"  ReadChat: true\n"
          @"  SendChat: true\n"
          @"  CreateUser: true\n"
          @"  DeleteUser: true\n"
          @"  OpenUser: true\n"
          @"  ModifyUser: true\n"
          @"  GetClientInfo: true\n"
          @"  DisconnectUser: true\n"
          @"  Broadcast: true\n"
          @"  CreateFolder: true\n"
          @"  DeleteFile: true\n"
          @"  OpenChat: true\n"
          @"  NewsReadArt: true\n"
          @"  NewsPostArt: true\n"
            writeToFile:admin atomically:YES
               encoding:NSUTF8StringEncoding error:nil];
    }

    NSString *guest = [usersDir stringByAppendingPathComponent:@"guest.yaml"];
    if (![fm fileExistsAtPath:guest]) {
        [@"Login: guest\n"
          @"Name: guest\n"
          @"Password: \"\"\n"
          @"Access:\n"
          @"  DownloadFile: true\n"
          @"  UploadFile: false\n"
          @"  ReadChat: true\n"
          @"  SendChat: true\n"
          @"  CreateUser: false\n"
          @"  DeleteUser: false\n"
          @"  OpenUser: false\n"
          @"  ModifyUser: false\n"
          @"  GetClientInfo: true\n"
          @"  DisconnectUser: false\n"
          @"  Broadcast: false\n"
          @"  CreateFolder: false\n"
          @"  DeleteFile: false\n"
          @"  OpenChat: true\n"
          @"  NewsReadArt: true\n"
          @"  NewsPostArt: true\n"
            writeToFile:guest atomically:YES
               encoding:NSUTF8StringEncoding error:nil];
    }
}

- (void)saveSettings:(id)sender
{
    (void)sender;
    if (_serverNameField) {
        [_serverName release];
        _serverName = [[_serverNameField stringValue] retain];
    }
    if (_descriptionField) {
        [_serverDescription release];
        _serverDescription = [[_descriptionField stringValue] retain];
    }
    if (_portField) {
        _serverPort = [_portField intValue];
        if (_serverPort < 1 || _serverPort > 65535) _serverPort = 5500;
    }

    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setObject:_configDir forKey:@"configDir"];
    [d setInteger:_serverPort forKey:@"serverPort"];
    [d setObject:_serverName forKey:@"serverName"];
    [d setObject:_serverDescription forKey:@"serverDescription"];
    if ([_processManager binaryPath])
        [d setObject:[_processManager binaryPath] forKey:@"binaryPath"];
    [d synchronize];

    [self writeConfigToDisk];
}

/* ===== Menu bar ===== */

- (void)createMainMenu
{
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    /* App menu */
    NSMenuItem *appItem = [[NSMenuItem alloc]
        initWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Lemoniscate"];
    NSMenuItem *mi;
    mi = [[NSMenuItem alloc] initWithTitle:@"About Lemoniscate"
        action:@selector(showAboutPanel:) keyEquivalent:@""];
    [mi setTarget:self];
    [appMenu addItem:mi];
    [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Check for Updates..."
        action:@selector(checkForUpdates:) keyEquivalent:@""];
    [mi setTarget:self];
    [appMenu addItem:mi];
    [mi release];

    [appMenu addItem:[NSMenuItem separatorItem]];

    mi = [[NSMenuItem alloc] initWithTitle:@"Project Homepage"
        action:@selector(openProjectHomepage:) keyEquivalent:@""];
    [mi setTarget:self];
    [appMenu addItem:mi];
    [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"GUI Repository"
        action:@selector(openGUIRepository:) keyEquivalent:@""];
    [mi setTarget:self];
    [appMenu addItem:mi];
    [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Server Repository"
        action:@selector(openServerRepository:) keyEquivalent:@""];
    [mi setTarget:self];
    [appMenu addItem:mi];
    [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Download Latest Build"
        action:@selector(openDownloadPage:) keyEquivalent:@""];
    [mi setTarget:self];
    [appMenu addItem:mi];
    [mi release];

    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit Lemoniscate"
                       action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [mainMenu addItem:appItem];
    [appMenu release]; [appItem release];

    /* Server menu */
    NSMenuItem *srvItem = [[NSMenuItem alloc]
        initWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *srvMenu = [[NSMenu alloc] initWithTitle:@"Server"];

    mi = [[NSMenuItem alloc] initWithTitle:@"Start Server"
        action:@selector(startServer:) keyEquivalent:@"r"];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Stop Server"
        action:@selector(stopServer:) keyEquivalent:@"."];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Restart Server"
        action:@selector(restartServer:) keyEquivalent:@"R"];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Reload Config"
        action:@selector(reloadServerConfig:) keyEquivalent:@"l"];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    [srvMenu addItem:[NSMenuItem separatorItem]];

    mi = [[NSMenuItem alloc] initWithTitle:@"Setup Wizard..."
        action:@selector(showSetupWizard:) keyEquivalent:@""];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    [srvMenu addItem:[NSMenuItem separatorItem]];

    mi = [[NSMenuItem alloc] initWithTitle:@"Save Settings"
        action:@selector(saveSettings:) keyEquivalent:@"s"];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    [srvItem setSubmenu:srvMenu];
    [mainMenu addItem:srvItem];
    [srvMenu release]; [srvItem release];

    [NSApp setMainMenu:mainMenu];
    [mainMenu release];
}

/* ===== Main window ===== */

- (void)createMainWindow
{
    NSRect frame = NSMakeRect(60, 60, 1280, 700);
    _mainWindow = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [_mainWindow setTitle:@"Lemoniscate"];
    [_mainWindow setMinSize:NSMakeSize(920, 500)];

    _splitView = [[NSSplitView alloc]
        initWithFrame:[[_mainWindow contentView] bounds]];
    [_splitView setVertical:YES];
    [_splitView setAutoresizesSubviews:YES];
    [_splitView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [_splitView setDividerStyle:NSSplitViewDividerStyleThin];
    [_splitView setDelegate:self];

    [_splitView addSubview:[self createSettingsPanel]];
    [_splitView addSubview:[self createRightPanel]];
    [_splitView setPosition:(float)LEFT_PANEL_WIDTH ofDividerAtIndex:0];
    [self splitView:_splitView resizeSubviewsWithOldSize:NSZeroSize];

    [[_mainWindow contentView] addSubview:_splitView];
    [_mainWindow center];
    [_mainWindow makeKeyAndOrderFront:nil];
}

- (float)splitView:(NSSplitView *)splitView
constrainMinCoordinate:(float)proposed
      ofSubviewAt:(int)dividerIndex
{
    (void)splitView;
    (void)dividerIndex;
    if (proposed < 430.0f) return 430.0f;
    return proposed;
}

- (float)splitView:(NSSplitView *)splitView
constrainMaxCoordinate:(float)proposed
      ofSubviewAt:(int)dividerIndex
{
    (void)dividerIndex;
    float minRightWidth = 420.0f;
    float maxLeft = [splitView bounds].size.width
        - [splitView dividerThickness] - minRightWidth;
    if (maxLeft < 430.0f) maxLeft = 430.0f;
    if (proposed > maxLeft) return maxLeft;
    return proposed;
}

- (void)splitView:(NSSplitView *)splitView
resizeSubviewsWithOldSize:(NSSize)oldSize
{
    (void)oldSize;
    NSArray *subs = [splitView subviews];
    if ([subs count] < 2) {
        [splitView adjustSubviews];
        return;
    }

    NSView *left = [subs objectAtIndex:0];
    NSView *right = [subs objectAtIndex:1];
    NSRect b = [splitView bounds];
    float divider = [splitView dividerThickness];

    float leftWidth = [left frame].size.width;
    float minLeft = 430.0f;
    float minRight = 420.0f;
    float maxLeft = b.size.width - divider - minRight;
    if (maxLeft < minLeft) maxLeft = minLeft;
    if (leftWidth < minLeft) leftWidth = minLeft;
    if (leftWidth > maxLeft) leftWidth = maxLeft;

    [left setFrame:NSMakeRect(0, 0, leftWidth, b.size.height)];
    [right setFrame:NSMakeRect(leftWidth + divider, 0,
                               b.size.width - leftWidth - divider,
                               b.size.height)];
    [self layoutRightPanel];
}

- (void)layoutRightPanel
{
    if (!_tabView) return;
    NSView *container = [_tabView superview];
    if (!container) return;

    NSRect cb = [container bounds];
    float footerH = 28.0f;
    float tabH = cb.size.height - footerH;
    if (tabH < 0.0f) tabH = 0.0f;
    [_tabView setFrame:NSMakeRect(0, footerH, cb.size.width, tabH)];

    NSRect content = [_tabView contentRect];
    int i, n = [_tabView numberOfTabViewItems];
    for (i = 0; i < n; i++) {
        NSTabViewItem *item = [_tabView tabViewItemAtIndex:i];
        NSView *tabView = [item view];
        [tabView setFrame:content];
        [tabView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    }

    if (_serverButtonsRow) {
        NSView *serverView = [_serverButtonsRow superview];
        if (serverView) {
            NSRect vf = [serverView bounds];
            NSRect bf = [_serverButtonsRow frame];
            bf.origin.x = (vf.size.width - bf.size.width) / 2.0f;
            if (bf.origin.x < 20.0f) bf.origin.x = 20.0f;
            [_serverButtonsRow setFrame:bf];
        }
    }
    [self updateFooterStats];
}

/* ===== Left panel: Settings ===== */

- (NSView *)createSettingsPanel
{
    NSView *outer = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, LEFT_PANEL_WIDTH, 650)];
    [outer setAutoresizesSubviews:YES];
    [outer setAutoresizingMask:NSViewHeightSizable];

    _settingsScrollView = [[NSScrollView alloc]
        initWithFrame:[outer bounds]];
    [_settingsScrollView setAutoresizingMask:
        (NSViewWidthSizable | NSViewHeightSizable)];
    [_settingsScrollView setHasVerticalScroller:YES];
    [_settingsScrollView setHasHorizontalScroller:NO];
    [_settingsScrollView setBorderType:NSNoBorder];
    [_settingsScrollView setDrawsBackground:NO];

    float docWidth = LEFT_PANEL_WIDTH - 20; /* account for scroller */
    float secWidth = docWidth - 2 * SECTION_MARGIN;
    float fieldWidth = secWidth - LABEL_WIDTH + 2;

    FlippedView *doc = [[FlippedView alloc]
        initWithFrame:NSMakeRect(0, 0, docWidth, 800)];
    [doc setAutoresizingMask:NSViewWidthSizable];

    float y = SECTION_MARGIN;

    /* ===== General ===== */
    {
        float boxH = 7 * ROW_HEIGHT + 20;
        NSBox *box = makeSection(@"General", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;

        _serverNameField = makeEditField(fieldWidth);
        [_serverNameField setStringValue:_serverName];
        iy = addRow(c, @"Server Name:", _serverNameField, iy, fieldWidth);

        _descriptionField = makeEditField(fieldWidth);
        [_descriptionField setStringValue:_serverDescription];
        iy = addRow(c, @"Description:", _descriptionField, iy, fieldWidth);

        _bannerFileField = makeEditField(fieldWidth);
        _chooseBannerButton = makeButton(@"Browse...", self,
                                          @selector(chooseBannerFile:));
        iy = addRowWithButton(c, @"Banner File:", _bannerFileField,
                               _chooseBannerButton, iy, fieldWidth);

        _fileRootField = makeEditField(fieldWidth);
        [_fileRootField setStringValue:@"Files"];
        _chooseFileRootButton = makeButton(@"Browse...", self,
                                            @selector(chooseFileRoot:));
        iy = addRowWithButton(c, @"File Root:", _fileRootField,
                               _chooseFileRootButton, iy, fieldWidth);

        _configDirField = makeEditField(fieldWidth);
        [_configDirField setStringValue:_configDir];
        [_configDirField setEditable:NO];
        [_configDirField setFont:[NSFont systemFontOfSize:10.0]];
        _chooseConfigDirButton = makeButton(@"Change...", self,
                                             @selector(chooseConfigDir:));
        iy = addRowWithButton(c, @"Config Dir:", _configDirField,
                               _chooseConfigDirButton, iy, fieldWidth);

        NSTextField *agreementField = makeEditField(fieldWidth);
        [agreementField setStringValue:@"Agreement.txt"];
        [agreementField setEditable:NO];
        [agreementField setFont:[NSFont systemFontOfSize:10.0]];
        NSButton *agreementButton = makeButton(@"Edit...", self,
                                               @selector(editAgreementFile:));
        iy = addRowWithButton(c, @"Agreement:", agreementField,
                              agreementButton, iy, fieldWidth);
        [agreementField release];
        [agreementButton release];

        NSTextField *boardField = makeEditField(fieldWidth);
        [boardField setStringValue:@"MessageBoard.txt"];
        [boardField setEditable:NO];
        [boardField setFont:[NSFont systemFontOfSize:10.0]];
        NSButton *boardButton = makeButton(@"Edit...", self,
                                           @selector(editMessageBoardFile:));
        iy = addRowWithButton(c, @"Message Board:", boardField,
                              boardButton, iy, fieldWidth);
        [boardField release];
        [boardButton release];

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Network ===== */
    {
        float boxH = 2 * ROW_HEIGHT + 16;
        NSBox *box = makeSection(@"Network", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;

        _portField = makeEditField(70);
        [_portField setIntValue:_serverPort];
        iy = addRow(c, @"Hotline Port:", _portField, iy, 70);

        _bonjourCheckbox = [[NSButton alloc]
            initWithFrame:NSZeroRect];
        [_bonjourCheckbox setButtonType:NSSwitchButton];
        [_bonjourCheckbox setTitle:@"Enable Bonjour"];
        [_bonjourCheckbox setState:NSOnState];
        iy = addCheckbox(c, _bonjourCheckbox, iy);

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Tracker Registration ===== */
    {
        float boxH = 154;
        NSBox *box = makeSection(@"Tracker Registration", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;

        _trackerCheckbox = [[NSButton alloc]
            initWithFrame:NSZeroRect];
        [_trackerCheckbox setButtonType:NSSwitchButton];
        [_trackerCheckbox setTitle:@"Enable Tracker Registration"];
        [_trackerCheckbox setState:NSOffState];
        iy = addCheckbox(c, _trackerCheckbox, iy);

        NSScrollView *tsv = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(LABEL_WIDTH - 4, iy + 2,
                                     fieldWidth - ROW_RIGHT_PAD, 78)];
        [tsv setHasVerticalScroller:YES];
        [tsv setBorderType:NSBezelBorder];
        _trackerTableView = [[NSTableView alloc]
            initWithFrame:NSMakeRect(0, 0, fieldWidth - ROW_RIGHT_PAD, 78)];
        NSTableColumn *tcol = [[NSTableColumn alloc] initWithIdentifier:@"tracker"];
        [[tcol headerCell] setStringValue:@"Trackers"];
        [tcol setWidth:fieldWidth - ROW_RIGHT_PAD - 20];
        [_trackerTableView addTableColumn:tcol];
        [tcol release];
        [_trackerTableView setDataSource:(id)self];
        [_trackerTableView setDelegate:(id)self];
        [_trackerTableView setAllowsMultipleSelection:NO];
        [tsv setDocumentView:_trackerTableView];
        [c addSubview:tsv];
        [tsv release];

        _newTrackerField = makeEditField(248);
        [_newTrackerField setFrame:NSMakeRect(LABEL_WIDTH - 4, iy + 84, 248, 22)];
        [c addSubview:_newTrackerField];

        _addTrackerButton = makeButton(@"Add", self, @selector(addTracker:));
        [_addTrackerButton setFrame:NSMakeRect(LABEL_WIDTH + 248, iy + 84, 50, 22)];
        [c addSubview:_addTrackerButton];

        _removeTrackerButton = makeButton(@"Remove", self, @selector(removeTracker:));
        [_removeTrackerButton setFrame:NSMakeRect(LABEL_WIDTH + 302, iy + 84, 70, 22)];
        [_removeTrackerButton setEnabled:NO];
        [c addSubview:_removeTrackerButton];

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Files ===== */
    {
        float boxH = 154;
        NSBox *box = makeSection(@"Files", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;

        _preserveForkCheckbox = [[NSButton alloc]
            initWithFrame:NSZeroRect];
        [_preserveForkCheckbox setButtonType:NSSwitchButton];
        [_preserveForkCheckbox setTitle:@"Preserve Resource Forks"];
        [_preserveForkCheckbox setState:NSOffState];
        iy = addCheckbox(c, _preserveForkCheckbox, iy);

        NSScrollView *isv = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(LABEL_WIDTH - 4, iy + 2,
                                     fieldWidth - ROW_RIGHT_PAD, 78)];
        [isv setHasVerticalScroller:YES];
        [isv setBorderType:NSBezelBorder];
        _ignoreFilesTableView = [[NSTableView alloc]
            initWithFrame:NSMakeRect(0, 0, fieldWidth - ROW_RIGHT_PAD, 78)];
        NSTableColumn *icol = [[NSTableColumn alloc] initWithIdentifier:@"ignore_pattern"];
        [[icol headerCell] setStringValue:@"Ignore Patterns"];
        [icol setWidth:fieldWidth - ROW_RIGHT_PAD - 20];
        [_ignoreFilesTableView addTableColumn:icol];
        [icol release];
        [_ignoreFilesTableView setDataSource:(id)self];
        [_ignoreFilesTableView setDelegate:(id)self];
        [_ignoreFilesTableView setAllowsMultipleSelection:NO];
        [isv setDocumentView:_ignoreFilesTableView];
        [c addSubview:isv];
        [isv release];

        _newIgnoreFileField = makeEditField(248);
        [_newIgnoreFileField setFrame:NSMakeRect(LABEL_WIDTH - 4, iy + 84, 248, 22)];
        [c addSubview:_newIgnoreFileField];

        _addIgnoreFileButton = makeButton(@"Add", self, @selector(addIgnoreFilePattern:));
        [_addIgnoreFileButton setFrame:NSMakeRect(LABEL_WIDTH + 248, iy + 84, 50, 22)];
        [c addSubview:_addIgnoreFileButton];

        _removeIgnoreFileButton = makeButton(@"Remove", self, @selector(removeIgnoreFilePattern:));
        [_removeIgnoreFileButton setFrame:NSMakeRect(LABEL_WIDTH + 302, iy + 84, 70, 22)];
        [_removeIgnoreFileButton setEnabled:NO];
        [c addSubview:_removeIgnoreFileButton];

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Limits ===== */
    {
        float boxH = 2 * ROW_HEIGHT + 20;
        NSBox *box = makeSection(@"News", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;

        _newsDateFormatPopup = [[NSPopUpButton alloc]
            initWithFrame:NSMakeRect(0, 0, 220, 24) pullsDown:NO];
        [_newsDateFormatPopup addItemWithTitle:@"Jan02 15:04"];
        [_newsDateFormatPopup addItemWithTitle:@"01/02/2006 3:04 PM"];
        [_newsDateFormatPopup addItemWithTitle:@"02/01/2006 15:04"];
        [_newsDateFormatPopup addItemWithTitle:@"2006-01-02 15:04"];
        [_newsDateFormatPopup addItemWithTitle:@"Mon Jan 2 15:04:05"];
        [_newsDateFormatPopup selectItemAtIndex:0];
        NSTextField *dateFmtLabel = makeLabel(@"Date Format:", 11.0, NO);
        [dateFmtLabel setAlignment:NSRightTextAlignment];
        [dateFmtLabel setFrame:NSMakeRect(6, iy + 3, LABEL_WIDTH - 12, 17)];
        [c addSubview:dateFmtLabel];
        [dateFmtLabel release];
        [_newsDateFormatPopup setFrame:NSMakeRect(LABEL_WIDTH - 4, iy, 220, 24)];
        [c addSubview:_newsDateFormatPopup];
        iy += ROW_HEIGHT;

        _newsDelimiterField = makeEditField(fieldWidth);
        [_newsDelimiterField setStringValue:@"__________________________________________________________"];
        iy = addRow(c, @"Delimiter:", _newsDelimiterField, iy, fieldWidth);

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Limits ===== */
    {
        float boxH = 3 * ROW_HEIGHT + 24;
        NSBox *box = makeSection(@"Limits", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;

        _maxDownloadsField = makeEditField(60);
        [_maxDownloadsField setIntValue:0];
        iy = addRow(c, @"Max Downloads:", _maxDownloadsField, iy, 60);

        _maxDLPerClientField = makeEditField(60);
        [_maxDLPerClientField setIntValue:0];
        iy = addRow(c, @"Max DL/Client:", _maxDLPerClientField, iy, 60);

        _maxConnPerIPField = makeEditField(60);
        [_maxConnPerIPField setIntValue:0];
        iy = addRow(c, @"Max Conn/IP:", _maxConnPerIPField, iy, 60);

        /* Hint text */
        NSTextField *hint = makeLabel(@"Set to 0 for unlimited", 10.0, NO);
        [hint setTextColor:[NSColor grayColor]];
        [hint setFrame:NSMakeRect(LABEL_WIDTH - 4, iy, 150, 14)];
        [c addSubview:hint];
        [hint release];

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Server Binary ===== */
    {
        float boxH = 38;
        NSBox *box = makeSection(@"Server Binary", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];

        NSString *binMsg;
        NSColor *binColor;
        if ([_processManager hasBinary]) {
            binMsg = [NSString stringWithFormat:@"Found: %@",
                [[_processManager binaryPath] lastPathComponent]];
            binColor = [NSColor colorWithCalibratedRed:0.0
                green:0.5 blue:0.0 alpha:1.0];
        } else {
            binMsg = @"Not found - place lemoniscate-server or mobius-hotline-server in bundle";
            binColor = [NSColor redColor];
        }
        NSTextField *bl = makeLabel(binMsg, 10.0, NO);
        [bl setTextColor:binColor];
        [bl setFrame:NSMakeRect(6, 2, secWidth - 28, 14)];
        [c addSubview:bl];
        [bl release];

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* Size the document view */
    [doc setFrame:NSMakeRect(0, 0, docWidth, y + SECTION_MARGIN)];
    [_settingsScrollView setDocumentView:doc];
    [doc release];
    [outer addSubview:_settingsScrollView];

    return [outer autorelease];
}

/* ===== Right panel ===== */

- (NSView *)createRightPanel
{
    NSView *container = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 650, 650)];
    [container setAutoresizesSubviews:YES];
    [container setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    /* Footer (28px) */
    NSView *footer = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 650, 28)];
    [footer setAutoresizingMask:(NSViewWidthSizable | NSViewMaxYMargin)];

    NSBox *dot = [[NSBox alloc] initWithFrame:NSMakeRect(10, 9, 10, 10)];
    [dot setBoxType:NSBoxCustom];
    [dot setBorderType:NSLineBorder];
    [dot setFillColor:[NSColor grayColor]];
    [dot setCornerRadius:5.0];
    [footer addSubview:dot];
    _footerStatusDot = (NSImageView *)dot;

    _footerStatusLabel = makeLabel(@"Stopped", 11.0, NO);
    [_footerStatusLabel setFrameOrigin:NSMakePoint(26, 6)];
    [_footerStatusLabel setTextColor:[NSColor grayColor]];
    [footer addSubview:_footerStatusLabel];

    _footerPortLabel = makeLabel(@"", 11.0, NO);
    [_footerPortLabel setFrameOrigin:NSMakePoint(120, 6)];
    [_footerPortLabel setTextColor:[NSColor grayColor]];
    [footer addSubview:_footerPortLabel];

    _footerConnectedLabel = makeLabel(@"Connected: 0", 11.0, NO);
    [_footerConnectedLabel setTextColor:[NSColor grayColor]];
    [footer addSubview:_footerConnectedLabel];

    _footerPeakLabel = makeLabel(@"Peak: 0", 11.0, NO);
    [_footerPeakLabel setTextColor:[NSColor grayColor]];
    [footer addSubview:_footerPeakLabel];

    _footerDLLabel = makeLabel(@"DL: 0 B", 11.0, NO);
    [_footerDLLabel setTextColor:[NSColor grayColor]];
    [footer addSubview:_footerDLLabel];

    _footerULLabel = makeLabel(@"UL: 0 B", 11.0, NO);
    [_footerULLabel setTextColor:[NSColor grayColor]];
    [footer addSubview:_footerULLabel];

    [container addSubview:footer];
    [footer release];

    /* Tab view */
    _tabView = [[NSTabView alloc]
        initWithFrame:NSMakeRect(0, 28, 650, 622)];
    [_tabView setAutoresizesSubviews:YES];
    [_tabView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSTabViewItem *tab;

    tab = [[NSTabViewItem alloc] initWithIdentifier:@"server"];
    [tab setLabel:@"Server"];
    [tab setView:[self createServerTabView]];
    [_tabView addTabViewItem:tab];
    [tab release];

    tab = [[NSTabViewItem alloc] initWithIdentifier:@"logs"];
    [tab setLabel:@"Logs"];
    [tab setView:[self createLogsTabView]];
    [_tabView addTabViewItem:tab];
    [tab release];

    tab = [[NSTabViewItem alloc] initWithIdentifier:@"accounts"];
    [tab setLabel:@"Accounts"];
    [tab setView:[self createAccountsTabView]];
    [_tabView addTabViewItem:tab];
    [tab release];

    tab = [[NSTabViewItem alloc] initWithIdentifier:@"online"];
    [tab setLabel:@"Online"];
    [tab setView:[self createOnlineTabView]];
    [_tabView addTabViewItem:tab];
    [tab release];

    tab = [[NSTabViewItem alloc] initWithIdentifier:@"files"];
    [tab setLabel:@"Files"];
    [tab setView:[self createFilesTabView]];
    [_tabView addTabViewItem:tab];
    [tab release];

    tab = [[NSTabViewItem alloc] initWithIdentifier:@"news"];
    [tab setLabel:@"News"];
    [tab setView:[self createNewsTabView]];
    [_tabView addTabViewItem:tab];
    [tab release];

    [container addSubview:_tabView];
    [self layoutRightPanel];
    [self updateFooterStats];
    return [container autorelease];
}

/* ===== Server tab ===== */

- (NSView *)createServerTabView
{
    NSView *view = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 590)];
    [view setAutoresizesSubviews:YES];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    /* Use width-sizable + centered vertically so elements stretch/center
     * properly when the split view is resized. */

    /* Status dot — centered horizontally */
    NSBox *sDot = [[NSBox alloc]
        initWithFrame:NSMakeRect(307, 330, 16, 16)];
    [sDot setBoxType:NSBoxCustom];
    [sDot setBorderType:NSLineBorder];
    [sDot setFillColor:[NSColor grayColor]];
    [sDot setCornerRadius:8.0];
    [sDot setAutoresizingMask:(NSViewMinXMargin | NSViewMaxXMargin |
                               NSViewMinYMargin | NSViewMaxYMargin)];
    [view addSubview:sDot];
    _statusIndicator = (NSImageView *)sDot;

    /* Status text — full width, centered */
    _statusLabel = makeLabel(@"Server is stopped", 20.0, NO);
    [_statusLabel setAlignment:NSCenterTextAlignment];
    [_statusLabel setFrame:NSMakeRect(20, 290,
                                      [view bounds].size.width - 40, 30)];
    [_statusLabel setAutoresizingMask:(NSViewWidthSizable |
                                       NSViewMinYMargin | NSViewMaxYMargin)];
    [view addSubview:_statusLabel];

    /* Port info — full width, centered */
    _portInfoLabel = makeLabel(@"", 12.0, NO);
    [_portInfoLabel setAlignment:NSCenterTextAlignment];
    [_portInfoLabel setTextColor:[NSColor grayColor]];
    [_portInfoLabel setFrame:NSMakeRect(20, 264,
                                        [view bounds].size.width - 40, 20)];
    [_portInfoLabel setAutoresizingMask:(NSViewWidthSizable |
                                         NSViewMinYMargin | NSViewMaxYMargin)];
    [view addSubview:_portInfoLabel];

    /* Buttons in a centered row with intrinsic widths to prevent overlap. */
    float btnGap = 12.0f;
    _startButton = makeButton(@"Start", self, @selector(startServer:));
    _stopButton = makeButton(@"Stop", self, @selector(stopServer:));
    _restartButton = makeButton(@"Restart", self, @selector(restartServer:));
    _reloadButton = makeButton(@"Reload Config", self,
                               @selector(reloadServerConfig:));
    _setupWizardButton = makeButton(@"Setup Wizard", self,
                                    @selector(showSetupWizard:));

    float startW = [_startButton frame].size.width;
    float stopW = [_stopButton frame].size.width;
    float restartW = [_restartButton frame].size.width;
    float reloadW = [_reloadButton frame].size.width;
    float wizardW = [_setupWizardButton frame].size.width;
    if (startW < 72.0f) startW = 72.0f;
    if (stopW < 72.0f) stopW = 72.0f;
    if (restartW < 84.0f) restartW = 84.0f;
    if (reloadW < 108.0f) reloadW = 108.0f;
    if (wizardW < 112.0f) wizardW = 112.0f;

    float btnH = 30.0f;
    float totalBtnW = startW + stopW + restartW + reloadW + wizardW
        + 4.0f * btnGap;
    NSView *buttonRow = [[NSView alloc]
        initWithFrame:NSMakeRect(([view bounds].size.width - totalBtnW) / 2.0,
                                 222, totalBtnW, btnH)];
    [buttonRow setAutoresizingMask:(NSViewMinYMargin | NSViewMaxYMargin)];
    [view addSubview:buttonRow];
    _serverButtonsRow = buttonRow;

    [_startButton setFrame:NSMakeRect(0, 0, startW, btnH)];
    [buttonRow addSubview:_startButton];

    [_stopButton setFrame:NSMakeRect(startW + btnGap, 0, stopW, btnH)];
    [buttonRow addSubview:_stopButton];

    [_restartButton setFrame:NSMakeRect(startW + stopW + 2.0f * btnGap,
                                        0, restartW, btnH)];
    [buttonRow addSubview:_restartButton];

    [_reloadButton setFrame:NSMakeRect(startW + stopW + restartW + 3.0f * btnGap,
                                       0, reloadW, btnH)];
    [buttonRow addSubview:_reloadButton];

    [_setupWizardButton setFrame:NSMakeRect(startW + stopW + restartW + reloadW
                                            + 4.0f * btnGap,
                                            0, wizardW, btnH)];
    [buttonRow addSubview:_setupWizardButton];
    [buttonRow release];

    if (![_processManager hasBinary]) {
        NSTextField *w = makeLabel(
            @"Server binary not found in app bundle (lemoniscate-server or mobius-hotline-server).",
            11.0, NO);
        [w setTextColor:[NSColor redColor]];
        [w setAlignment:NSCenterTextAlignment];
        [w setFrame:NSMakeRect(20, 185, [view bounds].size.width - 40, 20)];
        [w setAutoresizingMask:(NSViewWidthSizable |
                                NSViewMinYMargin | NSViewMaxYMargin)];
        [view addSubview:w];
        [w release];
    }

    return [view autorelease];
}

/* ===== Logs tab ===== */

- (NSView *)createLogsTabView
{
    NSView *view = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 590)];
    [view setAutoresizesSubviews:YES];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    /* Toolbar */
    NSView *tb = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 560, 630, 30)];
    [tb setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    _autoScrollCheckbox = [[NSButton alloc]
        initWithFrame:NSMakeRect(8, 6, 110, 18)];
    [_autoScrollCheckbox setButtonType:NSSwitchButton];
    [_autoScrollCheckbox setTitle:@"Auto-scroll"];
    [_autoScrollCheckbox setState:NSOnState];
    [_autoScrollCheckbox setFont:[NSFont systemFontOfSize:11.0]];
    [_autoScrollCheckbox setTarget:self];
    [_autoScrollCheckbox setAction:@selector(toggleAutoScroll:)];
    [tb addSubview:_autoScrollCheckbox];

    NSTextField *filterLabel = makeLabel(@"Filter:", 11.0, NO);
    [filterLabel setFrame:NSMakeRect(124, 7, 36, 16)];
    [tb addSubview:filterLabel];
    [filterLabel release];

    _logFilterField = makeEditField(190);
    [_logFilterField setFrame:NSMakeRect(162, 4, 190, 22)];
    [_logFilterField setDelegate:(id)self];
    [tb addSubview:_logFilterField];

    _showStdoutCheckbox = [[NSButton alloc]
        initWithFrame:NSMakeRect(360, 6, 62, 18)];
    [_showStdoutCheckbox setButtonType:NSSwitchButton];
    [_showStdoutCheckbox setTitle:@"stdout"];
    [_showStdoutCheckbox setState:NSOnState];
    [_showStdoutCheckbox setFont:[NSFont systemFontOfSize:11.0]];
    [_showStdoutCheckbox setTarget:self];
    [_showStdoutCheckbox setAction:@selector(toggleStdoutVisibility:)];
    [tb addSubview:_showStdoutCheckbox];

    _showStderrCheckbox = [[NSButton alloc]
        initWithFrame:NSMakeRect(428, 6, 58, 18)];
    [_showStderrCheckbox setButtonType:NSSwitchButton];
    [_showStderrCheckbox setTitle:@"stderr"];
    [_showStderrCheckbox setState:NSOnState];
    [_showStderrCheckbox setFont:[NSFont systemFontOfSize:11.0]];
    [_showStderrCheckbox setTarget:self];
    [_showStderrCheckbox setAction:@selector(toggleStderrVisibility:)];
    [tb addSubview:_showStderrCheckbox];

    _clearLogsButton = makeButton(@"Clear", self, @selector(clearLogs:));
    [_clearLogsButton setFrame:NSMakeRect(548, 3, 72, 24)];
    [_clearLogsButton setAutoresizingMask:NSViewMinXMargin];
    [tb addSubview:_clearLogsButton];

    [view addSubview:tb];
    [tb release];

    /* Log text */
    _logScrollView = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(4, 4, 622, 554)];
    [_logScrollView setAutoresizingMask:
        (NSViewWidthSizable | NSViewHeightSizable)];
    [_logScrollView setHasVerticalScroller:YES];
    [_logScrollView setBorderType:NSBezelBorder];

    NSSize cs = [_logScrollView contentSize];
    _logTextView = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0, 0, cs.width, cs.height)];
    [_logTextView setMinSize:NSMakeSize(0, cs.height)];
    [_logTextView setMaxSize:NSMakeSize(1e7, 1e7)];
    [_logTextView setVerticallyResizable:YES];
    [_logTextView setHorizontallyResizable:NO];
    [_logTextView setAutoresizingMask:NSViewWidthSizable];
    [[_logTextView textContainer]
        setContainerSize:NSMakeSize(cs.width, 1e7)];
    [[_logTextView textContainer] setWidthTracksTextView:YES];
    [_logTextView setEditable:NO];
    [_logTextView setFont:[NSFont fontWithName:@"Monaco" size:10.0]];

    [_logScrollView setDocumentView:_logTextView];
    [view addSubview:_logScrollView];

    return [view autorelease];
}

/* ===== Accounts tab ===== */

- (NSView *)createAccountsTabView
{
    NSView *view = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 590)];
    [view setAutoresizesSubviews:YES];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSView *tb = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 560, 630, 30)];
    [tb setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    _accountsSegmentedControl = [[NSSegmentedControl alloc]
        initWithFrame:NSMakeRect(8, 4, 150, 22)];
    [_accountsSegmentedControl setSegmentCount:2];
    [_accountsSegmentedControl setLabel:@"Users" forSegment:0];
    [_accountsSegmentedControl setLabel:@"Bans" forSegment:1];
    [_accountsSegmentedControl setSelectedSegment:0];
    [_accountsSegmentedControl setTarget:self];
    [_accountsSegmentedControl setAction:@selector(accountsSegmentChanged:)];
    [tb addSubview:_accountsSegmentedControl];

    _accountsCountLabel = makeLabel(@"0 accounts", 11.0, NO);
    [_accountsCountLabel setFrame:NSMakeRect(170, 7, 280, 16)];
    [tb addSubview:_accountsCountLabel];

    NSButton *refreshBtn = makeButton(@"Refresh", self,
                                      @selector(refreshAccountsList:));
    [refreshBtn setFrame:NSMakeRect(540, 3, 80, 24)];
    [refreshBtn setAutoresizingMask:NSViewMinXMargin];
    [tb addSubview:refreshBtn];
    [refreshBtn release];

    [view addSubview:tb];
    [tb release];

    _accountsUsersView = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 560)];
    [_accountsUsersView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [view addSubview:_accountsUsersView];

    NSScrollView *sv = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(4, 4, 212, 552)];
    [sv setAutoresizingMask:(NSViewHeightSizable)];
    [sv setHasVerticalScroller:YES];
    [sv setBorderType:NSBezelBorder];

    _accountsTableView = [[NSTableView alloc]
        initWithFrame:NSMakeRect(0, 0, 212, 552)];
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"login"];
    [[col headerCell] setStringValue:@"Login"];
    [col setWidth:190];
    [_accountsTableView addTableColumn:col];
    [col release];
    [_accountsTableView setDataSource:(id)self];
    [_accountsTableView setDelegate:(id)self];
    [_accountsTableView setAllowsMultipleSelection:NO];
    [sv setDocumentView:_accountsTableView];
    [_accountsUsersView addSubview:sv];
    [sv release];

    NSBox *editor = [[NSBox alloc]
        initWithFrame:NSMakeRect(220, 4, 406, 552)];
    [editor setTitle:@"Account Editor"];
    [editor setTitleFont:[NSFont boldSystemFontOfSize:11.0]];
    [_accountsUsersView addSubview:editor];

    NSView *ec = [editor contentView];
    NSTextField *l;

    l = makeLabel(@"Login:", 11.0, NO);
    [l setAlignment:NSRightTextAlignment];
    [l setFrame:NSMakeRect(6, 496, 70, 17)];
    [ec addSubview:l];
    [l release];
    _accountLoginField = makeEditField(290);
    [_accountLoginField setFrame:NSMakeRect(84, 492, 290, 22)];
    [_accountLoginField setEditable:NO];
    [ec addSubview:_accountLoginField];

    l = makeLabel(@"Display Name:", 11.0, NO);
    [l setAlignment:NSRightTextAlignment];
    [l setFrame:NSMakeRect(6, 464, 70, 17)];
    [ec addSubview:l];
    [l release];
    _accountNameField = makeEditField(290);
    [_accountNameField setFrame:NSMakeRect(84, 460, 290, 22)];
    [ec addSubview:_accountNameField];

    l = makeLabel(@"File Root:", 11.0, NO);
    [l setAlignment:NSRightTextAlignment];
    [l setFrame:NSMakeRect(6, 432, 70, 17)];
    [ec addSubview:l];
    [l release];
    _accountFileRootField = makeEditField(290);
    [_accountFileRootField setFrame:NSMakeRect(84, 428, 290, 22)];
    [ec addSubview:_accountFileRootField];

    l = makeLabel(@"Template:", 11.0, NO);
    [l setAlignment:NSRightTextAlignment];
    [l setFrame:NSMakeRect(6, 400, 70, 17)];
    [ec addSubview:l];
    [l release];
    _accountTemplatePopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(84, 396, 140, 24) pullsDown:NO];
    [_accountTemplatePopup addItemWithTitle:@"Custom"];
    [_accountTemplatePopup addItemWithTitle:@"Guest"];
    [_accountTemplatePopup addItemWithTitle:@"Admin"];
    [_accountTemplatePopup setTarget:self];
    [_accountTemplatePopup setAction:@selector(accountTemplateChanged:)];
    [ec addSubview:_accountTemplatePopup];

    _accountNewButton = makeButton(@"New", self, @selector(newAccount:));
    [_accountNewButton setFrame:NSMakeRect(84, 356, 64, 24)];
    [ec addSubview:_accountNewButton];

    _accountDeleteButton = makeButton(@"Delete", self, @selector(deleteAccount:));
    [_accountDeleteButton setFrame:NSMakeRect(152, 356, 64, 24)];
    [ec addSubview:_accountDeleteButton];

    _accountSaveButton = makeButton(@"Save", self, @selector(saveAccount:));
    [_accountSaveButton setFrame:NSMakeRect(220, 356, 64, 24)];
    [ec addSubview:_accountSaveButton];

    _accountResetPasswordButton = makeButton(@"Reset Password", self,
                                             @selector(resetAccountPassword:));
    [_accountResetPasswordButton setFrame:NSMakeRect(288, 356, 102, 24)];
    [ec addSubview:_accountResetPasswordButton];

    _accountPasswordStatusLabel = makeLabel(@"Password: none", 10.0, NO);
    [_accountPasswordStatusLabel setFrame:NSMakeRect(84, 334, 306, 14)];
    [_accountPasswordStatusLabel setTextColor:[NSColor grayColor]];
    [ec addSubview:_accountPasswordStatusLabel];

    l = makeLabel(@"Templates and permissions map directly to YAML Access keys.", 10.0, NO);
    [l setTextColor:[NSColor grayColor]];
    [l setFrame:NSMakeRect(84, 318, 306, 14)];
    [ec addSubview:l];
    [l release];

    _accountPermissionsScrollView = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(10, 8, 386, 304)];
    [_accountPermissionsScrollView setHasVerticalScroller:YES];
    [_accountPermissionsScrollView setBorderType:NSBezelBorder];
    [_accountPermissionsScrollView setAutoresizingMask:
        (NSViewWidthSizable | NSViewHeightSizable)];

    _accountPermissionsContentView = [[FlippedView alloc]
        initWithFrame:NSMakeRect(0, 0, 366, 640)];
    [_accountPermissionsContentView setAutoresizingMask:NSViewWidthSizable];
    [_accountPermissionsScrollView setDocumentView:_accountPermissionsContentView];
    [ec addSubview:_accountPermissionsScrollView];
    [self rebuildAccountPermissionUI];

    [editor release];

    _accountsBansView = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 560)];
    [_accountsBansView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [view addSubview:_accountsBansView];
    [_accountsBansView setHidden:YES];

    {
        NSBox *box = [[NSBox alloc] initWithFrame:NSMakeRect(6, 6, 203, 548)];
        [box setTitle:@"Banned IPs"];
        [box setTitleFont:[NSFont boldSystemFontOfSize:11.0]];
        NSView *c = [box contentView];

        NSScrollView *bsv = [[NSScrollView alloc] initWithFrame:NSMakeRect(6, 56, 185, 456)];
        [bsv setHasVerticalScroller:YES];
        [bsv setBorderType:NSBezelBorder];
        _bannedIPsTableView = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 185, 456)];
        col = [[NSTableColumn alloc] initWithIdentifier:@"ban_ip"];
        [[col headerCell] setStringValue:@"IP"];
        [col setWidth:170];
        [_bannedIPsTableView addTableColumn:col];
        [col release];
        [_bannedIPsTableView setDataSource:(id)self];
        [_bannedIPsTableView setDelegate:(id)self];
        [bsv setDocumentView:_bannedIPsTableView];
        [c addSubview:bsv];
        [bsv release];

        _newBanIPField = makeEditField(132);
        [_newBanIPField setFrame:NSMakeRect(6, 30, 132, 22)];
        [c addSubview:_newBanIPField];
        NSButton *add = makeButton(@"Add", self, @selector(addIPBan:));
        [add setFrame:NSMakeRect(142, 30, 50, 22)];
        [c addSubview:add];
        [add release];
        NSButton *rem = makeButton(@"Remove", self, @selector(removeIPBan:));
        [rem setFrame:NSMakeRect(126, 4, 66, 22)];
        [c addSubview:rem];
        [rem release];

        [_accountsBansView addSubview:box];
        [box release];
    }

    {
        NSBox *box = [[NSBox alloc] initWithFrame:NSMakeRect(214, 6, 203, 548)];
        [box setTitle:@"Banned Usernames"];
        [box setTitleFont:[NSFont boldSystemFontOfSize:11.0]];
        NSView *c = [box contentView];

        NSScrollView *bsv = [[NSScrollView alloc] initWithFrame:NSMakeRect(6, 56, 185, 456)];
        [bsv setHasVerticalScroller:YES];
        [bsv setBorderType:NSBezelBorder];
        _bannedUsersTableView = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 185, 456)];
        col = [[NSTableColumn alloc] initWithIdentifier:@"ban_user"];
        [[col headerCell] setStringValue:@"Username"];
        [col setWidth:170];
        [_bannedUsersTableView addTableColumn:col];
        [col release];
        [_bannedUsersTableView setDataSource:(id)self];
        [_bannedUsersTableView setDelegate:(id)self];
        [bsv setDocumentView:_bannedUsersTableView];
        [c addSubview:bsv];
        [bsv release];

        _newBanUserField = makeEditField(132);
        [_newBanUserField setFrame:NSMakeRect(6, 30, 132, 22)];
        [c addSubview:_newBanUserField];
        NSButton *add = makeButton(@"Add", self, @selector(addUserBan:));
        [add setFrame:NSMakeRect(142, 30, 50, 22)];
        [c addSubview:add];
        [add release];
        NSButton *rem = makeButton(@"Remove", self, @selector(removeUserBan:));
        [rem setFrame:NSMakeRect(126, 4, 66, 22)];
        [c addSubview:rem];
        [rem release];

        [_accountsBansView addSubview:box];
        [box release];
    }

    {
        NSBox *box = [[NSBox alloc] initWithFrame:NSMakeRect(422, 6, 203, 548)];
        [box setTitle:@"Banned Nicknames"];
        [box setTitleFont:[NSFont boldSystemFontOfSize:11.0]];
        NSView *c = [box contentView];

        NSScrollView *bsv = [[NSScrollView alloc] initWithFrame:NSMakeRect(6, 56, 185, 456)];
        [bsv setHasVerticalScroller:YES];
        [bsv setBorderType:NSBezelBorder];
        _bannedNicksTableView = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 185, 456)];
        col = [[NSTableColumn alloc] initWithIdentifier:@"ban_nick"];
        [[col headerCell] setStringValue:@"Nickname"];
        [col setWidth:170];
        [_bannedNicksTableView addTableColumn:col];
        [col release];
        [_bannedNicksTableView setDataSource:(id)self];
        [_bannedNicksTableView setDelegate:(id)self];
        [bsv setDocumentView:_bannedNicksTableView];
        [c addSubview:bsv];
        [bsv release];

        _newBanNickField = makeEditField(132);
        [_newBanNickField setFrame:NSMakeRect(6, 30, 132, 22)];
        [c addSubview:_newBanNickField];
        NSButton *add = makeButton(@"Add", self, @selector(addNickBan:));
        [add setFrame:NSMakeRect(142, 30, 50, 22)];
        [c addSubview:add];
        [add release];
        NSButton *rem = makeButton(@"Remove", self, @selector(removeNickBan:));
        [rem setFrame:NSMakeRect(126, 4, 66, 22)];
        [c addSubview:rem];
        [rem release];

        [_accountsBansView addSubview:box];
        [box release];
    }

    [self refreshAccountsList:nil];
    [self loadBanListData];
    [self populateAccountEditorForNewAccount];
    return [view autorelease];
}

/* ===== Online tab ===== */

- (NSView *)createOnlineTabView
{
    NSView *view = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 590)];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSView *tb = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 560, 630, 30)];
    [tb setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    _onlineStatusLabel = makeLabel(@"Connected: 0  Peak: 0", 11.0, NO);
    [_onlineStatusLabel setFrame:NSMakeRect(8, 7, 320, 16)];
    [_onlineStatusLabel setTextColor:[NSColor grayColor]];
    [tb addSubview:_onlineStatusLabel];

    _onlineRefreshButton = makeButton(@"Refresh", self, @selector(refreshOnlineUsers:));
    [_onlineRefreshButton setFrame:NSMakeRect(460, 3, 80, 24)];
    [_onlineRefreshButton setAutoresizingMask:NSViewMinXMargin];
    [tb addSubview:_onlineRefreshButton];

    _onlineBanButton = makeButton(@"Ban Selected IP", self, @selector(banSelectedOnlineUser:));
    [_onlineBanButton setFrame:NSMakeRect(542, 3, 86, 24)];
    [_onlineBanButton setAutoresizingMask:NSViewMinXMargin];
    [_onlineBanButton setEnabled:NO];
    [tb addSubview:_onlineBanButton];

    [view addSubview:tb];
    [tb release];

    _onlineSplitView = [[NSSplitView alloc]
        initWithFrame:NSMakeRect(4, 4, 622, 554)];
    [_onlineSplitView setVertical:NO];
    [_onlineSplitView setDividerStyle:NSSplitViewDividerStyleThin];
    [_onlineSplitView setAutoresizesSubviews:YES];
    [_onlineSplitView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSScrollView *usersSV = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(0, 190, 622, 364)];
    [usersSV setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [usersSV setHasVerticalScroller:YES];
    [usersSV setBorderType:NSBezelBorder];

    _onlineTableView = [[NSTableView alloc]
        initWithFrame:NSMakeRect(0, 0, 622, 364)];

    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"online_login"];
    [[col headerCell] setStringValue:@"Login"];
    [col setWidth:180];
    [_onlineTableView addTableColumn:col];
    [col release];

    col = [[NSTableColumn alloc] initWithIdentifier:@"online_ip"];
    [[col headerCell] setStringValue:@"IP Address"];
    [col setWidth:210];
    [_onlineTableView addTableColumn:col];
    [col release];

    col = [[NSTableColumn alloc] initWithIdentifier:@"online_id"];
    [[col headerCell] setStringValue:@"User ID"];
    [col setWidth:90];
    [_onlineTableView addTableColumn:col];
    [col release];

    col = [[NSTableColumn alloc] initWithIdentifier:@"online_seen"];
    [[col headerCell] setStringValue:@"Last Seen"];
    [col setWidth:130];
    [_onlineTableView addTableColumn:col];
    [col release];

    [_onlineTableView setDataSource:(id)self];
    [_onlineTableView setDelegate:(id)self];
    [_onlineTableView setAllowsMultipleSelection:NO];
    [usersSV setDocumentView:_onlineTableView];

    _onlineLogScrollView = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(0, 0, 622, 186)];
    [_onlineLogScrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [_onlineLogScrollView setHasVerticalScroller:YES];
    [_onlineLogScrollView setBorderType:NSBezelBorder];

    NSSize cs = [_onlineLogScrollView contentSize];
    _onlineLogTextView = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0, 0, cs.width, cs.height)];
    [_onlineLogTextView setMinSize:NSMakeSize(0, cs.height)];
    [_onlineLogTextView setMaxSize:NSMakeSize(1e7, 1e7)];
    [_onlineLogTextView setVerticallyResizable:YES];
    [_onlineLogTextView setHorizontallyResizable:NO];
    [_onlineLogTextView setAutoresizingMask:NSViewWidthSizable];
    [[_onlineLogTextView textContainer]
        setContainerSize:NSMakeSize(cs.width, 1e7)];
    [[_onlineLogTextView textContainer] setWidthTracksTextView:YES];
    [_onlineLogTextView setEditable:NO];
    [_onlineLogTextView setFont:[NSFont fontWithName:@"Monaco" size:10.0]];
    [_onlineLogScrollView setDocumentView:_onlineLogTextView];

    [_onlineSplitView addSubview:usersSV];
    [_onlineSplitView addSubview:_onlineLogScrollView];
    [usersSV release];
    [_onlineSplitView setPosition:364 ofDividerAtIndex:0];
    [view addSubview:_onlineSplitView];

    if (!_onlineRefreshTimer) {
        _onlineRefreshTimer = [[NSTimer scheduledTimerWithTimeInterval:5.0
                                                                 target:self
                                                               selector:@selector(refreshOnlineUsers:)
                                                               userInfo:nil
                                                                repeats:YES] retain];
    }
    [self refreshOnlineUsers:nil];

    return [view autorelease];
}

/* ===== Files tab ===== */

- (NSView *)createFilesTabView
{
    NSView *view = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 590)];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSView *tb = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 560, 630, 30)];
    [tb setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    NSButton *homeBtn = makeButton(@"Home", self, @selector(navigateFilesHome:));
    [homeBtn setFrame:NSMakeRect(8, 3, 62, 24)];
    [tb addSubview:homeBtn];
    [homeBtn release];

    NSButton *upBtn = makeButton(@"Up", self, @selector(navigateFilesUp:));
    [upBtn setFrame:NSMakeRect(74, 3, 52, 24)];
    [tb addSubview:upBtn];
    [upBtn release];

    NSButton *refreshBtn = makeButton(@"Refresh", self, @selector(refreshFilesList:));
    [refreshBtn setFrame:NSMakeRect(130, 3, 74, 24)];
    [tb addSubview:refreshBtn];
    [refreshBtn release];

    _filesPathLabel = makeLabel(@"", 10.0, NO);
    [_filesPathLabel setTextColor:[NSColor grayColor]];
    [_filesPathLabel setFrame:NSMakeRect(212, 7, 410, 16)];
    [_filesPathLabel setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [tb addSubview:_filesPathLabel];

    [view addSubview:tb];
    [tb release];

    NSScrollView *sv = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(4, 4, 622, 554)];
    [sv setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [sv setHasVerticalScroller:YES];
    [sv setBorderType:NSBezelBorder];

    _filesTableView = [[NSTableView alloc]
        initWithFrame:NSMakeRect(0, 0, 622, 554)];

    NSTableColumn *nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    [[nameCol headerCell] setStringValue:@"Name"];
    [nameCol setWidth:350];
    [_filesTableView addTableColumn:nameCol];
    [nameCol release];

    NSTableColumn *sizeCol = [[NSTableColumn alloc] initWithIdentifier:@"size"];
    [[sizeCol headerCell] setStringValue:@"Size"];
    [sizeCol setWidth:90];
    [_filesTableView addTableColumn:sizeCol];
    [sizeCol release];

    NSTableColumn *dateCol = [[NSTableColumn alloc] initWithIdentifier:@"modified"];
    [[dateCol headerCell] setStringValue:@"Modified"];
    [dateCol setWidth:170];
    [_filesTableView addTableColumn:dateCol];
    [dateCol release];

    [_filesTableView setDataSource:(id)self];
    [_filesTableView setDelegate:(id)self];
    [_filesTableView setTarget:self];
    [_filesTableView setDoubleAction:@selector(filesTableDoubleClick:)];

    [sv setDocumentView:_filesTableView];
    [view addSubview:sv];
    [sv release];

    [self refreshFilesList:nil];
    return [view autorelease];
}

/* ===== News tab ===== */

- (NSView *)createNewsTabView
{
    NSView *view = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 590)];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSView *tb = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 560, 630, 30)];
    [tb setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    _newsSegmentedControl = [[NSSegmentedControl alloc]
        initWithFrame:NSMakeRect(8, 4, 230, 22)];
    [_newsSegmentedControl setSegmentCount:2];
    [_newsSegmentedControl setLabel:@"Message Board" forSegment:0];
    [_newsSegmentedControl setLabel:@"Threaded News" forSegment:1];
    [_newsSegmentedControl setTarget:self];
    [_newsSegmentedControl setAction:@selector(newsSegmentChanged:)];
    [_newsSegmentedControl setSelectedSegment:0];
    [tb addSubview:_newsSegmentedControl];

    [view addSubview:tb];
    [tb release];

    _newsContainerView = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 560)];
    [_newsContainerView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [view addSubview:_newsContainerView];

    _newsMessageBoardView = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 560)];
    [_newsMessageBoardView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSScrollView *mbScroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(4, 34, 622, 522)];
    [mbScroll setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [mbScroll setHasVerticalScroller:YES];
    [mbScroll setBorderType:NSBezelBorder];

    _messageBoardTextView = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0, 0, 622, 522)];
    [_messageBoardTextView setMinSize:NSMakeSize(0, 522)];
    [_messageBoardTextView setMaxSize:NSMakeSize(1e7, 1e7)];
    [_messageBoardTextView setVerticallyResizable:YES];
    [_messageBoardTextView setHorizontallyResizable:NO];
    [_messageBoardTextView setAutoresizingMask:NSViewWidthSizable];
    [[_messageBoardTextView textContainer] setContainerSize:NSMakeSize(622, 1e7)];
    [[_messageBoardTextView textContainer] setWidthTracksTextView:YES];
    [_messageBoardTextView setFont:[NSFont fontWithName:@"Monaco" size:10.0]];
    [_messageBoardTextView setDelegate:(id)self];
    [mbScroll setDocumentView:_messageBoardTextView];
    [_newsMessageBoardView addSubview:mbScroll];
    [mbScroll release];

    _messageBoardDirtyLabel = makeLabel(@"Unsaved changes", 11.0, NO);
    [_messageBoardDirtyLabel setFrame:NSMakeRect(8, 8, 220, 16)];
    [_messageBoardDirtyLabel setTextColor:[NSColor orangeColor]];
    [_newsMessageBoardView addSubview:_messageBoardDirtyLabel];

    _saveMessageBoardButton = makeButton(@"Save Message Board", self,
                                         @selector(saveMessageBoard:));
    [_saveMessageBoardButton setFrame:NSMakeRect(488, 5, 136, 24)];
    [_saveMessageBoardButton setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
    [_newsMessageBoardView addSubview:_saveMessageBoardButton];

    _newsThreadedView = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, 630, 560)];
    [_newsThreadedView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    _newsNewCategoryField = makeEditField(170);
    [_newsNewCategoryField setFrame:NSMakeRect(8, 530, 170, 22)];
    [_newsNewCategoryField setAutoresizingMask:NSViewMaxXMargin];
    [_newsThreadedView addSubview:_newsNewCategoryField];

    NSButton *addCatBtn = makeButton(@"Add Category", self,
                                     @selector(addNewsCategory:));
    [addCatBtn setFrame:NSMakeRect(182, 528, 90, 24)];
    [addCatBtn setAutoresizingMask:NSViewMaxXMargin];
    [_newsThreadedView addSubview:addCatBtn];
    [addCatBtn release];

    _newsDeleteCategoryButton = makeButton(@"Delete Category", self,
                                           @selector(deleteNewsCategory:));
    [_newsDeleteCategoryButton setFrame:NSMakeRect(276, 528, 98, 24)];
    [_newsDeleteCategoryButton setAutoresizingMask:NSViewMaxXMargin];
    [_newsDeleteCategoryButton setEnabled:NO];
    [_newsThreadedView addSubview:_newsDeleteCategoryButton];

    _newsAddArticleButton = makeButton(@"Add Article", self,
                                       @selector(addNewsArticle:));
    [_newsAddArticleButton setFrame:NSMakeRect(378, 528, 82, 24)];
    [_newsAddArticleButton setAutoresizingMask:NSViewMinXMargin];
    [_newsAddArticleButton setEnabled:NO];
    [_newsThreadedView addSubview:_newsAddArticleButton];

    _newsEditArticleButton = makeButton(@"Edit", self,
                                        @selector(editNewsArticle:));
    [_newsEditArticleButton setFrame:NSMakeRect(464, 528, 70, 24)];
    [_newsEditArticleButton setAutoresizingMask:NSViewMinXMargin];
    [_newsEditArticleButton setEnabled:NO];
    [_newsThreadedView addSubview:_newsEditArticleButton];

    _newsDeleteArticleButton = makeButton(@"Delete", self,
                                          @selector(deleteNewsArticle:));
    [_newsDeleteArticleButton setFrame:NSMakeRect(538, 528, 88, 24)];
    [_newsDeleteArticleButton setAutoresizingMask:NSViewMinXMargin];
    [_newsDeleteArticleButton setEnabled:NO];
    [_newsThreadedView addSubview:_newsDeleteArticleButton];

    NSScrollView *catsScroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(4, 4, 220, 522)];
    [catsScroll setAutoresizingMask:(NSViewHeightSizable)];
    [catsScroll setHasVerticalScroller:YES];
    [catsScroll setBorderType:NSBezelBorder];

    _newsCategoriesTableView = [[NSTableView alloc]
        initWithFrame:NSMakeRect(0, 0, 220, 522)];
    NSTableColumn *catCol = [[NSTableColumn alloc] initWithIdentifier:@"category"];
    [[catCol headerCell] setStringValue:@"Categories"];
    [catCol setWidth:205];
    [_newsCategoriesTableView addTableColumn:catCol];
    [catCol release];
    [_newsCategoriesTableView setDataSource:(id)self];
    [_newsCategoriesTableView setDelegate:(id)self];
    [_newsCategoriesTableView setAllowsMultipleSelection:NO];
    [catsScroll setDocumentView:_newsCategoriesTableView];
    [_newsThreadedView addSubview:catsScroll];
    [catsScroll release];

    NSScrollView *artsScroll = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(228, 4, 398, 522)];
    [artsScroll setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [artsScroll setHasVerticalScroller:YES];
    [artsScroll setBorderType:NSBezelBorder];

    _newsArticlesTableView = [[NSTableView alloc]
        initWithFrame:NSMakeRect(0, 0, 398, 522)];
    NSTableColumn *titleCol = [[NSTableColumn alloc] initWithIdentifier:@"news_title"];
    [[titleCol headerCell] setStringValue:@"Title"];
    [titleCol setWidth:165];
    [_newsArticlesTableView addTableColumn:titleCol];
    [titleCol release];
    NSTableColumn *posterCol = [[NSTableColumn alloc] initWithIdentifier:@"news_poster"];
    [[posterCol headerCell] setStringValue:@"Poster"];
    [posterCol setWidth:90];
    [_newsArticlesTableView addTableColumn:posterCol];
    [posterCol release];
    NSTableColumn *dateCol = [[NSTableColumn alloc] initWithIdentifier:@"news_date"];
    [[dateCol headerCell] setStringValue:@"Date"];
    [dateCol setWidth:80];
    [_newsArticlesTableView addTableColumn:dateCol];
    [dateCol release];
    NSTableColumn *bodyCol = [[NSTableColumn alloc] initWithIdentifier:@"news_body"];
    [[bodyCol headerCell] setStringValue:@"Body"];
    [bodyCol setWidth:250];
    [_newsArticlesTableView addTableColumn:bodyCol];
    [bodyCol release];
    [_newsArticlesTableView setDataSource:(id)self];
    [_newsArticlesTableView setDelegate:(id)self];
    [artsScroll setDocumentView:_newsArticlesTableView];
    [_newsThreadedView addSubview:artsScroll];
    [artsScroll release];

    [_newsContainerView addSubview:_newsMessageBoardView];
    [_newsContainerView addSubview:_newsThreadedView];
    [_newsThreadedView setHidden:YES];

    [self loadMessageBoardText];
    [self refreshThreadedNews:nil];
    [self setMessageBoardDirty:NO];

    return [view autorelease];
}

/* ===== Actions ===== */

- (void)startServer:(id)sender
{
    (void)sender;
    [self saveSettings:nil];
    [self ensureConfigScaffolding];
    [self writeConfigToDisk];
    [_processManager startWithConfigDir:_configDir port:_serverPort];
}

- (void)stopServer:(id)sender
{
    (void)sender;
    [_processManager stop];
}

- (void)restartServer:(id)sender
{
    (void)sender;
    [self saveSettings:nil];
    [self ensureConfigScaffolding];
    [self writeConfigToDisk];
    [_processManager restartWithConfigDir:_configDir port:_serverPort];
}

- (void)chooseConfigDir:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select config directory"];
    if ([panel runModal] == NSOKButton) {
        NSString *path = [[panel filenames] objectAtIndex:0];
        [_configDir release];
        _configDir = [path retain];
        [_configDirField setStringValue:_configDir];
        [self loadConfigFromDisk];
        [self saveSettings:nil];
        [self refreshAccountsList:nil];
        [self loadBanListData];
        [self refreshFilesList:nil];
        [self loadMessageBoardText];
        [self refreshThreadedNews:nil];
    }
}

- (void)chooseFileRoot:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select files directory"];
    if ([panel runModal] == NSOKButton) {
        [_fileRootField setStringValue:
            [[panel filenames] objectAtIndex:0]];
        [self refreshFilesList:nil];
    }
}

- (void)chooseBannerFile:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select banner image (JPG or GIF)"];
    if ([panel runModal] == NSOKButton) {
        [_bannerFileField setStringValue:
            [[panel filenames] objectAtIndex:0]];
    }
}

- (void)editAgreementFile:(id)sender
{
    (void)sender;
    [self openTextConfigFileNamed:@"Agreement.txt" title:@"Agreement"];
}

- (void)editMessageBoardFile:(id)sender
{
    (void)sender;
    [self openTextConfigFileNamed:@"MessageBoard.txt" title:@"Message Board"];
}

- (void)showSetupWizard:(id)sender
{
    (void)sender;

    if (!_wizardWindow) {
        _wizardWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 660, 430)
                      styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                                 NSMiniaturizableWindowMask)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [_wizardWindow setTitle:@"Server Setup Wizard"];
        [_wizardWindow setReleasedWhenClosed:NO];
        [_wizardWindow setDelegate:(id)self];

        NSView *content = [_wizardWindow contentView];

        _wizardStepLabel = makeLabel(@"", 14.0, YES);
        [_wizardStepLabel setFrame:NSMakeRect(16, 392, 620, 22)];
        [content addSubview:_wizardStepLabel];

        _wizardProgress = [[NSProgressIndicator alloc]
            initWithFrame:NSMakeRect(16, 368, 628, 16)];
        [_wizardProgress setIndeterminate:NO];
        [_wizardProgress setMinValue:0.0];
        [_wizardProgress setMaxValue:4.0];
        [_wizardProgress setDoubleValue:1.0];
        [content addSubview:_wizardProgress];

        _wizardStepContainer = [[NSView alloc]
            initWithFrame:NSMakeRect(16, 66, 628, 292)];
        [content addSubview:_wizardStepContainer];

        _wizardCancelButton = makeButton(@"Cancel", self, @selector(closeWizard:));
        [_wizardCancelButton setFrame:NSMakeRect(16, 20, 90, 28)];
        [content addSubview:_wizardCancelButton];

        _wizardBackButton = makeButton(@"Back", self, @selector(wizardBack:));
        [_wizardBackButton setFrame:NSMakeRect(340, 20, 76, 28)];
        [content addSubview:_wizardBackButton];

        _wizardNextButton = makeButton(@"Next", self, @selector(wizardNext:));
        [_wizardNextButton setFrame:NSMakeRect(422, 20, 76, 28)];
        [content addSubview:_wizardNextButton];

        _wizardFinishButton = makeButton(@"Finish", self, @selector(wizardFinish:));
        [_wizardFinishButton setFrame:NSMakeRect(504, 20, 70, 28)];
        [content addSubview:_wizardFinishButton];

        _wizardFinishStartButton = makeButton(@"Finish & Start", self,
                                              @selector(wizardFinishAndStart:));
        [_wizardFinishStartButton setFrame:NSMakeRect(580, 20, 74, 28)];
        [content addSubview:_wizardFinishStartButton];

        _wizardNameField = makeEditField(430);
        _wizardDescriptionField = makeEditField(430);
        _wizardPortField = makeEditField(86);
        _wizardFileRootField = makeEditField(430);
        _wizardBannerField = makeEditField(430);
        _wizardMaxDownloadsField = makeEditField(80);
        _wizardMaxDLPerClientField = makeEditField(80);
        _wizardMaxConnPerIPField = makeEditField(80);

        _wizardBonjourCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_wizardBonjourCheckbox setButtonType:NSSwitchButton];
        [_wizardBonjourCheckbox setTitle:@"Enable Bonjour"];

        _wizardTrackerCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_wizardTrackerCheckbox setButtonType:NSSwitchButton];
        [_wizardTrackerCheckbox setTitle:@"Enable Tracker Registration"];

        _wizardPreserveForkCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_wizardPreserveForkCheckbox setButtonType:NSSwitchButton];
        [_wizardPreserveForkCheckbox setTitle:@"Preserve Resource Forks"];

        _wizardSummaryTextView = [[NSTextView alloc]
            initWithFrame:NSMakeRect(0, 0, 596, 248)];
        [_wizardSummaryTextView setEditable:NO];
        [_wizardSummaryTextView setSelectable:YES];
        [_wizardSummaryTextView setFont:[NSFont userFixedPitchFontOfSize:11.0]];
    }

    [_wizardNameField setStringValue:
        _serverNameField ? [_serverNameField stringValue] : (_serverName ? _serverName : @"")];
    [_wizardDescriptionField setStringValue:
        _descriptionField ? [_descriptionField stringValue] : (_serverDescription ? _serverDescription : @"")];
    [_wizardPortField setIntValue:_portField ? [_portField intValue] : _serverPort];
    [_wizardFileRootField setStringValue:
        _fileRootField ? [_fileRootField stringValue] : @"Files"];
    [_wizardBannerField setStringValue:
        _bannerFileField ? [_bannerFileField stringValue] : @""];
    [_wizardBonjourCheckbox setState:
        (_bonjourCheckbox && [_bonjourCheckbox state] == NSOffState) ? NSOffState : NSOnState];
    [_wizardTrackerCheckbox setState:
        (_trackerCheckbox && [_trackerCheckbox state] == NSOnState) ? NSOnState : NSOffState];
    [_wizardPreserveForkCheckbox setState:
        (_preserveForkCheckbox && [_preserveForkCheckbox state] == NSOnState) ? NSOnState : NSOffState];
    [_wizardMaxDownloadsField setIntValue:
        _maxDownloadsField ? [_maxDownloadsField intValue] : 0];
    [_wizardMaxDLPerClientField setIntValue:
        _maxDLPerClientField ? [_maxDLPerClientField intValue] : 0];
    [_wizardMaxConnPerIPField setIntValue:
        _maxConnPerIPField ? [_maxConnPerIPField intValue] : 0];

    _wizardStepIndex = 0;
    [self rebuildWizardStepUI];
    [_wizardWindow center];
    [_wizardWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    _wizardPresented = YES;
}

- (void)wizardBack:(id)sender
{
    (void)sender;
    if (_wizardStepIndex <= 0) return;
    _wizardStepIndex--;
    [self rebuildWizardStepUI];
}

- (void)wizardNext:(id)sender
{
    (void)sender;
    if (![self validateWizardStep:YES]) return;
    if (_wizardStepIndex >= 3) return;
    _wizardStepIndex++;
    [self rebuildWizardStepUI];
}

- (void)wizardFinish:(id)sender
{
    (void)sender;
    if (![self validateWizardStep:YES]) return;
    [self applyWizardValuesToSettings];
    [self closeWizard:nil];
}

- (void)wizardFinishAndStart:(id)sender
{
    (void)sender;
    if (![self validateWizardStep:YES]) return;
    [self applyWizardValuesToSettings];
    [self closeWizard:nil];
    if ([_processManager isRunning]) {
        [self restartServer:nil];
    } else {
        [self startServer:nil];
    }
}

- (void)closeWizard:(id)sender
{
    (void)sender;
    if (_wizardWindow) [_wizardWindow orderOut:nil];
    _wizardPresented = NO;
}

- (void)rebuildWizardStepUI
{
    if (!_wizardStepContainer) return;

    NSArray *subs = [NSArray arrayWithArray:[_wizardStepContainer subviews]];
    unsigned i;
    for (i = 0; i < [subs count]; i++) {
        [[subs objectAtIndex:i] removeFromSuperview];
    }

    [_wizardProgress setDoubleValue:(double)(_wizardStepIndex + 1)];
    [_wizardBackButton setEnabled:(_wizardStepIndex > 0)];
    [_wizardNextButton setHidden:(_wizardStepIndex >= 3)];
    [_wizardFinishButton setHidden:(_wizardStepIndex < 3)];
    [_wizardFinishStartButton setHidden:(_wizardStepIndex < 3)];

    NSString *title = @"";
    switch (_wizardStepIndex) {
        case 0: title = @"Identity & Network"; break;
        case 1: title = @"Paths & Registration"; break;
        case 2: title = @"Limits & File Behavior"; break;
        default: title = @"Summary"; break;
    }
    [_wizardStepLabel setStringValue:
        [NSString stringWithFormat:@"Step %d of 4: %@", _wizardStepIndex + 1, title]];

    if (_wizardStepIndex == 0) {
        NSTextField *intro = makeLabel(
            @"Configure the core server identity and listener port.",
            12.0, NO);
        [intro setFrame:NSMakeRect(10, 262, 560, 20)];
        [_wizardStepContainer addSubview:intro];
        [intro release];

        NSTextField *nameLabel = makeLabel(@"Server Name:", 11.0, NO);
        [nameLabel setAlignment:NSRightTextAlignment];
        [nameLabel setFrame:NSMakeRect(10, 224, 110, 20)];
        [_wizardStepContainer addSubview:nameLabel];
        [nameLabel release];
        [_wizardNameField setFrame:NSMakeRect(126, 222, 436, 22)];
        [_wizardStepContainer addSubview:_wizardNameField];

        NSTextField *descLabel = makeLabel(@"Description:", 11.0, NO);
        [descLabel setAlignment:NSRightTextAlignment];
        [descLabel setFrame:NSMakeRect(10, 190, 110, 20)];
        [_wizardStepContainer addSubview:descLabel];
        [descLabel release];
        [_wizardDescriptionField setFrame:NSMakeRect(126, 188, 436, 22)];
        [_wizardStepContainer addSubview:_wizardDescriptionField];

        NSTextField *portLabel = makeLabel(@"Hotline Port:", 11.0, NO);
        [portLabel setAlignment:NSRightTextAlignment];
        [portLabel setFrame:NSMakeRect(10, 156, 110, 20)];
        [_wizardStepContainer addSubview:portLabel];
        [portLabel release];
        [_wizardPortField setFrame:NSMakeRect(126, 154, 90, 22)];
        [_wizardStepContainer addSubview:_wizardPortField];

        [_wizardBonjourCheckbox setFrame:NSMakeRect(126, 126, 220, 18)];
        [_wizardStepContainer addSubview:_wizardBonjourCheckbox];
    } else if (_wizardStepIndex == 1) {
        NSTextField *intro = makeLabel(
            @"Choose content paths and network registration behavior.",
            12.0, NO);
        [intro setFrame:NSMakeRect(10, 262, 560, 20)];
        [_wizardStepContainer addSubview:intro];
        [intro release];

        NSTextField *rootLabel = makeLabel(@"File Root:", 11.0, NO);
        [rootLabel setAlignment:NSRightTextAlignment];
        [rootLabel setFrame:NSMakeRect(10, 224, 110, 20)];
        [_wizardStepContainer addSubview:rootLabel];
        [rootLabel release];
        [_wizardFileRootField setFrame:NSMakeRect(126, 222, 436, 22)];
        [_wizardStepContainer addSubview:_wizardFileRootField];

        NSTextField *bannerLabel = makeLabel(@"Banner File:", 11.0, NO);
        [bannerLabel setAlignment:NSRightTextAlignment];
        [bannerLabel setFrame:NSMakeRect(10, 190, 110, 20)];
        [_wizardStepContainer addSubview:bannerLabel];
        [bannerLabel release];
        [_wizardBannerField setFrame:NSMakeRect(126, 188, 436, 22)];
        [_wizardStepContainer addSubview:_wizardBannerField];

        [_wizardTrackerCheckbox setFrame:NSMakeRect(126, 156, 260, 18)];
        [_wizardStepContainer addSubview:_wizardTrackerCheckbox];

        NSTextField *hint = makeLabel(
            @"Tip: add tracker endpoints in the left panel after finishing the wizard.",
            10.0, NO);
        [hint setTextColor:[NSColor grayColor]];
        [hint setFrame:NSMakeRect(126, 132, 470, 16)];
        [_wizardStepContainer addSubview:hint];
        [hint release];
    } else if (_wizardStepIndex == 2) {
        NSTextField *intro = makeLabel(
            @"Set transfer limits and file-storage compatibility options.",
            12.0, NO);
        [intro setFrame:NSMakeRect(10, 262, 560, 20)];
        [_wizardStepContainer addSubview:intro];
        [intro release];

        [_wizardPreserveForkCheckbox setFrame:NSMakeRect(126, 228, 260, 18)];
        [_wizardStepContainer addSubview:_wizardPreserveForkCheckbox];

        NSTextField *maxDL = makeLabel(@"Max Downloads:", 11.0, NO);
        [maxDL setAlignment:NSRightTextAlignment];
        [maxDL setFrame:NSMakeRect(10, 190, 110, 20)];
        [_wizardStepContainer addSubview:maxDL];
        [maxDL release];
        [_wizardMaxDownloadsField setFrame:NSMakeRect(126, 188, 86, 22)];
        [_wizardStepContainer addSubview:_wizardMaxDownloadsField];

        NSTextField *maxPer = makeLabel(@"Max DL/Client:", 11.0, NO);
        [maxPer setAlignment:NSRightTextAlignment];
        [maxPer setFrame:NSMakeRect(10, 156, 110, 20)];
        [_wizardStepContainer addSubview:maxPer];
        [maxPer release];
        [_wizardMaxDLPerClientField setFrame:NSMakeRect(126, 154, 86, 22)];
        [_wizardStepContainer addSubview:_wizardMaxDLPerClientField];

        NSTextField *maxIP = makeLabel(@"Max Conn/IP:", 11.0, NO);
        [maxIP setAlignment:NSRightTextAlignment];
        [maxIP setFrame:NSMakeRect(10, 122, 110, 20)];
        [_wizardStepContainer addSubview:maxIP];
        [maxIP release];
        [_wizardMaxConnPerIPField setFrame:NSMakeRect(126, 120, 86, 22)];
        [_wizardStepContainer addSubview:_wizardMaxConnPerIPField];

        NSTextField *hint = makeLabel(@"Use 0 for unlimited values.", 10.0, NO);
        [hint setTextColor:[NSColor grayColor]];
        [hint setFrame:NSMakeRect(126, 98, 240, 16)];
        [_wizardStepContainer addSubview:hint];
        [hint release];
    } else {
        NSString *summary = [NSString stringWithFormat:
            @"Name: %@\n"
            @"Description: %@\n"
            @"Port: %d\n"
            @"File Root: %@\n"
            @"Banner File: %@\n"
            @"Bonjour: %@\n"
            @"Tracker Registration: %@\n"
            @"Preserve Resource Forks: %@\n"
            @"Max Downloads: %d\n"
            @"Max DL/Client: %d\n"
            @"Max Conn/IP: %d\n\n"
            @"Click Finish to save and close the wizard,\n"
            @"or Finish & Start to save and launch the server.",
            [_wizardNameField stringValue],
            [_wizardDescriptionField stringValue],
            [_wizardPortField intValue],
            [_wizardFileRootField stringValue],
            [_wizardBannerField stringValue],
            ([_wizardBonjourCheckbox state] == NSOnState ? @"Enabled" : @"Disabled"),
            ([_wizardTrackerCheckbox state] == NSOnState ? @"Enabled" : @"Disabled"),
            ([_wizardPreserveForkCheckbox state] == NSOnState ? @"Enabled" : @"Disabled"),
            [_wizardMaxDownloadsField intValue],
            [_wizardMaxDLPerClientField intValue],
            [_wizardMaxConnPerIPField intValue]];
        [_wizardSummaryTextView setString:summary];

        NSScrollView *sv = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(10, 16, 608, 260)];
        [sv setHasVerticalScroller:YES];
        [sv setBorderType:NSBezelBorder];
        [sv setDocumentView:_wizardSummaryTextView];
        [_wizardStepContainer addSubview:sv];
        [sv release];
    }
}

- (BOOL)validateWizardStep:(BOOL)showAlert
{
    if (_wizardStepIndex == 0) {
        NSString *name = trimmedString([_wizardNameField stringValue]);
        if ([name length] == 0) {
            if (showAlert) {
                NSRunAlertPanel(@"Missing Server Name",
                                @"Enter a server name before continuing.",
                                @"OK", nil, nil);
            }
            return NO;
        }

        int port = [_wizardPortField intValue];
        if (port < 1 || port > 65535) {
            if (showAlert) {
                NSRunAlertPanel(@"Invalid Port",
                                @"Hotline port must be between 1 and 65535.",
                                @"OK", nil, nil);
            }
            return NO;
        }
    } else if (_wizardStepIndex == 1) {
        NSString *root = trimmedString([_wizardFileRootField stringValue]);
        if ([root length] == 0) {
            if (showAlert) {
                NSRunAlertPanel(@"Missing File Root",
                                @"Enter a file root path before continuing.",
                                @"OK", nil, nil);
            }
            return NO;
        }
    } else if (_wizardStepIndex == 2) {
        if ([_wizardMaxDownloadsField intValue] < 0 ||
            [_wizardMaxDLPerClientField intValue] < 0 ||
            [_wizardMaxConnPerIPField intValue] < 0) {
            if (showAlert) {
                NSRunAlertPanel(@"Invalid Limits",
                                @"Limit values cannot be negative.",
                                @"OK", nil, nil);
            }
            return NO;
        }
    }
    return YES;
}

- (void)applyWizardValuesToSettings
{
    NSString *name = trimmedString([_wizardNameField stringValue]);
    NSString *desc = [_wizardDescriptionField stringValue];
    NSString *fileRoot = trimmedString([_wizardFileRootField stringValue]);
    NSString *banner = trimmedString([_wizardBannerField stringValue]);
    int port = [_wizardPortField intValue];
    if (port < 1 || port > 65535) port = 5500;

    if (_serverNameField) [_serverNameField setStringValue:name];
    if (_descriptionField) [_descriptionField setStringValue:desc];
    if (_portField) [_portField setIntValue:port];
    if (_fileRootField) [_fileRootField setStringValue:fileRoot];
    if (_bannerFileField) [_bannerFileField setStringValue:banner];
    if (_bonjourCheckbox) [_bonjourCheckbox setState:[_wizardBonjourCheckbox state]];
    if (_trackerCheckbox) [_trackerCheckbox setState:[_wizardTrackerCheckbox state]];
    if (_preserveForkCheckbox) {
        [_preserveForkCheckbox setState:[_wizardPreserveForkCheckbox state]];
    }
    if (_maxDownloadsField) {
        [_maxDownloadsField setIntValue:[_wizardMaxDownloadsField intValue]];
    }
    if (_maxDLPerClientField) {
        [_maxDLPerClientField setIntValue:[_wizardMaxDLPerClientField intValue]];
    }
    if (_maxConnPerIPField) {
        [_maxConnPerIPField setIntValue:[_wizardMaxConnPerIPField intValue]];
    }

    [self saveSettings:nil];
    [self ensureConfigScaffolding];
    [self writeConfigToDisk];
    [self updateServerUI];

    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:YES forKey:@"setupWizardCompleted"];
    [d synchronize];
}

- (void)showAboutPanel:(id)sender
{
    (void)sender;

    if (!_aboutWindow) {
        _aboutWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 560, 320)
                      styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                                 NSMiniaturizableWindowMask)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [_aboutWindow setTitle:@"About Lemoniscate"];
        [_aboutWindow setReleasedWhenClosed:NO];
        [_aboutWindow setDelegate:(id)self];

        NSView *content = [_aboutWindow contentView];

        NSTextField *title = makeLabel(@"Lemoniscate Server GUI", 18.0, YES);
        [title setFrame:NSMakeRect(20, 280, 360, 28)];
        [content addSubview:title];
        [title release];

        _aboutVersionLabel = makeLabel(@"", 12.0, NO);
        [_aboutVersionLabel setFrame:NSMakeRect(20, 254, 520, 18)];
        [content addSubview:_aboutVersionLabel];

        _aboutUpdateLabel = makeLabel(@"No update check run yet.", 11.0, NO);
        [_aboutUpdateLabel setTextColor:[NSColor grayColor]];
        [_aboutUpdateLabel setFrame:NSMakeRect(20, 228, 520, 18)];
        [content addSubview:_aboutUpdateLabel];

        NSButton *checkBtn = makeButton(@"Check for Updates...", self,
                                        @selector(checkForUpdates:));
        [checkBtn setFrame:NSMakeRect(20, 190, 158, 28)];
        [content addSubview:checkBtn];
        [checkBtn release];

        NSButton *downloadBtn = makeButton(@"Download Latest Build", self,
                                           @selector(openDownloadPage:));
        [downloadBtn setFrame:NSMakeRect(188, 190, 172, 28)];
        [content addSubview:downloadBtn];
        [downloadBtn release];

        NSButton *homeBtn = makeButton(@"Project Homepage", self,
                                       @selector(openProjectHomepage:));
        [homeBtn setFrame:NSMakeRect(20, 146, 140, 28)];
        [content addSubview:homeBtn];
        [homeBtn release];

        NSButton *guiBtn = makeButton(@"GUI Repository", self,
                                      @selector(openGUIRepository:));
        [guiBtn setFrame:NSMakeRect(170, 146, 120, 28)];
        [content addSubview:guiBtn];
        [guiBtn release];

        NSButton *srvBtn = makeButton(@"Server Repository", self,
                                      @selector(openServerRepository:));
        [srvBtn setFrame:NSMakeRect(300, 146, 140, 28)];
        [content addSubview:srvBtn];
        [srvBtn release];

        NSButton *closeBtn = makeButton(@"Close", _aboutWindow, @selector(performClose:));
        [closeBtn setFrame:NSMakeRect(470, 16, 70, 28)];
        [content addSubview:closeBtn];
        [closeBtn release];
    }

    NSString *shortVer = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    NSString *buildVer = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"CFBundleVersion"];
    if (!shortVer || [shortVer length] == 0) shortVer = @"development";

    if (buildVer && [buildVer length] > 0 &&
        ![buildVer isEqualToString:shortVer]) {
        [_aboutVersionLabel setStringValue:
            [NSString stringWithFormat:@"Version %@ (build %@)", shortVer, buildVer]];
    } else {
        [_aboutVersionLabel setStringValue:
            [NSString stringWithFormat:@"Version %@", shortVer]];
    }

    [_aboutWindow center];
    [_aboutWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)checkForUpdates:(id)sender
{
    (void)sender;
    [self showAboutPanel:nil];
    [_aboutUpdateLabel setTextColor:[NSColor grayColor]];
    [_aboutUpdateLabel setStringValue:
        [NSString stringWithFormat:@"Manual check at %@. Opening releases page...",
         [NSDate date]]];
    [self openDownloadPage:nil];
}

- (void)openProjectURL:(NSString *)urlString
{
    if (!urlString || [urlString length] == 0) return;

    NSURL *url = [NSURL URLWithString:urlString];
    if (!url) {
        NSRunAlertPanel(@"Invalid URL",
                        @"Could not open requested link.",
                        @"OK", nil, nil);
        return;
    }

    if (![[NSWorkspace sharedWorkspace] openURL:url]) {
        NSRunAlertPanel(@"Open Failed",
                        @"Unable to open link in the default browser.",
                        @"OK", nil, nil);
    }
}

- (void)openServerRepository:(id)sender
{
    (void)sender;
    [self openProjectURL:@"https://github.com/jhalter/mobius"];
}

- (void)openGUIRepository:(id)sender
{
    (void)sender;
    [self openProjectURL:@"https://github.com/fuzzywalrus/mobius-macOS-GUI"];
}

- (void)openProjectHomepage:(id)sender
{
    (void)sender;
    [self openProjectURL:@"https://github.com/fuzzywalrus/its-morbin-time"];
}

- (void)openDownloadPage:(id)sender
{
    (void)sender;
    [self openProjectURL:@"https://github.com/fuzzywalrus/its-morbin-time/releases"];
}

- (void)addTracker:(id)sender
{
    (void)sender;
    NSString *tracker = trimmedString([_newTrackerField stringValue]);
    if ([tracker length] == 0) return;
    if (![_trackerItems containsObject:tracker]) {
        [_trackerItems addObject:tracker];
        if (_trackerTableView) [_trackerTableView reloadData];
        [self writeConfigToDisk];
    }
    [_newTrackerField setStringValue:@""];
}

- (void)removeTracker:(id)sender
{
    (void)sender;
    int row = _trackerTableView ? [_trackerTableView selectedRow] : -1;
    if (row < 0 || row >= (int)[_trackerItems count]) return;
    [_trackerItems removeObjectAtIndex:(unsigned)row];
    if (_trackerTableView) [_trackerTableView reloadData];
    [self writeConfigToDisk];
}

- (void)addIgnoreFilePattern:(id)sender
{
    (void)sender;
    NSString *pattern = trimmedString([_newIgnoreFileField stringValue]);
    if ([pattern length] == 0) return;
    if (![_ignoreFileItems containsObject:pattern]) {
        [_ignoreFileItems addObject:pattern];
        if (_ignoreFilesTableView) [_ignoreFilesTableView reloadData];
        [self writeConfigToDisk];
    }
    [_newIgnoreFileField setStringValue:@""];
}

- (void)removeIgnoreFilePattern:(id)sender
{
    (void)sender;
    int row = _ignoreFilesTableView ? [_ignoreFilesTableView selectedRow] : -1;
    if (row < 0 || row >= (int)[_ignoreFileItems count]) return;
    [_ignoreFileItems removeObjectAtIndex:(unsigned)row];
    if (_ignoreFilesTableView) [_ignoreFilesTableView reloadData];
    [self writeConfigToDisk];
}

- (void)reloadServerConfig:(id)sender
{
    (void)sender;
    [self saveSettings:nil];
    [self ensureConfigScaffolding];
    [self writeConfigToDisk];

    if ([_processManager isRunning]) {
        [_processManager reloadConfiguration];
    }
}

- (void)refreshAccountsList:(id)sender
{
    (void)sender;
    if (_accountsSegmentedControl && [_accountsSegmentedControl selectedSegment] == 1) {
        [self loadBanListData];
    } else {
        [self loadAccountsListData];
    }
}

- (void)accountsSegmentChanged:(id)sender
{
    (void)sender;
    BOOL showUsers = ([_accountsSegmentedControl selectedSegment] == 0);
    [_accountsUsersView setHidden:!showUsers];
    [_accountsBansView setHidden:showUsers];
    if (showUsers) {
        [self refreshAccountsList:nil];
    } else {
        [self loadBanListData];
    }
}

- (void)newAccount:(id)sender
{
    (void)sender;
    if (!_newAccountWindow) {
        _newAccountWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 360, 176)
                      styleMask:(NSTitledWindowMask | NSClosableWindowMask)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [_newAccountWindow setReleasedWhenClosed:NO];
        [_newAccountWindow setTitle:@"New Account"];
        [_newAccountWindow setDelegate:(id)self];

        NSView *content = [_newAccountWindow contentView];

        NSTextField *ll = makeLabel(@"Login:", 11.0, NO);
        [ll setAlignment:NSRightTextAlignment];
        [ll setFrame:NSMakeRect(14, 118, 70, 18)];
        [content addSubview:ll];
        [ll release];

        _newAccountLoginField = makeEditField(246);
        [_newAccountLoginField setFrame:NSMakeRect(90, 114, 246, 22)];
        [content addSubview:_newAccountLoginField];

        NSTextField *nl = makeLabel(@"Display Name:", 11.0, NO);
        [nl setAlignment:NSRightTextAlignment];
        [nl setFrame:NSMakeRect(14, 86, 70, 18)];
        [content addSubview:nl];
        [nl release];

        _newAccountNameField = makeEditField(246);
        [_newAccountNameField setFrame:NSMakeRect(90, 82, 246, 22)];
        [content addSubview:_newAccountNameField];

        NSTextField *hint = makeLabel(
            @"Login: lowercase letters, numbers, '-' or '_'.",
            10.0, NO);
        [hint setTextColor:[NSColor grayColor]];
        [hint setFrame:NSMakeRect(16, 54, 320, 14)];
        [content addSubview:hint];
        [hint release];

        NSButton *cancelBtn = makeButton(@"Cancel", self, @selector(cancelNewAccount:));
        [cancelBtn setFrame:NSMakeRect(188, 16, 70, 24)];
        [content addSubview:cancelBtn];
        [cancelBtn release];

        NSButton *createBtn = makeButton(@"Create", self, @selector(createNewAccount:));
        [createBtn setFrame:NSMakeRect(266, 16, 70, 24)];
        [content addSubview:createBtn];
        [createBtn release];
    }

    [_newAccountLoginField setStringValue:@""];
    [_newAccountNameField setStringValue:@""];
    [_newAccountWindow center];
    [_newAccountWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)cancelNewAccount:(id)sender
{
    (void)sender;
    if (_newAccountWindow) [_newAccountWindow orderOut:nil];
}

- (void)createNewAccount:(id)sender
{
    (void)sender;
    NSString *login = [trimmedString([_newAccountLoginField stringValue]) lowercaseString];
    if ([login length] == 0) {
        NSRunAlertPanel(@"Invalid Login", @"Login cannot be empty.", @"OK", nil, nil);
        return;
    }

    NSCharacterSet *allowed = [NSCharacterSet characterSetWithCharactersInString:
        @"abcdefghijklmnopqrstuvwxyz0123456789-_"];
    unsigned i;
    for (i = 0; i < [login length]; i++) {
        unichar ch = [login characterAtIndex:i];
        if (![allowed characterIsMember:ch]) {
            NSRunAlertPanel(@"Invalid Login",
                            @"Use lowercase letters, numbers, '-' or '_'.",
                            @"OK", nil, nil);
            return;
        }
    }

    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    NSString *path = [usersDir stringByAppendingPathComponent:
        [NSString stringWithFormat:@"%@.yaml", login]];
    if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
        NSRunAlertPanel(@"Account Exists",
                        @"An account with that login already exists.",
                        @"OK", nil, nil);
        return;
    }

    [_accountsTableView deselectAll:nil];
    [self populateAccountEditorForNewAccount];
    [_accountLoginField setStringValue:login];

    NSString *name = trimmedString([_newAccountNameField stringValue]);
    if ([name length] == 0) name = login;
    [_accountNameField setStringValue:name];
    [_accountFileRootField setStringValue:@""];

    [self saveAccount:nil];
    [self cancelNewAccount:nil];
}

- (void)accountTemplateChanged:(id)sender
{
    (void)sender;
    NSInteger idx = [_accountTemplatePopup indexOfSelectedItem];
    if (idx == 1) {
        [_accountAccessKeys removeAllObjects];
        [_accountAccessKeys unionSet:[self guestAccessTemplate]];
    } else if (idx == 2) {
        [_accountAccessKeys removeAllObjects];
        [_accountAccessKeys unionSet:[self adminAccessTemplate]];
    }
    [self syncPermissionCheckboxesFromAccess];
}

- (void)toggleAccountPermission:(id)sender
{
    NSButton *cb = (NSButton *)sender;
    NSString *key = [cb toolTip];
    if (!key || [key length] == 0) return;

    if ([cb state] == NSOnState) {
        [_accountAccessKeys addObject:key];
    } else {
        [_accountAccessKeys removeObject:key];
    }
    [self updateAccountTemplateFromAccessKeys];
}

- (void)resetAccountPassword:(id)sender
{
    (void)sender;
    if (!_selectedAccountLogin || [_selectedAccountLogin length] == 0) return;

    int rc = NSRunAlertPanel(@"Reset Password",
                             @"Clear password for \"%@\" so login requires no password?",
                             @"Reset", @"Cancel", nil, _selectedAccountLogin);
    if (rc != NSAlertDefaultReturn) return;

    [_selectedAccountPassword release];
    _selectedAccountPassword = [@"" retain];
    [self updateAccountPasswordStatus];
    [self saveAccount:nil];
}

- (void)saveAccount:(id)sender
{
    (void)sender;
    [self ensureConfigScaffolding];

    NSString *login = [trimmedString([_accountLoginField stringValue]) lowercaseString];
    if ([login length] == 0) {
        NSRunAlertPanel(@"Invalid Account", @"Login cannot be empty.", @"OK", nil, nil);
        return;
    }

    NSCharacterSet *allowed = [NSCharacterSet characterSetWithCharactersInString:
        @"abcdefghijklmnopqrstuvwxyz0123456789-_"];
    unsigned i;
    for (i = 0; i < [login length]; i++) {
        unichar ch = [login characterAtIndex:i];
        if (![allowed characterIsMember:ch]) {
            NSRunAlertPanel(@"Invalid Login",
                            @"Use lowercase letters, numbers, '-' or '_'.",
                            @"OK", nil, nil);
            return;
        }
    }

    NSString *name = trimmedString([_accountNameField stringValue]);
    if ([name length] == 0) name = login;
    NSString *fileRoot = trimmedString([_accountFileRootField stringValue]);

    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    NSString *path = [usersDir stringByAppendingPathComponent:
        [NSString stringWithFormat:@"%@.yaml", login]];

    if (!_selectedAccountLogin || ![_selectedAccountLogin isEqualToString:login]) {
        NSFileManager *fm = [NSFileManager defaultManager];
        if ([fm fileExistsAtPath:path]) {
            NSRunAlertPanel(@"Account Exists",
                            @"An account with that login already exists.",
                            @"OK", nil, nil);
            return;
        }
    }

    NSMutableString *yaml = [NSMutableString string];
    [yaml appendFormat:@"Login: %@\n", login];
    [yaml appendFormat:@"Name: %@\n", name];
    [yaml appendFormat:@"Password: \"%@\"\n", _selectedAccountPassword ? _selectedAccountPassword : @""];
    [yaml appendString:@"Access:\n"];

    NSArray *sorted = [[_accountAccessKeys allObjects] sortedArrayUsingSelector:
        @selector(caseInsensitiveCompare:)];
    for (i = 0; i < [sorted count]; i++) {
        [yaml appendFormat:@"  %@: true\n", [sorted objectAtIndex:i]];
    }
    if ([fileRoot length] > 0) {
        [yaml appendFormat:@"FileRoot: %@\n", fileRoot];
    }

    if (![yaml writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil]) {
        NSRunAlertPanel(@"Save Failed", @"Could not write account file.", @"OK", nil, nil);
        return;
    }

    [_selectedAccountLogin release];
    _selectedAccountLogin = [login retain];
    [self updateAccountPasswordStatus];
    [self refreshAccountsList:nil];
    NSUInteger row = [_accountsItems indexOfObject:login];
    if (row != NSNotFound) {
        [_accountsTableView selectRow:(int)row byExtendingSelection:NO];
    }
}

- (void)deleteAccount:(id)sender
{
    (void)sender;
    NSString *login = _selectedAccountLogin;
    if (!login || [login length] == 0) return;

    int result = NSRunAlertPanel(@"Delete Account",
                                 @"Delete account \"%@\"?",
                                 @"Delete", @"Cancel", nil, login);
    if (result != NSAlertDefaultReturn) return;

    NSString *path = [[_configDir stringByAppendingPathComponent:@"Users"]
        stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.yaml", login]];
    NSFileManager *fm = [NSFileManager defaultManager];
    if ([fm fileExistsAtPath:path]) {
        [fm removeFileAtPath:path handler:nil];
    }

    [self populateAccountEditorForNewAccount];
    [self refreshAccountsList:nil];
}

- (void)addIPBan:(id)sender
{
    (void)sender;
    NSString *value = trimmedString([_newBanIPField stringValue]);
    if ([value length] == 0) return;
    if (![_bannedIPs containsObject:value]) [_bannedIPs addObject:value];
    [_newBanIPField setStringValue:@""];
    [self writeBanListData];
    [self loadBanListData];
}

- (void)removeIPBan:(id)sender
{
    (void)sender;
    int row = [_bannedIPsTableView selectedRow];
    if (row < 0 || row >= (int)[_bannedIPs count]) return;
    [_bannedIPs removeObjectAtIndex:(unsigned)row];
    [self writeBanListData];
    [self loadBanListData];
}

- (void)addUserBan:(id)sender
{
    (void)sender;
    NSString *value = trimmedString([_newBanUserField stringValue]);
    if ([value length] == 0) return;
    if (![_bannedUsers containsObject:value]) [_bannedUsers addObject:value];
    [_newBanUserField setStringValue:@""];
    [self writeBanListData];
    [self loadBanListData];
}

- (void)removeUserBan:(id)sender
{
    (void)sender;
    int row = [_bannedUsersTableView selectedRow];
    if (row < 0 || row >= (int)[_bannedUsers count]) return;
    [_bannedUsers removeObjectAtIndex:(unsigned)row];
    [self writeBanListData];
    [self loadBanListData];
}

- (void)addNickBan:(id)sender
{
    (void)sender;
    NSString *value = trimmedString([_newBanNickField stringValue]);
    if ([value length] == 0) return;
    if (![_bannedNicks containsObject:value]) [_bannedNicks addObject:value];
    [_newBanNickField setStringValue:@""];
    [self writeBanListData];
    [self loadBanListData];
}

- (void)removeNickBan:(id)sender
{
    (void)sender;
    int row = [_bannedNicksTableView selectedRow];
    if (row < 0 || row >= (int)[_bannedNicks count]) return;
    [_bannedNicks removeObjectAtIndex:(unsigned)row];
    [self writeBanListData];
    [self loadBanListData];
}

- (void)refreshOnlineUsers:(id)sender
{
    (void)sender;
    [self updateOnlineUI];
}

- (void)banSelectedOnlineUser:(id)sender
{
    (void)sender;
    int row = _onlineTableView ? [_onlineTableView selectedRow] : -1;
    if (row < 0 || row >= (int)[_onlineUsersItems count]) return;

    NSDictionary *user = [_onlineUsersItems objectAtIndex:(unsigned)row];
    NSString *ip = [user objectForKey:@"ip"];
    if (!ip || [ip length] == 0) return;

    if (![_bannedIPs containsObject:ip]) {
        [_bannedIPs addObject:ip];
        [_bannedIPs sortUsingSelector:@selector(caseInsensitiveCompare:)];
        [self writeBanListData];
        [self loadBanListData];
    }

    if ([_processManager isRunning]) {
        [_processManager reloadConfiguration];
    }
}

- (void)navigateFilesHome:(id)sender
{
    (void)sender;
    [self loadFilesAtPath:[self resolvedFileRootPath]];
}

- (void)navigateFilesUp:(id)sender
{
    (void)sender;
    if (!_filesCurrentPath || [_filesCurrentPath length] == 0) {
        [self navigateFilesHome:nil];
        return;
    }

    NSString *root = [self resolvedFileRootPath];
    NSString *parent = [_filesCurrentPath stringByDeletingLastPathComponent];
    if (![parent hasPrefix:root] || [parent isEqualToString:@""]) {
        parent = root;
    }
    [self loadFilesAtPath:parent];
}

- (void)refreshFilesList:(id)sender
{
    (void)sender;
    if (_filesCurrentPath && [_filesCurrentPath length] > 0) {
        [self loadFilesAtPath:_filesCurrentPath];
    } else {
        [self loadFilesAtPath:[self resolvedFileRootPath]];
    }
}

- (void)filesTableDoubleClick:(id)sender
{
    (void)sender;
    int row = [_filesTableView clickedRow];
    if (row < 0 || row >= (int)[_filesItems count]) return;
    NSDictionary *entry = [_filesItems objectAtIndex:(unsigned)row];
    NSNumber *isDir = [entry objectForKey:@"isDir"];
    if ([isDir boolValue]) {
        [self loadFilesAtPath:[entry objectForKey:@"path"]];
    }
}

- (void)newsSegmentChanged:(id)sender
{
    (void)sender;
    BOOL showMessageBoard = ([_newsSegmentedControl selectedSegment] == 0);
    [_newsMessageBoardView setHidden:!showMessageBoard];
    [_newsThreadedView setHidden:showMessageBoard];
}

- (void)saveMessageBoard:(id)sender
{
    (void)sender;
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:@"MessageBoard.txt"];
    NSString *text = [_messageBoardTextView string];
    if (![text writeToFile:path atomically:YES
                  encoding:NSUTF8StringEncoding error:nil]) {
        NSRunAlertPanel(@"Unable to Save", @"Could not save MessageBoard.txt.",
                        @"OK", nil, nil);
        return;
    }
    [self setMessageBoardDirty:NO];
}

- (void)refreshThreadedNews:(id)sender
{
    (void)sender;
    [self loadThreadedNewsCategories];
}

- (void)addNewsCategory:(id)sender
{
    (void)sender;
    NSString *name = trimmedString([_newsNewCategoryField stringValue]);
    if ([name length] == 0) return;

    if ([_newsCategoriesByKey objectForKey:name]) {
        NSRunAlertPanel(@"Category Exists",
                        @"A category named \"%@\" already exists.",
                        @"OK", nil, nil, name);
        return;
    }

    NSMutableDictionary *cat = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        name, @"name",
        @"bundle", @"type",
        [NSMutableDictionary dictionary], @"articles",
        nil];
    [_newsCategoriesByKey setObject:cat forKey:name];
    [_newsNewCategoryField setStringValue:@""];
    [_newsSelectedCategoryKey release];
    _newsSelectedCategoryKey = [name retain];
    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
}

- (void)deleteNewsCategory:(id)sender
{
    (void)sender;
    int row = _newsCategoriesTableView ? [_newsCategoriesTableView selectedRow] : -1;
    if (row < 0 || row >= (int)[_newsCategoryItems count]) return;

    NSString *key = [_newsCategoryItems objectAtIndex:(unsigned)row];
    if (![_newsCategoriesByKey objectForKey:key]) return;

    int rc = NSRunAlertPanel(@"Delete Category",
                             @"Delete \"%@\" and all listed articles?",
                             @"Delete", @"Cancel", nil, key);
    if (rc != NSAlertDefaultReturn) return;

    [_newsCategoriesByKey removeObjectForKey:key];
    [_newsSelectedCategoryKey release];
    _newsSelectedCategoryKey = nil;
    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
}

- (NSString *)nextThreadedNewsArticleIDForCategory:(NSDictionary *)category
{
    NSDictionary *articles = category ? [category objectForKey:@"articles"] : nil;
    NSArray *ids = articles ? [articles allKeys] : [NSArray array];
    int maxID = 0;
    unsigned i;
    for (i = 0; i < [ids count]; i++) {
        int v = [[ids objectAtIndex:i] intValue];
        if (v > maxID) maxID = v;
    }
    if (maxID > 0) {
        return [NSString stringWithFormat:@"%d", maxID + 1];
    }
    return [NSString stringWithFormat:@"%u", (unsigned)[[NSDate date] timeIntervalSince1970]];
}

- (void)openNewsArticleEditorForNew:(BOOL)isNew
{
    if (!_newsSelectedCategoryKey || [_newsSelectedCategoryKey length] == 0) {
        NSRunAlertPanel(@"Select Category",
                        @"Select a category before editing articles.",
                        @"OK", nil, nil);
        return;
    }

    if (_newsArticlesTableView && !isNew) {
        int row = [_newsArticlesTableView selectedRow];
        if (row < 0 || row >= (int)[_newsArticleItems count]) return;
    }

    if (!_newsArticleEditorWindow) {
        _newsArticleEditorWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 640, 430)
                      styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                                 NSMiniaturizableWindowMask | NSResizableWindowMask)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [_newsArticleEditorWindow setReleasedWhenClosed:NO];
        [_newsArticleEditorWindow setDelegate:(id)self];

        NSView *content = [_newsArticleEditorWindow contentView];

        NSTextField *tl = makeLabel(@"Title:", 11.0, NO);
        [tl setAlignment:NSRightTextAlignment];
        [tl setFrame:NSMakeRect(8, 396, 70, 17)];
        [content addSubview:tl];
        [tl release];
        _newsArticleTitleField = makeEditField(548);
        [_newsArticleTitleField setFrame:NSMakeRect(84, 392, 548, 22)];
        [content addSubview:_newsArticleTitleField];

        NSTextField *pl = makeLabel(@"Poster:", 11.0, NO);
        [pl setAlignment:NSRightTextAlignment];
        [pl setFrame:NSMakeRect(8, 364, 70, 17)];
        [content addSubview:pl];
        [pl release];
        _newsArticlePosterField = makeEditField(208);
        [_newsArticlePosterField setFrame:NSMakeRect(84, 360, 208, 22)];
        [content addSubview:_newsArticlePosterField];

        NSTextField *dl = makeLabel(@"Date:", 11.0, NO);
        [dl setAlignment:NSRightTextAlignment];
        [dl setFrame:NSMakeRect(308, 364, 60, 17)];
        [content addSubview:dl];
        [dl release];
        _newsArticleDateField = makeEditField(264);
        [_newsArticleDateField setFrame:NSMakeRect(370, 360, 262, 22)];
        [content addSubview:_newsArticleDateField];

        NSScrollView *bodySV = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(8, 40, 624, 312)];
        [bodySV setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [bodySV setHasVerticalScroller:YES];
        [bodySV setBorderType:NSBezelBorder];
        _newsArticleBodyTextView = [[NSTextView alloc]
            initWithFrame:NSMakeRect(0, 0, 624, 312)];
        [_newsArticleBodyTextView setMinSize:NSMakeSize(0, 312)];
        [_newsArticleBodyTextView setMaxSize:NSMakeSize(1e7, 1e7)];
        [_newsArticleBodyTextView setVerticallyResizable:YES];
        [_newsArticleBodyTextView setHorizontallyResizable:NO];
        [_newsArticleBodyTextView setAutoresizingMask:NSViewWidthSizable];
        [[_newsArticleBodyTextView textContainer]
            setContainerSize:NSMakeSize(624, 1e7)];
        [[_newsArticleBodyTextView textContainer] setWidthTracksTextView:YES];
        [_newsArticleBodyTextView setFont:[NSFont fontWithName:@"Monaco" size:10.0]];
        [bodySV setDocumentView:_newsArticleBodyTextView];
        [content addSubview:bodySV];
        [bodySV release];

        NSButton *cancelBtn = makeButton(@"Cancel", self,
                                         @selector(cancelNewsArticleEditor:));
        [cancelBtn setFrame:NSMakeRect(464, 8, 80, 24)];
        [cancelBtn setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
        [content addSubview:cancelBtn];
        [cancelBtn release];

        NSButton *saveBtn = makeButton(@"Save", self,
                                       @selector(saveNewsArticleEditor:));
        [saveBtn setFrame:NSMakeRect(552, 8, 80, 24)];
        [saveBtn setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
        [content addSubview:saveBtn];
        [saveBtn release];
    }

    [_newsEditingCategoryKey release];
    _newsEditingCategoryKey = [_newsSelectedCategoryKey retain];
    [_newsEditingArticleID release];
    _newsEditingArticleID = nil;

    if (isNew) {
        [_newsArticleTitleField setStringValue:@""];
        [_newsArticlePosterField setStringValue:@"admin"];
        NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
        [fmt setDateFormat:@"yyyy-MM-dd HH:mm"];
        [_newsArticleDateField setStringValue:[fmt stringFromDate:[NSDate date]]];
        [fmt release];
        [_newsArticleBodyTextView setString:@""];
        [_newsArticleEditorWindow setTitle:@"Add News Article"];
    } else {
        int row = [_newsArticlesTableView selectedRow];
        if (row < 0 || row >= (int)[_newsArticleItems count]) return;
        NSDictionary *item = [_newsArticleItems objectAtIndex:(unsigned)row];
        NSString *articleID = [item objectForKey:@"id"];
        if (articleID) _newsEditingArticleID = [articleID retain];
        [_newsArticleTitleField setStringValue:[item objectForKey:@"title"] ? [item objectForKey:@"title"] : @""];
        [_newsArticlePosterField setStringValue:[item objectForKey:@"poster"] ? [item objectForKey:@"poster"] : @""];
        [_newsArticleDateField setStringValue:[item objectForKey:@"date"] ? [item objectForKey:@"date"] : @""];
        [_newsArticleBodyTextView setString:[item objectForKey:@"body"] ? [item objectForKey:@"body"] : @""];
        [_newsArticleEditorWindow setTitle:@"Edit News Article"];
    }

    [_newsArticleEditorWindow center];
    [_newsArticleEditorWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)addNewsArticle:(id)sender
{
    (void)sender;
    [self openNewsArticleEditorForNew:YES];
}

- (void)editNewsArticle:(id)sender
{
    (void)sender;
    [self openNewsArticleEditorForNew:NO];
}

- (void)deleteNewsArticle:(id)sender
{
    (void)sender;
    if (!_newsSelectedCategoryKey || !_newsSelectedArticleID) return;

    int rc = NSRunAlertPanel(@"Delete Article",
                             @"Delete selected article from \"%@\"?",
                             @"Delete", @"Cancel", nil, _newsSelectedCategoryKey);
    if (rc != NSAlertDefaultReturn) return;

    NSMutableDictionary *cat = [_newsCategoriesByKey objectForKey:_newsSelectedCategoryKey];
    NSMutableDictionary *articles = [cat objectForKey:@"articles"];
    if (!articles) return;
    [articles removeObjectForKey:_newsSelectedArticleID];
    [_newsSelectedArticleID release];
    _newsSelectedArticleID = nil;
    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
}

- (void)cancelNewsArticleEditor:(id)sender
{
    (void)sender;
    if (_newsArticleEditorWindow) [_newsArticleEditorWindow orderOut:nil];
}

- (void)saveNewsArticleEditor:(id)sender
{
    (void)sender;
    if (!_newsEditingCategoryKey || [_newsEditingCategoryKey length] == 0) return;

    NSString *title = trimmedString([_newsArticleTitleField stringValue]);
    NSString *poster = trimmedString([_newsArticlePosterField stringValue]);
    NSString *date = trimmedString([_newsArticleDateField stringValue]);
    NSString *body = [_newsArticleBodyTextView string];
    if ([title length] == 0) {
        NSRunAlertPanel(@"Missing Title", @"Article title is required.", @"OK", nil, nil);
        return;
    }
    if ([poster length] == 0) poster = @"admin";
    if ([date length] == 0) date = @"";
    if (!body) body = @"";

    NSMutableDictionary *cat = [_newsCategoriesByKey objectForKey:_newsEditingCategoryKey];
    if (!cat) return;
    NSMutableDictionary *articles = [cat objectForKey:@"articles"];
    if (!articles) {
        articles = [NSMutableDictionary dictionary];
        [cat setObject:articles forKey:@"articles"];
    }

    NSString *articleID = _newsEditingArticleID;
    if (!articleID || [articleID length] == 0) {
        articleID = [self nextThreadedNewsArticleIDForCategory:cat];
    }

    NSString *safeTitle = [title stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    NSString *safePoster = [poster stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    NSString *safeDate = [date stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    NSString *safeBody = [body stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];

    NSMutableDictionary *article = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        articleID, @"id",
        safeTitle, @"title",
        safePoster, @"poster",
        safeDate, @"date",
        safeBody, @"body",
        nil];
    [articles setObject:article forKey:articleID];

    [_newsSelectedCategoryKey release];
    _newsSelectedCategoryKey = [_newsEditingCategoryKey retain];
    [_newsSelectedArticleID release];
    _newsSelectedArticleID = [articleID retain];

    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
    [self cancelNewsArticleEditor:nil];
}

- (void)clearLogs:(id)sender
{
    (void)sender;
    [_logEntries removeAllObjects];
    [_logTextView setString:@""];
    if (_onlineLogTextView) [_onlineLogTextView setString:@""];
}

- (void)toggleAutoScroll:(id)sender
{
    (void)sender;
    _autoScroll = ([_autoScrollCheckbox state] == NSOnState);
}

- (void)logFilterChanged:(id)sender
{
    (void)sender;
    [self refreshLogTextView];
}

- (void)toggleStdoutVisibility:(id)sender
{
    (void)sender;
    _showStdout = ([_showStdoutCheckbox state] == NSOnState);
    [self refreshLogTextView];
}

- (void)toggleStderrVisibility:(id)sender
{
    (void)sender;
    _showStderr = ([_showStderrCheckbox state] == NSOnState);
    [self refreshLogTextView];
}

- (void)textDidChange:(NSNotification *)note
{
    if ([note object] == _messageBoardTextView) {
        [self setMessageBoardDirty:YES];
    } else if ([note object] == _logFilterField) {
        [self logFilterChanged:nil];
    }
}

/* ===== Tab data helpers ===== */

- (void)openTextConfigFileNamed:(NSString *)filename title:(NSString *)title
{
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:filename];
    NSFileManager *fm = [NSFileManager defaultManager];
    if (![fm fileExistsAtPath:path]) {
        [@"" writeToFile:path atomically:YES
                encoding:NSUTF8StringEncoding error:nil];
    }

    NSString *text = [NSString stringWithContentsOfFile:path
                                                encoding:NSUTF8StringEncoding
                                                   error:nil];
    if (!text) text = @"";

    if (!_textEditorWindow) {
        _textEditorWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 640, 500)
                      styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                                 NSMiniaturizableWindowMask | NSResizableWindowMask)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [_textEditorWindow setReleasedWhenClosed:NO];
        [_textEditorWindow setDelegate:(id)self];

        NSView *content = [_textEditorWindow contentView];

        NSScrollView *sv = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(8, 40, 624, 452)];
        [sv setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [sv setHasVerticalScroller:YES];
        [sv setBorderType:NSBezelBorder];

        NSSize cs = [sv contentSize];
        _textEditorTextView = [[NSTextView alloc]
            initWithFrame:NSMakeRect(0, 0, cs.width, cs.height)];
        [_textEditorTextView setMinSize:NSMakeSize(0, cs.height)];
        [_textEditorTextView setMaxSize:NSMakeSize(1e7, 1e7)];
        [_textEditorTextView setVerticallyResizable:YES];
        [_textEditorTextView setHorizontallyResizable:NO];
        [_textEditorTextView setAutoresizingMask:NSViewWidthSizable];
        [[_textEditorTextView textContainer]
            setContainerSize:NSMakeSize(cs.width, 1e7)];
        [[_textEditorTextView textContainer] setWidthTracksTextView:YES];
        [_textEditorTextView setFont:[NSFont fontWithName:@"Monaco" size:10.0]];
        [sv setDocumentView:_textEditorTextView];
        [content addSubview:sv];
        [sv release];

        NSButton *saveBtn = makeButton(@"Save", self, @selector(saveTextEditor:));
        [saveBtn setFrame:NSMakeRect(552, 8, 80, 24)];
        [saveBtn setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
        [content addSubview:saveBtn];
        [saveBtn release];

        NSButton *closeBtn = makeButton(@"Close", self, @selector(closeTextEditor:));
        [closeBtn setFrame:NSMakeRect(466, 8, 80, 24)];
        [closeBtn setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
        [content addSubview:closeBtn];
        [closeBtn release];
    }

    [_textEditorFilePath release];
    _textEditorFilePath = [path retain];
    [_textEditorTextView setString:text];

    NSString *winTitle = [NSString stringWithFormat:@"%@ - %@", title, [_mainWindow title]];
    [_textEditorWindow setTitle:winTitle];
    [_textEditorWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)saveTextEditor:(id)sender
{
    (void)sender;
    if (!_textEditorFilePath || !_textEditorTextView) return;
    NSString *text = [_textEditorTextView string];
    if (![text writeToFile:_textEditorFilePath atomically:YES
                  encoding:NSUTF8StringEncoding error:nil]) {
        NSRunAlertPanel(@"Unable to Save", @"Could not save the file.", @"OK", nil, nil);
        return;
    }

    if ([[_textEditorFilePath lastPathComponent] isEqualToString:@"MessageBoard.txt"]) {
        [self loadMessageBoardText];
    }
}

- (void)closeTextEditor:(id)sender
{
    (void)sender;
    if (_textEditorWindow) [_textEditorWindow orderOut:nil];
}

- (void)windowWillClose:(NSNotification *)note
{
    if ([note object] == _textEditorWindow) {
        [_textEditorFilePath release];
        _textEditorFilePath = nil;
    } else if ([note object] == _newAccountWindow) {
        [_newAccountLoginField setStringValue:@""];
        [_newAccountNameField setStringValue:@""];
    } else if ([note object] == _newsArticleEditorWindow) {
        [_newsEditingCategoryKey release];
        _newsEditingCategoryKey = nil;
        [_newsEditingArticleID release];
        _newsEditingArticleID = nil;
    } else if ([note object] == _wizardWindow) {
        _wizardPresented = NO;
    }
}

- (void)loadAccountsListData
{
    [self ensureConfigScaffolding];
    [_accountsItems removeAllObjects];

    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray *contents = [fm directoryContentsAtPath:usersDir];
    if (contents) {
        NSArray *sorted = [contents sortedArrayUsingSelector:
            @selector(caseInsensitiveCompare:)];
        unsigned i;
        for (i = 0; i < [sorted count]; i++) {
            NSString *name = [sorted objectAtIndex:i];
            if (![name hasSuffix:@".yaml"]) continue;
            NSString *login = [name stringByDeletingPathExtension];
            if ([login length] > 0) [_accountsItems addObject:login];
        }
    }

    if (_accountsTableView) [_accountsTableView reloadData];
    if (_accountsCountLabel) {
        [_accountsCountLabel setStringValue:
            [NSString stringWithFormat:@"%u account%@",
             (unsigned)[_accountsItems count],
             ([ _accountsItems count] == 1 ? @"" : @"s")]];
    }

    if (_accountsTableView) {
        NSUInteger idx = NSNotFound;
        if (_selectedAccountLogin) {
            idx = [_accountsItems indexOfObject:_selectedAccountLogin];
        }
        if (idx == NSNotFound && [_accountsItems count] > 0) idx = 0;

        if (idx != NSNotFound) {
            [_accountsTableView selectRow:(int)idx byExtendingSelection:NO];
            NSDictionary *acct = [self loadAccountDataForLogin:
                [_accountsItems objectAtIndex:idx]];
            [self populateAccountEditorFromData:acct];
        } else {
            [self populateAccountEditorForNewAccount];
        }
    }
}

- (NSDictionary *)loadAccountDataForLogin:(NSString *)login
{
    NSMutableSet *access = [NSMutableSet set];
    NSString *name = login ? login : @"";
    NSString *password = @"";
    NSString *fileRoot = @"";
    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    NSString *path = [usersDir stringByAppendingPathComponent:
        [NSString stringWithFormat:@"%@.yaml", login ? login : @""]];

    NSString *yaml = [NSString stringWithContentsOfFile:path
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (yaml) {
        NSArray *lines = [yaml componentsSeparatedByCharactersInSet:
            [NSCharacterSet newlineCharacterSet]];
        BOOL inAccess = NO;
        unsigned i;
        for (i = 0; i < [lines count]; i++) {
            NSString *raw = [lines objectAtIndex:i];
            NSString *line = trimmedString(raw);
            if ([line length] == 0 || [line hasPrefix:@"#"]) continue;

            if (inAccess) {
                BOOL indented = [raw hasPrefix:@"  "] || [raw hasPrefix:@"\t"];
                if (!indented) {
                    inAccess = NO;
                } else {
                    NSRange sep = [line rangeOfString:@":"];
                    if (sep.location != NSNotFound) {
                        NSString *k = trimmedString([line substringToIndex:sep.location]);
                        NSString *v = [[trimmedString([line substringFromIndex:sep.location + 1])
                            lowercaseString] stringByTrimmingCharactersInSet:
                            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                        if ([k length] > 0 &&
                            ([v isEqualToString:@"true"] || [v isEqualToString:@"yes"] ||
                             [v isEqualToString:@"1"])) {
                            [access addObject:k];
                        }
                    }
                    continue;
                }
            }

            NSRange sep = [line rangeOfString:@":"];
            if (sep.location == NSNotFound) continue;
            NSString *key = trimmedString([line substringToIndex:sep.location]);
            NSString *val = yamlUnquote([line substringFromIndex:sep.location + 1]);

            if ([key isEqualToString:@"Access"]) {
                inAccess = YES;
            } else if ([key isEqualToString:@"Login"]) {
                if ([val length] > 0) login = val;
            } else if ([key isEqualToString:@"Name"]) {
                name = val;
            } else if ([key isEqualToString:@"Password"]) {
                password = val;
            } else if ([key isEqualToString:@"FileRoot"]) {
                fileRoot = val;
            }
        }
    }

    if ([name length] == 0) name = login ? login : @"";
    return [NSDictionary dictionaryWithObjectsAndKeys:
        (login ? login : @""), @"login",
        name, @"name",
        password, @"password",
        fileRoot, @"fileRoot",
        access, @"access",
        nil];
}

- (NSMutableSet *)guestAccessTemplate
{
    return [NSMutableSet setWithArray:[NSArray arrayWithObjects:
        @"DownloadFile", @"DownloadFolder", @"ReadChat", @"SendChat",
        @"OpenChat", @"ShowInList", @"NewsReadArt", @"NewsPostArt",
        @"GetClientInfo", @"SendPrivMsg", nil]];
}

- (NSMutableSet *)adminAccessTemplate
{
    NSMutableSet *all = [NSMutableSet set];
    unsigned i;
    for (i = 0; i < kAccountPermissionDefCount; i++) {
        NSString *key = [NSString stringWithUTF8String:kAccountPermissionDefs[i].key];
        [all addObject:key];
    }
    return all;
}

- (void)rebuildAccountPermissionUI
{
    if (!_accountPermissionsContentView) return;

    NSArray *subs = [NSArray arrayWithArray:[_accountPermissionsContentView subviews]];
    unsigned i;
    for (i = 0; i < [subs count]; i++) {
        [[subs objectAtIndex:i] removeFromSuperview];
    }
    [_accountPermissionCheckboxes removeAllObjects];

    NSString *lastGroup = nil;
    float y = 6.0f;
    int col = 0;

    for (i = 0; i < kAccountPermissionDefCount; i++) {
        NSString *group = [NSString stringWithUTF8String:kAccountPermissionDefs[i].group];
        NSString *label = [NSString stringWithUTF8String:kAccountPermissionDefs[i].label];
        NSString *key = [NSString stringWithUTF8String:kAccountPermissionDefs[i].key];

        if (!lastGroup || ![lastGroup isEqualToString:group]) {
            if (col != 0) {
                y += 24.0f;
                col = 0;
            }
            if (lastGroup) y += 6.0f;
            NSTextField *groupLabel = makeLabel(group, 11.0, YES);
            [groupLabel setFrame:NSMakeRect(8, y, 340, 16)];
            [_accountPermissionsContentView addSubview:groupLabel];
            [groupLabel release];
            y += 18.0f;
            lastGroup = group;
        }

        NSButton *cb = [[NSButton alloc] initWithFrame:NSMakeRect(
            8.0f + (float)col * 184.0f, y, 178.0f, 18.0f)];
        [cb setButtonType:NSSwitchButton];
        [cb setTitle:label];
        [cb setFont:[NSFont systemFontOfSize:10.0]];
        [cb setState:[_accountAccessKeys containsObject:key] ? NSOnState : NSOffState];
        [cb setToolTip:key];
        [cb setTarget:self];
        [cb setAction:@selector(toggleAccountPermission:)];
        [_accountPermissionsContentView addSubview:cb];
        [_accountPermissionCheckboxes setObject:cb forKey:key];
        [cb release];

        col++;
        if (col > 1) {
            col = 0;
            y += 22.0f;
        }
    }
    if (col != 0) y += 22.0f;

    float minH = 304.0f;
    if (y + 8.0f < minH) y = minH - 8.0f;
    [_accountPermissionsContentView setFrame:NSMakeRect(0, 0, 366, y + 8.0f)];
}

- (void)syncPermissionCheckboxesFromAccess
{
    NSArray *keys = [_accountPermissionCheckboxes allKeys];
    unsigned i;
    for (i = 0; i < [keys count]; i++) {
        NSString *key = [keys objectAtIndex:i];
        NSButton *cb = [_accountPermissionCheckboxes objectForKey:key];
        [cb setState:[_accountAccessKeys containsObject:key] ? NSOnState : NSOffState];
    }
}

- (void)updateAccountPasswordStatus
{
    if (!_accountPasswordStatusLabel) return;
    NSString *text = @"Password: none";
    if (_selectedAccountPassword && [_selectedAccountPassword length] > 0) {
        text = @"Password: set";
    }
    [_accountPasswordStatusLabel setStringValue:text];
}

- (void)updateAccountTemplateFromAccessKeys
{
    NSMutableSet *guest = [self guestAccessTemplate];
    NSMutableSet *admin = [self adminAccessTemplate];
    if ([_accountAccessKeys isEqualToSet:guest]) {
        [_accountTemplatePopup selectItemAtIndex:1];
    } else if ([_accountAccessKeys isEqualToSet:admin]) {
        [_accountTemplatePopup selectItemAtIndex:2];
    } else {
        [_accountTemplatePopup selectItemAtIndex:0];
    }
}

- (void)populateAccountEditorFromData:(NSDictionary *)acct
{
    NSString *login = [acct objectForKey:@"login"];
    NSString *name = [acct objectForKey:@"name"];
    NSString *fileRoot = [acct objectForKey:@"fileRoot"];
    NSString *password = [acct objectForKey:@"password"];
    NSSet *access = [acct objectForKey:@"access"];

    [_selectedAccountLogin release];
    _selectedAccountLogin = [login retain];
    [_selectedAccountPassword release];
    _selectedAccountPassword = [(password ? password : @"") retain];

    [_accountLoginField setStringValue:(login ? login : @"")];
    [_accountLoginField setEditable:NO];
    [_accountNameField setStringValue:(name ? name : @"")];
    [_accountFileRootField setStringValue:(fileRoot ? fileRoot : @"")];
    [_accountDeleteButton setEnabled:YES];

    [_accountAccessKeys removeAllObjects];
    if (access) [_accountAccessKeys unionSet:access];
    [self updateAccountTemplateFromAccessKeys];
    [self syncPermissionCheckboxesFromAccess];
    [self updateAccountPasswordStatus];
}

- (void)populateAccountEditorForNewAccount
{
    [_selectedAccountLogin release];
    _selectedAccountLogin = nil;
    [_selectedAccountPassword release];
    _selectedAccountPassword = [@"" retain];

    [_accountLoginField setEditable:YES];
    [_accountLoginField setStringValue:@""];
    [_accountNameField setStringValue:@""];
    [_accountFileRootField setStringValue:@""];
    [_accountDeleteButton setEnabled:NO];

    [_accountAccessKeys removeAllObjects];
    [_accountAccessKeys unionSet:[self guestAccessTemplate]];
    [_accountTemplatePopup selectItemAtIndex:1];
    [self syncPermissionCheckboxesFromAccess];
    [self updateAccountPasswordStatus];
}

- (void)loadBanListData
{
    [self ensureConfigScaffolding];
    [_bannedIPs removeAllObjects];
    [_bannedUsers removeAllObjects];
    [_bannedNicks removeAllObjects];

    NSString *path = [_configDir stringByAppendingPathComponent:@"Banlist.yaml"];
    NSString *yaml = [NSString stringWithContentsOfFile:path
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (yaml) {
        NSArray *lines = [yaml componentsSeparatedByCharactersInSet:
            [NSCharacterSet newlineCharacterSet]];
        int section = 0; /* 1 ip, 2 user, 3 nick */
        unsigned i;
        for (i = 0; i < [lines count]; i++) {
            NSString *raw = [lines objectAtIndex:i];
            NSString *line = trimmedString(raw);
            if ([line length] == 0 || [line hasPrefix:@"#"]) continue;

            if ([line isEqualToString:@"banList:"]) {
                section = 1; continue;
            }
            if ([line isEqualToString:@"bannedUsers:"]) {
                section = 2; continue;
            }
            if ([line isEqualToString:@"bannedNicks:"]) {
                section = 3; continue;
            }

            BOOL indented = [raw hasPrefix:@"  "] || [raw hasPrefix:@"\t"];
            if (!indented || section == 0) continue;

            NSRange sep = [line rangeOfString:@":"];
            if (sep.location == NSNotFound) continue;
            NSString *key = trimmedString([line substringToIndex:sep.location]);
            if ([key length] == 0) continue;

            if (section == 1 && ![_bannedIPs containsObject:key]) {
                [_bannedIPs addObject:key];
            } else if (section == 2 && ![_bannedUsers containsObject:key]) {
                [_bannedUsers addObject:key];
            } else if (section == 3 && ![_bannedNicks containsObject:key]) {
                [_bannedNicks addObject:key];
            }
        }
    }

    [_bannedIPs sortUsingSelector:@selector(caseInsensitiveCompare:)];
    [_bannedUsers sortUsingSelector:@selector(caseInsensitiveCompare:)];
    [_bannedNicks sortUsingSelector:@selector(caseInsensitiveCompare:)];

    if (_bannedIPsTableView) [_bannedIPsTableView reloadData];
    if (_bannedUsersTableView) [_bannedUsersTableView reloadData];
    if (_bannedNicksTableView) [_bannedNicksTableView reloadData];
}

- (void)writeBanListData
{
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:@"Banlist.yaml"];
    NSMutableString *yaml = [NSMutableString string];
    unsigned i;

    [yaml appendString:@"banList:\n"];
    for (i = 0; i < [_bannedIPs count]; i++) {
        [yaml appendFormat:@"  %@: ~\n", [_bannedIPs objectAtIndex:i]];
    }
    [yaml appendString:@"bannedUsers:\n"];
    for (i = 0; i < [_bannedUsers count]; i++) {
        [yaml appendFormat:@"  %@: true\n", [_bannedUsers objectAtIndex:i]];
    }
    [yaml appendString:@"bannedNicks:\n"];
    for (i = 0; i < [_bannedNicks count]; i++) {
        [yaml appendFormat:@"  %@: true\n", [_bannedNicks objectAtIndex:i]];
    }

    [yaml writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

- (void)processOnlineLogLine:(NSString *)text
{
    if (!text || [text length] == 0) return;

    if ([text rangeOfString:@"Server started"].location != NSNotFound) {
        [_onlineUsersByID removeAllObjects];
        _onlinePeakConnections = 0;
        _footerDownloadBytes = 0;
        _footerUploadBytes = 0;
        [self updateOnlineUI];
        return;
    }
    if ([text rangeOfString:@"Server shutting down"].location != NSNotFound) {
        [_onlineUsersByID removeAllObjects];
        [self updateOnlineUI];
        return;
    }

    unsigned long long bytes = parseLogByteCount(text);
    if (bytes > 0) {
        if ([text hasPrefix:@"File sent:"] || [text hasPrefix:@"Banner sent"]) {
            _footerDownloadBytes += bytes;
            [self updateFooterStats];
        } else if ([text hasPrefix:@"File received:"] ||
                   [text hasPrefix:@"Folder upload: received"]) {
            _footerUploadBytes += bytes;
            [self updateFooterStats];
        }
    }

    NSRange loginRange = [text rangeOfString:@"Login: "];
    if (loginRange.location != NSNotFound) {
        NSUInteger remoteStart = loginRange.location + [@"Login: " length];
        NSRange asRange = [text rangeOfString:@" as \""
                                      options:0
                                        range:NSMakeRange(remoteStart,
                                            [text length] - remoteStart)];
        if (asRange.location != NSNotFound) {
            NSString *remote = [text substringWithRange:
                NSMakeRange(remoteStart, asRange.location - remoteStart)];

            NSUInteger nameStart = asRange.location + [@" as \"" length];
            NSRange nameEnd = [text rangeOfString:@"\" (id="
                                             options:0
                                               range:NSMakeRange(nameStart,
                                                   [text length] - nameStart)];
            if (nameEnd.location != NSNotFound) {
                NSString *login = [text substringWithRange:
                    NSMakeRange(nameStart, nameEnd.location - nameStart)];
                NSUInteger idStart = nameEnd.location + [@"\" (id=" length];
                NSRange idEnd = [text rangeOfString:@")"
                                           options:0
                                             range:NSMakeRange(idStart,
                                                 [text length] - idStart)];
                if (idEnd.location != NSNotFound) {
                    NSString *idStr = [text substringWithRange:
                        NSMakeRange(idStart, idEnd.location - idStart)];
                    int userID = [idStr intValue];
                    if (userID > 0) {
                        NSString *ip = remote;
                        NSRange portSep = [remote rangeOfString:@":"
                                                         options:NSBackwardsSearch];
                        if (portSep.location != NSNotFound) {
                            ip = [remote substringToIndex:portSep.location];
                        }

                        NSMutableDictionary *user =
                            [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                [NSNumber numberWithInt:userID], @"id",
                                (login ? login : @""), @"login",
                                (ip ? ip : @""), @"ip",
                                [NSDate date], @"lastSeen",
                                nil];
                        [_onlineUsersByID setObject:user
                                            forKey:[NSString stringWithFormat:@"%d", userID]];
                        if ((int)[_onlineUsersByID count] > _onlinePeakConnections) {
                            _onlinePeakConnections = (int)[_onlineUsersByID count];
                        }
                        [self updateOnlineUI];
                        return;
                    }
                }
            }
        }
    }

    NSRange disRange = [text rangeOfString:@"Client disconnected: "];
    if (disRange.location != NSNotFound) {
        NSUInteger idStart = NSNotFound;
        NSRange idMarker = [text rangeOfString:@"(id="];
        if (idMarker.location != NSNotFound) {
            idStart = idMarker.location + [@"(id=" length];
        }
        if (idStart != NSNotFound) {
            NSRange idEnd = [text rangeOfString:@")"
                                       options:0
                                         range:NSMakeRange(idStart,
                                             [text length] - idStart)];
            if (idEnd.location != NSNotFound) {
                NSString *idStr = [text substringWithRange:
                    NSMakeRange(idStart, idEnd.location - idStart)];
                int userID = [idStr intValue];
                if (userID > 0) {
                    [_onlineUsersByID removeObjectForKey:
                        [NSString stringWithFormat:@"%d", userID]];
                    [self updateOnlineUI];
                }
            }
        }
    }
}

- (void)updateOnlineUI
{
    if (!_onlineTableView) {
        [self updateFooterStats];
        return;
    }

    [_onlineUsersItems removeAllObjects];
    NSMutableArray *sorted = [NSMutableArray arrayWithArray:
        [_onlineUsersByID allValues]];
    int i;
    for (i = 1; i < (int)[sorted count]; i++) {
        id item = [sorted objectAtIndex:(unsigned)i];
        int itemID = [[item objectForKey:@"id"] intValue];
        int j = i - 1;
        while (j >= 0) {
            id prev = [sorted objectAtIndex:(unsigned)j];
            int prevID = [[prev objectForKey:@"id"] intValue];
            if (prevID <= itemID) break;
            [sorted replaceObjectAtIndex:(unsigned)(j + 1) withObject:prev];
            j--;
        }
        [sorted replaceObjectAtIndex:(unsigned)(j + 1) withObject:item];
    }
    [_onlineUsersItems addObjectsFromArray:sorted];

    [_onlineTableView reloadData];

    NSString *status = [NSString stringWithFormat:
        @"Connected: %u  Peak: %d",
        (unsigned)[_onlineUsersItems count], _onlinePeakConnections];
    [_onlineStatusLabel setStringValue:status];
    [self updateFooterStats];

    int sel = [_onlineTableView selectedRow];
    [_onlineBanButton setEnabled:(sel >= 0 && sel < (int)[_onlineUsersItems count])];
}

- (NSString *)resolvedFileRootPath
{
    NSString *root = @"";
    if (_fileRootField) root = trimmedString([_fileRootField stringValue]);
    if (!root || [root length] == 0) root = @"Files";
    if ([root hasPrefix:@"/"]) return root;
    return [_configDir stringByAppendingPathComponent:root];
}

- (void)loadFilesAtPath:(NSString *)path
{
    if (!path || [path length] == 0) path = [self resolvedFileRootPath];

    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:path isDirectory:&isDir] || !isDir) {
        [_filesItems removeAllObjects];
        if (_filesTableView) [_filesTableView reloadData];
        if (_filesPathLabel) [_filesPathLabel setStringValue:@"File root not found"];
        return;
    }

    [path retain];
    [_filesCurrentPath release];
    _filesCurrentPath = path;

    if (_filesPathLabel) [_filesPathLabel setStringValue:_filesCurrentPath];

    [_filesItems removeAllObjects];
    NSArray *contents = [fm directoryContentsAtPath:_filesCurrentPath];
    NSArray *sorted = contents ? [contents sortedArrayUsingSelector:
        @selector(caseInsensitiveCompare:)] : [NSArray array];
    NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
    [fmt setDateFormat:@"yyyy-MM-dd HH:mm"];

    unsigned i;
    for (i = 0; i < [sorted count]; i++) {
        NSString *name = [sorted objectAtIndex:i];
        if ([name hasPrefix:@"."]) continue;

        NSString *fullPath = [_filesCurrentPath stringByAppendingPathComponent:name];
        BOOL entryIsDir = NO;
        if (![fm fileExistsAtPath:fullPath isDirectory:&entryIsDir]) continue;

        NSDictionary *attrs = [fm fileAttributesAtPath:fullPath traverseLink:YES];
        NSString *sizeText = entryIsDir ? @"—" : @"";
        NSString *dateText = @"";

        if (!entryIsDir && attrs) {
            NSNumber *sizeNum = [attrs objectForKey:NSFileSize];
            if (sizeNum) sizeText = humanFileSize([sizeNum unsignedLongLongValue]);
        }
        if (attrs) {
            NSDate *modDate = [attrs objectForKey:NSFileModificationDate];
            if (modDate) dateText = [fmt stringFromDate:modDate];
        }

        NSDictionary *item = [NSDictionary dictionaryWithObjectsAndKeys:
            name, @"name",
            fullPath, @"path",
            [NSNumber numberWithBool:entryIsDir], @"isDir",
            sizeText, @"size",
            dateText, @"modified",
            nil];
        [_filesItems addObject:item];
    }
    [fmt release];

    if (_filesTableView) [_filesTableView reloadData];
}

- (void)loadMessageBoardText
{
    if (!_messageBoardTextView) return;
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:@"MessageBoard.txt"];
    NSString *text = [NSString stringWithContentsOfFile:path
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (!text) text = @"";
    [_messageBoardTextView setString:text];
    [self setMessageBoardDirty:NO];
}

- (void)loadThreadedNewsCategories
{
    [self ensureConfigScaffolding];
    [_newsCategoryItems removeAllObjects];
    [_newsArticleItems removeAllObjects];
    [_newsCategoriesByKey removeAllObjects];

    NSString *path = [_configDir stringByAppendingPathComponent:@"ThreadedNews.yaml"];
    NSString *yaml = [NSString stringWithContentsOfFile:path
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (yaml) {
        NSArray *lines = [yaml componentsSeparatedByCharactersInSet:
            [NSCharacterSet newlineCharacterSet]];
        NSString *currentCategoryKey = nil;
        NSMutableDictionary *currentCategory = nil;
        NSMutableDictionary *currentArticles = nil;
        NSMutableDictionary *currentArticle = nil;
        unsigned i;
        for (i = 0; i < [lines count]; i++) {
            NSString *raw = [lines objectAtIndex:i];
            NSString *line = trimmedString(raw);
            int indent = leadingSpaces(raw);
            if ([line length] == 0 || [line hasPrefix:@"#"]) continue;
            if (indent == 0) continue;

            if (indent == 2 && [line hasSuffix:@":"]) {
                NSString *key = trimmedString([line substringToIndex:[line length] - 1]);
                key = yamlUnquote(key);
                if ([key length] == 0) continue;
                currentCategoryKey = key;
                currentCategory = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    key, @"name",
                    @"bundle", @"type",
                    [NSMutableDictionary dictionary], @"articles",
                    nil];
                [_newsCategoriesByKey setObject:currentCategory forKey:key];
                currentArticles = [currentCategory objectForKey:@"articles"];
                currentArticle = nil;
                continue;
            }

            if (!currentCategory) continue;

            NSRange sep = [line rangeOfString:@":"];
            if (sep.location == NSNotFound) continue;
            NSString *key = trimmedString([line substringToIndex:sep.location]);
            NSString *val = yamlUnquote([line substringFromIndex:sep.location + 1]);

            if (indent == 4) {
                if ([key isEqualToString:@"Name"] && [val length] > 0) {
                    [currentCategory setObject:val forKey:@"name"];
                } else if ([key isEqualToString:@"Type"] && [val length] > 0) {
                    [currentCategory setObject:val forKey:@"type"];
                } else if ([key isEqualToString:@"Articles"]) {
                    currentArticle = nil;
                }
                continue;
            }

            if (indent == 6 && [line hasSuffix:@":"]) {
                NSString *articleID = trimmedString([line substringToIndex:[line length] - 1]);
                articleID = yamlUnquote(articleID);
                if ([articleID length] == 0) continue;
                currentArticle = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    articleID, @"id",
                    @"", @"title",
                    @"", @"poster",
                    @"", @"date",
                    @"", @"body",
                    nil];
                if (!currentArticles) {
                    currentArticles = [NSMutableDictionary dictionary];
                    [currentCategory setObject:currentArticles forKey:@"articles"];
                }
                [currentArticles setObject:currentArticle forKey:articleID];
                continue;
            }

            if (indent >= 8 && currentArticle) {
                NSString *decodedVal = [val stringByReplacingOccurrencesOfString:@"\\n"
                                                                       withString:@"\n"];
                if ([key isEqualToString:@"Title"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"title"];
                } else if ([key isEqualToString:@"Poster"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"poster"];
                } else if ([key isEqualToString:@"Date"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"date"];
                } else if ([key isEqualToString:@"Body"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"body"];
                }
            }
        }
        (void)currentCategoryKey;
    }

    NSArray *keys = [[_newsCategoriesByKey allKeys] sortedArrayUsingSelector:
        @selector(caseInsensitiveCompare:)];
    [_newsCategoryItems addObjectsFromArray:keys];

    if (_newsCategoriesTableView) {
        [_newsCategoriesTableView reloadData];
        if ([_newsCategoryItems count] > 0) {
            NSUInteger idx = NSNotFound;
            if (_newsSelectedCategoryKey) {
                idx = [_newsCategoryItems indexOfObject:_newsSelectedCategoryKey];
            }
            if (idx == NSNotFound) idx = 0;
            [_newsCategoriesTableView selectRow:(int)idx byExtendingSelection:NO];
            [_newsSelectedCategoryKey release];
            _newsSelectedCategoryKey = [[_newsCategoryItems objectAtIndex:idx] retain];
        } else {
            [_newsSelectedCategoryKey release];
            _newsSelectedCategoryKey = nil;
        }
    }
    [self refreshThreadedNewsArticles];
}

- (void)writeThreadedNewsCategories
{
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:@"ThreadedNews.yaml"];

    if ([_newsCategoriesByKey count] == 0) {
        [@"Categories: {}\n" writeToFile:path atomically:YES
                                encoding:NSUTF8StringEncoding error:nil];
        return;
    }

    NSMutableString *yaml = [NSMutableString stringWithString:@"Categories:\n"];
    NSArray *keys = [[_newsCategoriesByKey allKeys] sortedArrayUsingSelector:
        @selector(caseInsensitiveCompare:)];
    unsigned i;
    for (i = 0; i < [keys count]; i++) {
        NSString *key = [keys objectAtIndex:i];
        NSDictionary *cat = [_newsCategoriesByKey objectForKey:key];
        NSString *name = [cat objectForKey:@"name"];
        NSString *type = [cat objectForKey:@"type"];
        NSDictionary *articles = [cat objectForKey:@"articles"];
        if (!name) name = key;
        if (!type || [type length] == 0) type = @"bundle";

        [yaml appendFormat:@"  %@:\n", yamlQuoted(key)];
        [yaml appendFormat:@"    Name: %@\n", yamlQuoted(name)];
        [yaml appendFormat:@"    Type: %@\n", yamlQuoted(type)];

        if (!articles || [articles count] == 0) {
            [yaml appendString:@"    Articles: {}\n"];
            continue;
        }

        [yaml appendString:@"    Articles:\n"];
        NSArray *articleIDs = [[articles allKeys] sortedArrayUsingSelector:
            @selector(caseInsensitiveCompare:)];
        unsigned j;
        for (j = 0; j < [articleIDs count]; j++) {
            NSString *articleID = [articleIDs objectAtIndex:j];
            NSDictionary *art = [articles objectForKey:articleID];
            NSString *title = [art objectForKey:@"title"];
            NSString *poster = [art objectForKey:@"poster"];
            NSString *date = [art objectForKey:@"date"];
            NSString *body = [art objectForKey:@"body"];
            if (title) title = [title stringByReplacingOccurrencesOfString:@"\n"
                                                                 withString:@"\\n"];
            if (poster) poster = [poster stringByReplacingOccurrencesOfString:@"\n"
                                                                     withString:@"\\n"];
            if (date) date = [date stringByReplacingOccurrencesOfString:@"\n"
                                                               withString:@"\\n"];
            if (body) body = [body stringByReplacingOccurrencesOfString:@"\n"
                                                               withString:@"\\n"];

            [yaml appendFormat:@"      %@:\n", yamlQuoted(articleID)];
            [yaml appendFormat:@"        Title: %@\n", yamlQuoted(title ? title : @"")];
            [yaml appendFormat:@"        Poster: %@\n", yamlQuoted(poster ? poster : @"")];
            [yaml appendFormat:@"        Date: %@\n", yamlQuoted(date ? date : @"")];
            [yaml appendFormat:@"        Body: %@\n", yamlQuoted(body ? body : @"")];
        }
    }

    [yaml writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

- (void)refreshThreadedNewsArticles
{
    [_newsArticleItems removeAllObjects];
    NSString *selected = _newsSelectedCategoryKey;
    if (!selected && _newsCategoriesTableView) {
        int row = [_newsCategoriesTableView selectedRow];
        if (row >= 0 && row < (int)[_newsCategoryItems count]) {
            selected = [_newsCategoryItems objectAtIndex:(unsigned)row];
        }
    }

    if (selected) {
        NSDictionary *cat = [_newsCategoriesByKey objectForKey:selected];
        NSDictionary *articles = [cat objectForKey:@"articles"];
        NSArray *articleIDs = [[articles allKeys] sortedArrayUsingSelector:
            @selector(caseInsensitiveCompare:)];
        unsigned i;
        for (i = 0; i < [articleIDs count]; i++) {
            NSString *articleID = [articleIDs objectAtIndex:i];
            NSDictionary *art = [articles objectForKey:articleID];
            NSMutableDictionary *item = [NSMutableDictionary dictionary];
            [item setObject:articleID forKey:@"id"];
            [item setObject:([art objectForKey:@"title"] ? [art objectForKey:@"title"] : @"")
                     forKey:@"title"];
            [item setObject:([art objectForKey:@"poster"] ? [art objectForKey:@"poster"] : @"")
                     forKey:@"poster"];
            [item setObject:([art objectForKey:@"date"] ? [art objectForKey:@"date"] : @"")
                     forKey:@"date"];
            [item setObject:([art objectForKey:@"body"] ? [art objectForKey:@"body"] : @"")
                     forKey:@"body"];
            [_newsArticleItems addObject:item];
        }
    }

    if (_newsArticlesTableView) {
        [_newsArticlesTableView reloadData];
        int targetRow = -1;
        if (_newsSelectedArticleID && [selected length] > 0) {
            unsigned i;
            for (i = 0; i < [_newsArticleItems count]; i++) {
                NSDictionary *item = [_newsArticleItems objectAtIndex:i];
                if ([[_newsSelectedArticleID description] isEqualToString:
                    [[item objectForKey:@"id"] description]]) {
                    targetRow = (int)i;
                    break;
                }
            }
        }
        if (targetRow >= 0) {
            [_newsArticlesTableView selectRow:targetRow byExtendingSelection:NO];
        } else {
            [_newsArticlesTableView deselectAll:nil];
            [_newsSelectedArticleID release];
            _newsSelectedArticleID = nil;
        }
    }
    if (_newsDeleteCategoryButton) {
        [_newsDeleteCategoryButton setEnabled:(selected != nil)];
    }
    if (_newsAddArticleButton) {
        [_newsAddArticleButton setEnabled:(selected != nil)];
    }
    if (_newsEditArticleButton) {
        [_newsEditArticleButton setEnabled:(_newsSelectedArticleID != nil)];
    }
    if (_newsDeleteArticleButton) {
        [_newsDeleteArticleButton setEnabled:(_newsSelectedArticleID != nil)];
    }
}

- (void)setMessageBoardDirty:(BOOL)dirty
{
    _messageBoardDirty = dirty;
    if (_messageBoardDirtyLabel) {
        [_messageBoardDirtyLabel setHidden:!dirty];
    }
}

/* ===== NSTableView datasource ===== */

- (int)numberOfRowsInTableView:(NSTableView *)tableView
{
    if (tableView == _accountsTableView) return (int)[_accountsItems count];
    if (tableView == _trackerTableView) return (int)[_trackerItems count];
    if (tableView == _ignoreFilesTableView) return (int)[_ignoreFileItems count];
    if (tableView == _onlineTableView) return (int)[_onlineUsersItems count];
    if (tableView == _bannedIPsTableView) return (int)[_bannedIPs count];
    if (tableView == _bannedUsersTableView) return (int)[_bannedUsers count];
    if (tableView == _bannedNicksTableView) return (int)[_bannedNicks count];
    if (tableView == _filesTableView) return (int)[_filesItems count];
    if (tableView == _newsCategoriesTableView) return (int)[_newsCategoryItems count];
    if (tableView == _newsArticlesTableView) return (int)[_newsArticleItems count];
    return 0;
}

- (id)tableView:(NSTableView *)tableView
objectValueForTableColumn:(NSTableColumn *)tableColumn row:(int)row
{
    if (tableView == _accountsTableView) {
        if (row < 0 || row >= (int)[_accountsItems count]) return @"";
        return [_accountsItems objectAtIndex:(unsigned)row];
    }
    if (tableView == _trackerTableView) {
        if (row < 0 || row >= (int)[_trackerItems count]) return @"";
        return [_trackerItems objectAtIndex:(unsigned)row];
    }
    if (tableView == _ignoreFilesTableView) {
        if (row < 0 || row >= (int)[_ignoreFileItems count]) return @"";
        return [_ignoreFileItems objectAtIndex:(unsigned)row];
    }

    if (tableView == _filesTableView) {
        if (row < 0 || row >= (int)[_filesItems count]) return @"";
        NSDictionary *item = [_filesItems objectAtIndex:(unsigned)row];
        NSString *cid = [tableColumn identifier];
        id value = [item objectForKey:cid];
        return value ? value : @"";
    }

    if (tableView == _onlineTableView) {
        if (row < 0 || row >= (int)[_onlineUsersItems count]) return @"";
        NSDictionary *item = [_onlineUsersItems objectAtIndex:(unsigned)row];
        NSString *cid = [tableColumn identifier];
        if ([cid isEqualToString:@"online_login"]) {
            return [item objectForKey:@"login"];
        } else if ([cid isEqualToString:@"online_ip"]) {
            return [item objectForKey:@"ip"];
        } else if ([cid isEqualToString:@"online_id"]) {
            return [NSString stringWithFormat:@"%d",
                [[item objectForKey:@"id"] intValue]];
        } else if ([cid isEqualToString:@"online_seen"]) {
            NSDate *d = [item objectForKey:@"lastSeen"];
            NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
            [fmt setDateFormat:@"HH:mm:ss"];
            NSString *s = d ? [fmt stringFromDate:d] : @"";
            [fmt release];
            return s;
        }
        return @"";
    }

    if (tableView == _bannedIPsTableView) {
        if (row < 0 || row >= (int)[_bannedIPs count]) return @"";
        return [_bannedIPs objectAtIndex:(unsigned)row];
    }

    if (tableView == _bannedUsersTableView) {
        if (row < 0 || row >= (int)[_bannedUsers count]) return @"";
        return [_bannedUsers objectAtIndex:(unsigned)row];
    }

    if (tableView == _bannedNicksTableView) {
        if (row < 0 || row >= (int)[_bannedNicks count]) return @"";
        return [_bannedNicks objectAtIndex:(unsigned)row];
    }

    if (tableView == _newsCategoriesTableView) {
        if (row < 0 || row >= (int)[_newsCategoryItems count]) return @"";
        return [_newsCategoryItems objectAtIndex:(unsigned)row];
    }
    if (tableView == _newsArticlesTableView) {
        if (row < 0 || row >= (int)[_newsArticleItems count]) return @"";
        NSDictionary *item = [_newsArticleItems objectAtIndex:(unsigned)row];
        NSString *cid = [tableColumn identifier];
        if ([cid isEqualToString:@"news_title"]) return [item objectForKey:@"title"];
        if ([cid isEqualToString:@"news_poster"]) return [item objectForKey:@"poster"];
        if ([cid isEqualToString:@"news_date"]) return [item objectForKey:@"date"];
        if ([cid isEqualToString:@"news_body"]) return [item objectForKey:@"body"];
        return @"";
    }
    return @"";
}

- (void)tableViewSelectionDidChange:(NSNotification *)note
{
    NSTableView *tv = [note object];
    if (tv == _onlineTableView) {
        int sel = [_onlineTableView selectedRow];
        [_onlineBanButton setEnabled:(sel >= 0 && sel < (int)[_onlineUsersItems count])];
        return;
    }
    if (tv == _trackerTableView) {
        int sel = [_trackerTableView selectedRow];
        if (_removeTrackerButton) {
            [_removeTrackerButton setEnabled:(sel >= 0 && sel < (int)[_trackerItems count])];
        }
        return;
    }
    if (tv == _ignoreFilesTableView) {
        int sel = [_ignoreFilesTableView selectedRow];
        if (_removeIgnoreFileButton) {
            [_removeIgnoreFileButton setEnabled:(sel >= 0 && sel < (int)[_ignoreFileItems count])];
        }
        return;
    }
    if (tv == _newsCategoriesTableView) {
        int row = [_newsCategoriesTableView selectedRow];
        [_newsSelectedCategoryKey release];
        _newsSelectedCategoryKey = nil;
        [_newsSelectedArticleID release];
        _newsSelectedArticleID = nil;
        if (row >= 0 && row < (int)[_newsCategoryItems count]) {
            _newsSelectedCategoryKey =
                [[_newsCategoryItems objectAtIndex:(unsigned)row] retain];
        }
        [self refreshThreadedNewsArticles];
        return;
    }
    if (tv == _newsArticlesTableView) {
        int row = [_newsArticlesTableView selectedRow];
        [_newsSelectedArticleID release];
        _newsSelectedArticleID = nil;
        if (row >= 0 && row < (int)[_newsArticleItems count]) {
            NSDictionary *item = [_newsArticleItems objectAtIndex:(unsigned)row];
            NSString *articleID = [item objectForKey:@"id"];
            if (articleID) _newsSelectedArticleID = [articleID retain];
        }
        if (_newsEditArticleButton) {
            [_newsEditArticleButton setEnabled:(_newsSelectedArticleID != nil)];
        }
        if (_newsDeleteArticleButton) {
            [_newsDeleteArticleButton setEnabled:(_newsSelectedArticleID != nil)];
        }
        return;
    }
    if (tv != _accountsTableView) return;

    int row = [_accountsTableView selectedRow];
    if (row < 0 || row >= (int)[_accountsItems count]) {
        [self populateAccountEditorForNewAccount];
        return;
    }

    NSString *login = [_accountsItems objectAtIndex:(unsigned)row];
    NSDictionary *acct = [self loadAccountDataForLogin:login];
    [self populateAccountEditorFromData:acct];
}

/* ===== Notifications ===== */

- (void)serverStatusChanged:(NSNotification *)note
{
    (void)note;
    [self updateServerUI];
}

- (void)logLineReceived:(NSNotification *)note
{
    NSDictionary *info = [note userInfo];
    NSString *text = [info objectForKey:@"text"];
    NSNumber *src = [info objectForKey:@"source"];
    NSDate *ts = [info objectForKey:@"timestamp"];

    NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
    [fmt setDateFormat:@"HH:mm:ss"];
    NSString *timeStr = [fmt stringFromDate:ts];
    [fmt release];

    NSDictionary *entry = [NSDictionary dictionaryWithObjectsAndKeys:
        (timeStr ? timeStr : @""), @"time",
        (text ? text : @""), @"text",
        (src ? src : [NSNumber numberWithInt:LogSourceStdout]), @"source",
        nil];
    [_logEntries addObject:entry];

    NSString *line = [NSString stringWithFormat:@"%@  %@\n", timeStr, text];

    NSMutableDictionary *attrs = [NSMutableDictionary dictionary];
    [attrs setObject:[NSFont fontWithName:@"Monaco" size:10.0]
              forKey:NSFontAttributeName];
    if ([src intValue] == LogSourceStderr)
        [attrs setObject:[NSColor redColor]
                  forKey:NSForegroundColorAttributeName];

    NSAttributedString *as = [[NSAttributedString alloc]
        initWithString:line attributes:attrs];
    if (_onlineLogTextView) {
        [[_onlineLogTextView textStorage] appendAttributedString:as];
    }
    [as release];

    [self refreshLogTextView];
    [self processOnlineLogLine:text];

    if (_onlineLogTextView) {
        [_onlineLogTextView scrollRangeToVisible:
            NSMakeRange([[_onlineLogTextView string] length], 0)];
    }
}

- (void)refreshLogTextView
{
    if (!_logTextView) return;
    [_logTextView setString:@""];

    NSString *filter = _logFilterField ? [[_logFilterField stringValue] lowercaseString] : @"";
    BOOL hasFilter = (filter && [filter length] > 0);

    unsigned i;
    for (i = 0; i < [_logEntries count]; i++) {
        NSDictionary *entry = [_logEntries objectAtIndex:i];
        NSNumber *src = [entry objectForKey:@"source"];
        int source = src ? [src intValue] : LogSourceStdout;

        if (source == LogSourceStdout && !_showStdout) continue;
        if (source == LogSourceStderr && !_showStderr) continue;

        NSString *text = [entry objectForKey:@"text"];
        if (!text) text = @"";
        if (hasFilter) {
            if ([[text lowercaseString] rangeOfString:filter].location == NSNotFound) {
                continue;
            }
        }

        NSString *timeStr = [entry objectForKey:@"time"];
        if (!timeStr) timeStr = @"";
        NSString *line = [NSString stringWithFormat:@"%@  %@\n", timeStr, text];

        NSMutableDictionary *attrs = [NSMutableDictionary dictionary];
        [attrs setObject:[NSFont fontWithName:@"Monaco" size:10.0]
                  forKey:NSFontAttributeName];
        if (source == LogSourceStderr) {
            [attrs setObject:[NSColor redColor]
                      forKey:NSForegroundColorAttributeName];
        }

        NSAttributedString *as = [[NSAttributedString alloc]
            initWithString:line attributes:attrs];
        [[_logTextView textStorage] appendAttributedString:as];
        [as release];
    }

    if (_autoScroll) {
        [_logTextView scrollRangeToVisible:
            NSMakeRange([[_logTextView string] length], 0)];
    }
}

/* ===== UI update ===== */

- (void)updateFooterStats
{
    if (!_footerConnectedLabel || !_footerPeakLabel ||
        !_footerDLLabel || !_footerULLabel) {
        return;
    }

    NSUInteger connected = [_onlineUsersByID count];
    [_footerConnectedLabel setStringValue:
        [NSString stringWithFormat:@"Connected: %u", (unsigned)connected]];
    [_footerPeakLabel setStringValue:
        [NSString stringWithFormat:@"Peak: %d", _onlinePeakConnections]];
    [_footerDLLabel setStringValue:
        [NSString stringWithFormat:@"DL: %@", humanFileSize(_footerDownloadBytes)]];
    [_footerULLabel setStringValue:
        [NSString stringWithFormat:@"UL: %@", humanFileSize(_footerUploadBytes)]];

    [_footerConnectedLabel sizeToFit];
    [_footerPeakLabel sizeToFit];
    [_footerDLLabel sizeToFit];
    [_footerULLabel sizeToFit];

    NSView *footer = [_footerConnectedLabel superview];
    if (!footer) return;

    float right = [footer bounds].size.width - 10.0f;
    NSRect f;

    f = [_footerULLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerULLabel setFrame:f];
    right = f.origin.x - 14.0f;

    f = [_footerDLLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerDLLabel setFrame:f];
    right = f.origin.x - 14.0f;

    f = [_footerPeakLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerPeakLabel setFrame:f];
    right = f.origin.x - 14.0f;

    f = [_footerConnectedLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerConnectedLabel setFrame:f];
}

- (void)updateServerUI
{
    BOOL running = [_processManager isRunning];
    ServerStatus st = [_processManager status];

    if (!running && [_onlineUsersByID count] > 0) {
        [_onlineUsersByID removeAllObjects];
        [self updateOnlineUI];
    }

    switch (st) {
        case ServerStatusStopped:
            [_statusLabel setStringValue:@"Server is stopped"];
            [_portInfoLabel setStringValue:@""];
            break;
        case ServerStatusStarting:
            [_statusLabel setStringValue:@"Server is starting..."];
            break;
        case ServerStatusRunning:
            [_statusLabel setStringValue:@"Server is running"];
            [_portInfoLabel setStringValue:
                [NSString stringWithFormat:@"Hotline port %d", _serverPort]];
            break;
        case ServerStatusError:
            [_statusLabel setStringValue:
                [NSString stringWithFormat:@"Error: %@",
                 [_processManager errorMessage]]];
            [_portInfoLabel setStringValue:@""];
            break;
    }

    NSColor *dotColor;
    switch (st) {
        case ServerStatusRunning:
            dotColor = [NSColor colorWithCalibratedRed:0.2 green:0.8
                                                 blue:0.2 alpha:1.0];
            break;
        case ServerStatusStarting:
            dotColor = [NSColor colorWithCalibratedRed:0.9 green:0.8
                                                 blue:0.0 alpha:1.0];
            break;
        case ServerStatusError:
            dotColor = [NSColor redColor]; break;
        default:
            dotColor = [NSColor grayColor]; break;
    }
    [(NSBox *)_statusIndicator setFillColor:dotColor];
    [(NSBox *)_footerStatusDot setFillColor:dotColor];

    [_startButton setEnabled:!running];
    [_stopButton setEnabled:running];
    [_restartButton setEnabled:[_processManager hasBinary]];
    if (_reloadButton) [_reloadButton setEnabled:running];

    [_footerStatusLabel setStringValue:[_processManager statusLabel]];
    [_footerStatusLabel sizeToFit];
    if (running) {
        [_footerPortLabel setStringValue:
            [NSString stringWithFormat:@"port %d", _serverPort]];
        NSRect slF = [_footerStatusLabel frame];
        [_footerPortLabel setFrameOrigin:
            NSMakePoint(slF.origin.x + slF.size.width + 8, 6)];
    } else {
        [_footerPortLabel setStringValue:@""];
    }
    [self updateFooterStats];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    SEL action = [item action];
    BOOL running = [_processManager isRunning];
    if (action == @selector(startServer:)) return !running;
    if (action == @selector(stopServer:)) return running;
    if (action == @selector(restartServer:)) return [_processManager hasBinary];
    if (action == @selector(reloadServerConfig:)) return running;
    return YES;
}

@end
