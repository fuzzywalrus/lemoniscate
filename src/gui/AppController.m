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

@interface AppController ()
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
    [self createMainMenu];
    [self createMainWindow];
    [self loadConfigFromDisk];
    [self updateServerUI];
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

    NSArray *lines = [yaml componentsSeparatedByCharactersInSet:
        [NSCharacterSet newlineCharacterSet]];
    unsigned i;
    for (i = 0; i < [lines count]; i++) {
        NSString *line = trimmedString([lines objectAtIndex:i]);
        if ([line length] == 0 || [line hasPrefix:@"#"]) continue;

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
}

- (void)writeConfigToDisk
{
    NSString *name = _serverNameField ? [_serverNameField stringValue] : _serverName;
    NSString *desc = _descriptionField ? [_descriptionField stringValue] : _serverDescription;
    NSString *banner = _bannerFileField ? [_bannerFileField stringValue] : @"";
    NSString *fileRoot = _fileRootField ? [_fileRootField stringValue] : @"";
    BOOL enableBonjour = _bonjourCheckbox ? ([_bonjourCheckbox state] == NSOnState) : YES;
    BOOL enableTracker = _trackerCheckbox ? ([_trackerCheckbox state] == NSOnState) : NO;
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
        [fm createDirectoryAtPath:_configDir attributes:nil];
    }

    NSString *cfgPath = [_configDir stringByAppendingPathComponent:@"config.yaml"];
    NSString *qName = [name stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qDesc = [desc stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qBanner = [banner stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *qRoot = [fileRoot stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];

    NSString *yaml = [NSString stringWithFormat:
        @"Name: \"%@\"\n"
        @"Description: \"%@\"\n"
        @"BannerFile: \"%@\"\n"
        @"FileRoot: \"%@\"\n"
        @"EnableTrackerRegistration: %@\n"
        @"Trackers: []\n"
        @"EnableBonjour: %@\n"
        @"Encoding: macintosh\n"
        @"MaxDownloads: %d\n"
        @"MaxDownloadsPerClient: %d\n"
        @"MaxConnectionsPerIP: %d\n"
        @"PreserveResourceForks: %@\n",
        qName, qDesc, qBanner, qRoot,
        enableTracker ? @"true" : @"false",
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
        [fm createDirectoryAtPath:_configDir attributes:nil];
    }

    NSString *filesDir = [_configDir stringByAppendingPathComponent:@"Files"];
    if (![fm fileExistsAtPath:filesDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:filesDir attributes:nil];
    }

    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    if (![fm fileExistsAtPath:usersDir isDirectory:&isDir]) {
        [fm createDirectoryAtPath:usersDir attributes:nil];
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
    [appMenu addItemWithTitle:@"About Lemoniscate"
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
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

    NSMenuItem *mi;
    mi = [[NSMenuItem alloc] initWithTitle:@"Start Server"
        action:@selector(startServer:) keyEquivalent:@"r"];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Stop Server"
        action:@selector(stopServer:) keyEquivalent:@"."];
    [mi setTarget:self]; [srvMenu addItem:mi]; [mi release];

    mi = [[NSMenuItem alloc] initWithTitle:@"Restart Server"
        action:@selector(restartServer:) keyEquivalent:@"R"];
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
        float boxH = 5 * ROW_HEIGHT + 20;
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
        float boxH = ROW_HEIGHT + 16;
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

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Files ===== */
    {
        float boxH = ROW_HEIGHT + 16;
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

    [container addSubview:_tabView];
    [self layoutRightPanel];
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

    float startW = [_startButton frame].size.width;
    float stopW = [_stopButton frame].size.width;
    float restartW = [_restartButton frame].size.width;
    if (startW < 72.0f) startW = 72.0f;
    if (stopW < 72.0f) stopW = 72.0f;
    if (restartW < 84.0f) restartW = 84.0f;

    float btnH = 30.0f;
    float totalBtnW = startW + stopW + restartW + 2.0f * btnGap;
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

    _clearLogsButton = makeButton(@"Clear", self, @selector(clearLogs:));
    [_clearLogsButton setFrame:NSMakeRect(550, 3, 70, 24)];
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

- (void)clearLogs:(id)sender
{
    (void)sender;
    [_logTextView setString:@""];
}

- (void)toggleAutoScroll:(id)sender
{
    (void)sender;
    _autoScroll = ([_autoScrollCheckbox state] == NSOnState);
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

    NSString *line = [NSString stringWithFormat:@"%@  %@\n", timeStr, text];

    NSMutableDictionary *attrs = [NSMutableDictionary dictionary];
    [attrs setObject:[NSFont fontWithName:@"Monaco" size:10.0]
              forKey:NSFontAttributeName];
    if ([src intValue] == LogSourceStderr)
        [attrs setObject:[NSColor redColor]
                  forKey:NSForegroundColorAttributeName];

    NSAttributedString *as = [[NSAttributedString alloc]
        initWithString:line attributes:attrs];
    [[_logTextView textStorage] appendAttributedString:as];
    [as release];

    if (_autoScroll)
        [_logTextView scrollRangeToVisible:
            NSMakeRange([[_logTextView string] length], 0)];
}

/* ===== UI update ===== */

- (void)updateServerUI
{
    BOOL running = [_processManager isRunning];
    ServerStatus st = [_processManager status];

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
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    SEL action = [item action];
    BOOL running = [_processManager isRunning];
    if (action == @selector(startServer:)) return !running;
    if (action == @selector(stopServer:)) return running;
    if (action == @selector(restartServer:)) return [_processManager hasBinary];
    return YES;
}

@end
