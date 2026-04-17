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
        _trackerItems = [[NSMutableArray alloc] initWithObjects:
            @"tracked.mainecyber.com:5498",
            @"tracker.preterhuman.net:5498",
            @"badmoon.biz:5498", nil];
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
        _disclosureSections = [[NSMutableArray alloc] init];

        /* Load polling interval preference (default 10s) */
        {
            NSNumber *pval = [[NSUserDefaults standardUserDefaults]
                objectForKey:@"pollingInterval"];
            _pollingInterval = pval ? [pval doubleValue] : 10.0;
            if (_pollingInterval <= 0) _pollingInterval = 10.0;
        }

        /* Find server binary in the application bundle. */
        NSBundle *bundle = [NSBundle mainBundle];
        NSFileManager *fm = [NSFileManager defaultManager];
        NSString *binPath = nil;
        /* Note: do NOT include "lemoniscate" — on HFS+ (case-insensitive)
         * it matches "Lemoniscate" (the GUI binary). */
        NSArray *names = [NSArray arrayWithObjects:
            @"lemoniscate-server", nil];
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
    if (_newsRefreshTimer) {
        [_newsRefreshTimer invalidate];
        [_newsRefreshTimer release];
        _newsRefreshTimer = nil;
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

    /* Show wizard only if no plist config exists */
    NSString *plistPath = [NSHomeDirectory()
        stringByAppendingPathComponent:@"Library/Preferences/com.lemoniscate.server.plist"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:plistPath]) {
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

    /* binaryPath is discovered fresh on each launch — don't restore stale paths */
}

- (void)loadConfigFromDisk
{
    NSString *plistPath = [NSHomeDirectory()
        stringByAppendingPathComponent:@"Library/Preferences/com.lemoniscate.server.plist"];
    NSDictionary *dict = [NSDictionary dictionaryWithContentsOfFile:plistPath];
    if (!dict) return;

    [_trackerItems removeAllObjects];
    [_ignoreFileItems removeAllObjects];

    NSString *val;

    val = [dict objectForKey:@"ServerName"];
    if (val) {
        [val retain]; [_serverName release]; _serverName = val;
        if (_serverNameField) [_serverNameField setStringValue:_serverName];
    }
    val = [dict objectForKey:@"ServerDescription"];
    if (val) {
        [val retain]; [_serverDescription release]; _serverDescription = val;
        if (_descriptionField) [_descriptionField setStringValue:_serverDescription];
    }
    val = [dict objectForKey:@"BannerFile"];
    if (val && _bannerFileField) [_bannerFileField setStringValue:val];
    val = [dict objectForKey:@"FileRoot"];
    if (val && _fileRootField) [_fileRootField setStringValue:val];

    NSNumber *boolVal;
    boolVal = [dict objectForKey:@"EnableBonjour"];
    if (boolVal && _bonjourCheckbox)
        [_bonjourCheckbox setState:[boolVal boolValue] ? NSControlStateValueOn : NSControlStateValueOff];
    boolVal = [dict objectForKey:@"EnableTrackerRegistration"];
    if (boolVal && _trackerCheckbox)
        [_trackerCheckbox setState:[boolVal boolValue] ? NSControlStateValueOn : NSControlStateValueOff];
    boolVal = [dict objectForKey:@"PreserveResourceForks"];
    if (boolVal && _preserveForkCheckbox)
        [_preserveForkCheckbox setState:[boolVal boolValue] ? NSControlStateValueOn : NSControlStateValueOff];

    boolVal = [dict objectForKey:@"EnableHOPE"];
    if (boolVal && _hopeCheckbox)
        [_hopeCheckbox setState:[boolVal boolValue] ? NSControlStateValueOn : NSControlStateValueOff];
    boolVal = [dict objectForKey:@"HOPELegacyMode"];
    if (boolVal && _hopeLegacyCheckbox)
        [_hopeLegacyCheckbox setState:[boolVal boolValue] ? NSControlStateValueOn : NSControlStateValueOff];
    val = [dict objectForKey:@"HOPERequiredPrefix"];
    if (val && _hopePrefixField) [_hopePrefixField setStringValue:val];
    boolVal = [dict objectForKey:@"E2ERequireTLS"];
    if (boolVal && _hopeRequireTLSCheckbox)
        [_hopeRequireTLSCheckbox setState:[boolVal boolValue] ? NSControlStateValueOn : NSControlStateValueOff];
    val = [dict objectForKey:@"HOPECipherPolicy"];
    if (val && _hopeCipherPolicyPopup) {
        if ([val isEqualToString:@"require-aead"]) [_hopeCipherPolicyPopup selectItemAtIndex:1];
        else if ([val isEqualToString:@"rc4-only"]) [_hopeCipherPolicyPopup selectItemAtIndex:2];
        else [_hopeCipherPolicyPopup selectItemAtIndex:0]; /* prefer-aead */
    }
    boolVal = [dict objectForKey:@"E2ERequireAEAD"];
    if (boolVal && _hopeRequireAEADCheckbox)
        [_hopeRequireAEADCheckbox setState:[boolVal boolValue] ? NSControlStateValueOn : NSControlStateValueOff];

    /* TLS (string fields loaded here; port loaded below after numVal decl) */
    val = [dict objectForKey:@"TLSCertFile"];
    if (val && _tlsCertField) [_tlsCertField setStringValue:val];
    val = [dict objectForKey:@"TLSKeyFile"];
    if (val && _tlsKeyField) [_tlsKeyField setStringValue:val];

    NSArray *trackers = [dict objectForKey:@"Trackers"];
    if (trackers) {
        unsigned i;
        for (i = 0; i < [trackers count]; i++) {
            NSString *item = [trackers objectAtIndex:i];
            if ([item length] > 0 && ![_trackerItems containsObject:item])
                [_trackerItems addObject:item];
        }
    }

    val = [dict objectForKey:@"NewsDateFormat"];
    if (val && _newsDateFormatPopup) {
        int idx = [_newsDateFormatPopup indexOfItemWithTitle:val];
        if (idx < 0 && [val length] > 0) {
            [_newsDateFormatPopup addItemWithTitle:val];
            idx = [_newsDateFormatPopup indexOfItemWithTitle:val];
        }
        if (idx >= 0) [_newsDateFormatPopup selectItemAtIndex:idx];
    }
    val = [dict objectForKey:@"NewsDelimiter"];
    if (val && _newsDelimiterField) [_newsDelimiterField setStringValue:val];

    NSNumber *numVal;
    numVal = [dict objectForKey:@"MaxDownloads"];
    if (numVal && _maxDownloadsField) [_maxDownloadsField setIntValue:[numVal intValue]];
    numVal = [dict objectForKey:@"MaxDownloadsPerClient"];
    if (numVal && _maxDLPerClientField) [_maxDLPerClientField setIntValue:[numVal intValue]];
    numVal = [dict objectForKey:@"MaxConnectionsPerIP"];
    if (numVal && _maxConnPerIPField) [_maxConnPerIPField setIntValue:[numVal intValue]];
    numVal = [dict objectForKey:@"TLSPort"];
    if (numVal && _tlsPortField)
        [_tlsPortField setStringValue:[NSString stringWithFormat:@"%d", [numVal intValue]]];
    if (_tlsEnableCheckbox) {
        int tp = numVal ? [numVal intValue] : 0;
        [_tlsEnableCheckbox setState:(tp > 0) ? NSControlStateValueOn : NSControlStateValueOff];
    }

    /* Encoding popup */
    val = [dict objectForKey:@"Encoding"];
    if (val && _encodingPopup) {
        if ([val isEqualToString:@"utf-8"])
            [_encodingPopup selectItemWithTitle:@"UTF-8"];
        else
            [_encodingPopup selectItemWithTitle:@"Mac Roman"];
    }

    /* Mnemosyne */
    val = [dict objectForKey:@"MnemosyneURL"];
    if (val && _mnemosyneURLField) [_mnemosyneURLField setStringValue:val];
    val = [dict objectForKey:@"MnemosyneAPIKey"];
    if (val && _mnemosyneAPIKeyField) [_mnemosyneAPIKeyField setStringValue:val];

    boolVal = [dict objectForKey:@"MnemosyneIndexFiles"];
    if (_mnemosyneIndexFilesCheckbox)
        [_mnemosyneIndexFilesCheckbox setState:
            (boolVal ? [boolVal boolValue] : YES) ? NSControlStateValueOn : NSControlStateValueOff];
    boolVal = [dict objectForKey:@"MnemosyneIndexNews"];
    if (_mnemosyneIndexNewsCheckbox)
        [_mnemosyneIndexNewsCheckbox setState:
            (boolVal ? [boolVal boolValue] : YES) ? NSControlStateValueOn : NSControlStateValueOff];
    boolVal = [dict objectForKey:@"MnemosyneIndexMsgboard"];
    if (_mnemosyneIndexMsgboardCheckbox)
        [_mnemosyneIndexMsgboardCheckbox setState:
            (boolVal ? [boolVal boolValue] : YES) ? NSControlStateValueOn : NSControlStateValueOff];

    /* Set enable state based on whether URL is non-empty */
    {
        NSString *mnURL = [dict objectForKey:@"MnemosyneURL"];
        BOOL mnEnabled = (mnURL && [mnURL length] > 0);
        if (_mnemosyneEnableCheckbox) {
            [_mnemosyneEnableCheckbox setState:
                mnEnabled ? NSControlStateValueOn : NSControlStateValueOff];
            [self toggleMnemosyneEnable:nil];
        }
    }

    /* Chat History */
    boolVal = [dict objectForKey:@"ChatHistoryEnabled"];
    if (_chatHistoryEnableCheckbox)
        [_chatHistoryEnableCheckbox setState:
            (boolVal && [boolVal boolValue]) ? NSControlStateValueOn : NSControlStateValueOff];
    val = [dict objectForKey:@"ChatHistoryMaxMessages"];
    if (val && _chatHistoryMaxMsgsField)
        [_chatHistoryMaxMsgsField setIntValue:[val intValue]];
    val = [dict objectForKey:@"ChatHistoryMaxDays"];
    if (val && _chatHistoryMaxDaysField)
        [_chatHistoryMaxDaysField setIntValue:[val intValue]];
    boolVal = [dict objectForKey:@"ChatHistoryLegacyBroadcast"];
    if (_chatHistoryLegacyBroadcastCheckbox)
        [_chatHistoryLegacyBroadcastCheckbox setState:
            (boolVal && [boolVal boolValue]) ? NSControlStateValueOn : NSControlStateValueOff];
    val = [dict objectForKey:@"ChatHistoryLegacyCount"];
    if (val && _chatHistoryLegacyCountField)
        [_chatHistoryLegacyCountField setIntValue:[val intValue]];
    boolVal = [dict objectForKey:@"ChatHistoryLogJoins"];
    if (_chatHistoryLogJoinsCheckbox)
        [_chatHistoryLogJoinsCheckbox setState:
            (boolVal && [boolVal boolValue]) ? NSControlStateValueOn : NSControlStateValueOff];

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
    BOOL enableBonjour = _bonjourCheckbox ? ([_bonjourCheckbox state] == NSControlStateValueOn) : YES;
    BOOL enableTracker = _trackerCheckbox ? ([_trackerCheckbox state] == NSControlStateValueOn) : NO;
    NSString *newsDateFormat = _newsDateFormatPopup ?
        [_newsDateFormatPopup titleOfSelectedItem] : @"Jan02 15:04";
    NSString *newsDelimiter = _newsDelimiterField ?
        [_newsDelimiterField stringValue] : @"__________________________________________________________";
    if (!newsDateFormat || [newsDateFormat length] == 0)
        newsDateFormat = @"Jan02 15:04";
    if (!newsDelimiter || [newsDelimiter length] == 0)
        newsDelimiter = @"__________________________________________________________";
    BOOL preserveForks = _preserveForkCheckbox ? ([_preserveForkCheckbox state] == NSControlStateValueOn) : NO;
    BOOL enableHOPE = _hopeCheckbox ? ([_hopeCheckbox state] == NSControlStateValueOn) : NO;
    BOOL hopeLegacy = _hopeLegacyCheckbox ? ([_hopeLegacyCheckbox state] == NSControlStateValueOn) : NO;
    NSString *hopePrefix = _hopePrefixField ? [_hopePrefixField stringValue] : @"[E2E]";
    BOOL e2eRequireTLS = _hopeRequireTLSCheckbox ? ([_hopeRequireTLSCheckbox state] == NSControlStateValueOn) : NO;
    NSInteger cipherPolicyIdx = _hopeCipherPolicyPopup ? [_hopeCipherPolicyPopup indexOfSelectedItem] : 0;
    NSString *cipherPolicy = (cipherPolicyIdx == 1) ? @"require-aead" : (cipherPolicyIdx == 2) ? @"rc4-only" : @"prefer-aead";
    BOOL e2eRequireAEAD = _hopeRequireAEADCheckbox ? ([_hopeRequireAEADCheckbox state] == NSControlStateValueOn) : NO;
    BOOL tlsEnabled = _tlsEnableCheckbox ? ([_tlsEnableCheckbox state] == NSControlStateValueOn) : NO;
    NSString *tlsCert = _tlsCertField ? [_tlsCertField stringValue] : @"";
    NSString *tlsKey = _tlsKeyField ? [_tlsKeyField stringValue] : @"";
    int tlsPort = tlsEnabled ? (_tlsPortField ? [_tlsPortField intValue] : 5600) : 0;
    int maxDownloads = _maxDownloadsField ? [_maxDownloadsField intValue] : 0;
    int maxDLPerClient = _maxDLPerClientField ? [_maxDLPerClientField intValue] : 0;
    int maxConnPerIP = _maxConnPerIPField ? [_maxConnPerIPField intValue] : 0;

    if (maxDownloads < 0) maxDownloads = 0;
    if (maxDLPerClient < 0) maxDLPerClient = 0;
    if (maxConnPerIP < 0) maxConnPerIP = 0;

    /* Default banner to config dir copy if field is empty */
    if (!banner || [banner length] == 0) {
        banner = [_configDir stringByAppendingPathComponent:@"banner.jpg"];
    }

    /* Build plist dictionary while preserving unmanaged extension keys. */
    NSMutableDictionary *dict = [NSMutableDictionary dictionary];
    {
        NSString *existingPath = [NSHomeDirectory()
            stringByAppendingPathComponent:@"Library/Preferences/com.lemoniscate.server.plist"];
        NSDictionary *existing = [NSDictionary dictionaryWithContentsOfFile:existingPath];
        if (existing) [dict addEntriesFromDictionary:existing];
    }
    [dict setObject:name forKey:@"ServerName"];
    [dict setObject:desc forKey:@"ServerDescription"];
    [dict setObject:banner forKey:@"BannerFile"];
    [dict setObject:fileRoot forKey:@"FileRoot"];
    [dict setObject:[NSNumber numberWithBool:enableBonjour] forKey:@"EnableBonjour"];
    [dict setObject:[NSNumber numberWithBool:enableTracker] forKey:@"EnableTrackerRegistration"];
    [dict setObject:[NSNumber numberWithBool:preserveForks] forKey:@"PreserveResourceForks"];
    [dict setObject:[NSNumber numberWithInt:maxDownloads] forKey:@"MaxDownloads"];
    [dict setObject:[NSNumber numberWithInt:maxDLPerClient] forKey:@"MaxDownloadsPerClient"];
    [dict setObject:[NSNumber numberWithInt:maxConnPerIP] forKey:@"MaxConnectionsPerIP"];
    [dict setObject:(_trackerItems ? _trackerItems : [NSArray array]) forKey:@"Trackers"];
    [dict setObject:newsDateFormat forKey:@"NewsDateFormat"];
    [dict setObject:newsDelimiter forKey:@"NewsDelimiter"];
    /* Encoding from popup */
    {
        NSString *encTitle = _encodingPopup ? [_encodingPopup titleOfSelectedItem] : @"Mac Roman";
        NSString *encValue = [encTitle isEqualToString:@"UTF-8"] ? @"utf-8" : @"macintosh";
        [dict setObject:encValue forKey:@"Encoding"];
    }
    [dict setObject:[NSNumber numberWithBool:enableHOPE] forKey:@"EnableHOPE"];
    [dict setObject:[NSNumber numberWithBool:hopeLegacy] forKey:@"HOPELegacyMode"];
    [dict setObject:(hopePrefix ? hopePrefix : @"[E2E]") forKey:@"HOPERequiredPrefix"];
    [dict setObject:[NSNumber numberWithBool:e2eRequireTLS] forKey:@"E2ERequireTLS"];
    [dict setObject:(cipherPolicy ? cipherPolicy : @"prefer-aead") forKey:@"HOPECipherPolicy"];
    [dict setObject:[NSNumber numberWithBool:e2eRequireAEAD] forKey:@"E2ERequireAEAD"];
    [dict setObject:(tlsCert ? tlsCert : @"") forKey:@"TLSCertFile"];
    [dict setObject:(tlsKey ? tlsKey : @"") forKey:@"TLSKeyFile"];
    [dict setObject:[NSNumber numberWithInt:tlsPort] forKey:@"TLSPort"];

    /* Mnemosyne */
    {
        BOOL mnEnabled = _mnemosyneEnableCheckbox &&
            [_mnemosyneEnableCheckbox state] == NSControlStateValueOn;
        NSString *mnURL = mnEnabled && _mnemosyneURLField ?
            [_mnemosyneURLField stringValue] : @"";
        NSString *mnKey = _mnemosyneAPIKeyField ?
            [_mnemosyneAPIKeyField stringValue] : @"";
        BOOL mnFiles = _mnemosyneIndexFilesCheckbox ?
            ([_mnemosyneIndexFilesCheckbox state] == NSControlStateValueOn) : YES;
        BOOL mnNews = _mnemosyneIndexNewsCheckbox ?
            ([_mnemosyneIndexNewsCheckbox state] == NSControlStateValueOn) : YES;
        BOOL mnMsgboard = _mnemosyneIndexMsgboardCheckbox ?
            ([_mnemosyneIndexMsgboardCheckbox state] == NSControlStateValueOn) : YES;

        [dict setObject:(mnURL ? mnURL : @"") forKey:@"MnemosyneURL"];
        [dict setObject:(mnKey ? mnKey : @"") forKey:@"MnemosyneAPIKey"];
        [dict setObject:[NSNumber numberWithBool:mnFiles] forKey:@"MnemosyneIndexFiles"];
        [dict setObject:[NSNumber numberWithBool:mnNews] forKey:@"MnemosyneIndexNews"];
        [dict setObject:[NSNumber numberWithBool:mnMsgboard] forKey:@"MnemosyneIndexMsgboard"];
    }

    /* Chat History */
    {
        BOOL chEnabled = _chatHistoryEnableCheckbox &&
            [_chatHistoryEnableCheckbox state] == NSControlStateValueOn;
        BOOL chLegacy = _chatHistoryLegacyBroadcastCheckbox &&
            [_chatHistoryLegacyBroadcastCheckbox state] == NSControlStateValueOn;
        BOOL chLogJoins = _chatHistoryLogJoinsCheckbox &&
            [_chatHistoryLogJoinsCheckbox state] == NSControlStateValueOn;
        int chMaxMsgs = _chatHistoryMaxMsgsField ? [_chatHistoryMaxMsgsField intValue] : 10000;
        int chMaxDays = _chatHistoryMaxDaysField ? [_chatHistoryMaxDaysField intValue] : 0;
        int chLegacyCnt = _chatHistoryLegacyCountField ? [_chatHistoryLegacyCountField intValue] : 30;

        if (chMaxMsgs < 0) chMaxMsgs = 0;
        if (chMaxDays < 0) chMaxDays = 0;
        if (chLegacyCnt < 0) chLegacyCnt = 0;

        [dict setObject:[NSNumber numberWithBool:chEnabled] forKey:@"ChatHistoryEnabled"];
        [dict setObject:[NSNumber numberWithInt:chMaxMsgs] forKey:@"ChatHistoryMaxMessages"];
        [dict setObject:[NSNumber numberWithInt:chMaxDays] forKey:@"ChatHistoryMaxDays"];
        [dict setObject:[NSNumber numberWithBool:chLegacy] forKey:@"ChatHistoryLegacyBroadcast"];
        [dict setObject:[NSNumber numberWithInt:chLegacyCnt] forKey:@"ChatHistoryLegacyCount"];
        [dict setObject:[NSNumber numberWithBool:chLogJoins] forKey:@"ChatHistoryLogJoins"];
    }

    /* Write to ~/Library/Preferences/com.lemoniscate.server.plist */
    NSString *plistPath = [NSHomeDirectory()
        stringByAppendingPathComponent:@"Library/Preferences/com.lemoniscate.server.plist"];
    [dict writeToFile:plistPath atomically:YES];
}

/* Create directory and all intermediate parents (Tiger compatible) */
static void createDirectoryWithParents(NSFileManager *fm, NSString *path)
{
    BOOL isDir = NO;
    if ([fm fileExistsAtPath:path isDirectory:&isDir] && isDir) return;
    /* Recursively create parent */
    NSString *parent = [path stringByDeletingLastPathComponent];
    if (parent && [parent length] > 1) {
        createDirectoryWithParents(fm, parent);
    }
    [fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:NULL];
}

- (void)ensureConfigScaffolding
{
    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:_configDir isDirectory:&isDir]) {
        createDirectoryWithParents(fm, _configDir);
    }

    NSString *filesDir = [_configDir stringByAppendingPathComponent:@"Files"];
    if (![fm fileExistsAtPath:filesDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:filesDir withIntermediateDirectories:YES attributes:nil error:NULL];
    }

    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    if (![fm fileExistsAtPath:usersDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:usersDir withIntermediateDirectories:YES attributes:nil error:NULL];
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

    /* Copy default banner from app bundle if none exists */
    NSString *bannerDest = [_configDir stringByAppendingPathComponent:@"banner.jpg"];
    if (![fm fileExistsAtPath:bannerDest]) {
        NSString *bundleBanner = [[NSBundle mainBundle]
            pathForResource:@"default-banner" ofType:@"jpg"];
        if (bundleBanner) {
            [fm copyItemAtPath:bundleBanner toPath:bannerDest error:NULL];
        }
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
