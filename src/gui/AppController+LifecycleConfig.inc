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

