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

    NSView *leftPanel = [self createSettingsPanel]; NSView *rightPanel = [self createRightPanel]; NSRect svBounds = [_splitView bounds]; float divider = (float)LEFT_PANEL_WIDTH; [leftPanel setFrame:NSMakeRect(0, 0, divider, svBounds.size.height)]; [rightPanel setFrame:NSMakeRect(divider + 1, 0, svBounds.size.width - divider - 1, svBounds.size.height)]; [_splitView addSubview:leftPanel];
    [_splitView addSubview:rightPanel];

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
        float boxH = 7 * ROW_HEIGHT + 34;
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
        float boxH = 2 * ROW_HEIGHT + 30;
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
        float boxH = 170;
        NSBox *box = makeSection(@"Tracker Registration", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;
        float listX = LABEL_WIDTH - 4;
        float listW = fieldWidth - ROW_RIGHT_PAD - 12;
        float entryW = listW - 158;
        if (entryW < 180) entryW = 180;

        _trackerCheckbox = [[NSButton alloc]
            initWithFrame:NSZeroRect];
        [_trackerCheckbox setButtonType:NSSwitchButton];
        [_trackerCheckbox setTitle:@"Enable Tracker Registration"];
        [_trackerCheckbox setState:NSOffState];
        iy = addCheckbox(c, _trackerCheckbox, iy);

        NSScrollView *tsv = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(listX, iy + 2, listW, 78)];
        [tsv setHasVerticalScroller:YES];
        [tsv setBorderType:NSBezelBorder];
        _trackerTableView = [[NSTableView alloc]
            initWithFrame:NSMakeRect(0, 0, listW, 78)];
        NSTableColumn *tcol = [[NSTableColumn alloc] initWithIdentifier:@"tracker"];
        [[tcol headerCell] setStringValue:@"Trackers"];
        [tcol setWidth:listW - 20];
        [_trackerTableView addTableColumn:tcol];
        [tcol release];
        [_trackerTableView setDataSource:(id)self];
        [_trackerTableView setDelegate:(id)self];
        [_trackerTableView setAllowsMultipleSelection:NO];
        [tsv setDocumentView:_trackerTableView];
        [c addSubview:tsv];
        [tsv release];

        _newTrackerField = makeEditField(entryW);
        [_newTrackerField setFrame:NSMakeRect(listX, iy + 84, entryW, 22)];
        [c addSubview:_newTrackerField];

        _addTrackerButton = makeButton(@"Add", self, @selector(addTracker:));
        [_addTrackerButton setFrame:NSMakeRect(listX + entryW + 8, iy + 84, 62, 24)];
        [c addSubview:_addTrackerButton];

        _removeTrackerButton = makeButton(@"Remove", self, @selector(removeTracker:));
        [_removeTrackerButton setFrame:NSMakeRect(listX + entryW + 76, iy + 84, 82, 24)];
        [_removeTrackerButton setEnabled:NO];
        [c addSubview:_removeTrackerButton];

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Files ===== */
    {
        float boxH = 170;
        NSBox *box = makeSection(@"Files", SECTION_MARGIN, y,
                                  secWidth, boxH);
        NSView *c = [box contentView];
        float iy = 4;
        float listX = LABEL_WIDTH - 4;
        float listW = fieldWidth - ROW_RIGHT_PAD - 12;
        float entryW = listW - 158;
        if (entryW < 180) entryW = 180;

        _preserveForkCheckbox = [[NSButton alloc]
            initWithFrame:NSZeroRect];
        [_preserveForkCheckbox setButtonType:NSSwitchButton];
        [_preserveForkCheckbox setTitle:@"Preserve Resource Forks"];
        [_preserveForkCheckbox setState:NSOffState];
        iy = addCheckbox(c, _preserveForkCheckbox, iy);

        NSScrollView *isv = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(listX, iy + 2, listW, 78)];
        [isv setHasVerticalScroller:YES];
        [isv setBorderType:NSBezelBorder];
        _ignoreFilesTableView = [[NSTableView alloc]
            initWithFrame:NSMakeRect(0, 0, listW, 78)];
        NSTableColumn *icol = [[NSTableColumn alloc] initWithIdentifier:@"ignore_pattern"];
        [[icol headerCell] setStringValue:@"Ignore Patterns"];
        [icol setWidth:listW - 20];
        [_ignoreFilesTableView addTableColumn:icol];
        [icol release];
        [_ignoreFilesTableView setDataSource:(id)self];
        [_ignoreFilesTableView setDelegate:(id)self];
        [_ignoreFilesTableView setAllowsMultipleSelection:NO];
        [isv setDocumentView:_ignoreFilesTableView];
        [c addSubview:isv];
        [isv release];

        _newIgnoreFileField = makeEditField(entryW);
        [_newIgnoreFileField setFrame:NSMakeRect(listX, iy + 84, entryW, 22)];
        [c addSubview:_newIgnoreFileField];

        _addIgnoreFileButton = makeButton(@"Add", self, @selector(addIgnoreFilePattern:));
        [_addIgnoreFileButton setFrame:NSMakeRect(listX + entryW + 8, iy + 84, 62, 24)];
        [c addSubview:_addIgnoreFileButton];

        _removeIgnoreFileButton = makeButton(@"Remove", self, @selector(removeIgnoreFilePattern:));
        [_removeIgnoreFileButton setFrame:NSMakeRect(listX + entryW + 76, iy + 84, 82, 24)];
        [_removeIgnoreFileButton setEnabled:NO];
        [c addSubview:_removeIgnoreFileButton];

        [doc addSubview:box];
        [box release];
        y += boxH + 8;
    }

    /* ===== Limits ===== */
    {
        float boxH = 2 * ROW_HEIGHT + 34;
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
        float boxH = 3 * ROW_HEIGHT + 42;
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
    [_tabView setDelegate:(id)self];

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

    [usersSV setFrame:NSMakeRect(0, 190, 622, 364)];
    [_onlineLogScrollView setFrame:NSMakeRect(0, 0, 622, 186)];
    [_onlineSplitView addSubview:usersSV];
    [_onlineSplitView addSubview:_onlineLogScrollView];
    [usersSV release];
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
    [_saveMessageBoardButton setFrame:NSMakeRect(460, 5, 164, 24)];
    [_saveMessageBoardButton setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
    [_newsMessageBoardView addSubview:_saveMessageBoardButton];

    /* Refresh button */
    NSButton *refreshBoardBtn = makeButton(@"Refresh", self, @selector(loadMessageBoardText));
    [refreshBoardBtn setFrame:NSMakeRect(388, 5, 68, 24)];
    [refreshBoardBtn setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
    [_newsMessageBoardView addSubview:refreshBoardBtn];

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

    NSButton *refreshNewsBtn = makeButton(@"Refresh", self, @selector(refreshThreadedNews:));
    [refreshNewsBtn setFrame:NSMakeRect(548, 530, 76, 24)];
    [refreshNewsBtn setAutoresizingMask:NSViewMinXMargin];
    [_newsThreadedView addSubview:refreshNewsBtn];
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
