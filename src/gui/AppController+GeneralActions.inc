- (void)startServer:(id)sender
{
    (void)sender;

    /* Pre-flight validation with clear error messages */
    if (![_processManager hasBinary]) {
        { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Cannot Start Server"]; [a setInformativeText:@"The server binary was not found inside the application bundle. Please reinstall Lemoniscate."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
        return;
    }

    if (_serverPort < 1 || _serverPort > 65535) {
        { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Invalid Port"]; [a setInformativeText:@"The Hotline port must be between 1 and 65535. The default is 5500."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
        return;
    }

    NSString *fileRoot = _fileRootField ? [_fileRootField stringValue] : @"";
    if ([fileRoot length] == 0) {
        { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"No File Root Set"]; [a setInformativeText:@"Please set a File Root directory in the server settings. This is the folder that will be shared with connected users."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
        return;
    }

    BOOL isDir = NO;
    if (![[NSFileManager defaultManager] fileExistsAtPath:fileRoot isDirectory:&isDir] || !isDir) {
        { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"File Root Not Found"]; [a setInformativeText:[NSString stringWithFormat:@"The File Root directory does not exist:\n\n%@\n\nPlease create it or choose a different folder.", fileRoot]]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
        return;
    }

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
    if ([panel runModal] == NSModalResponseOK) {
        NSString *path = [[[panel URLs] objectAtIndex:0] path];
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
    if ([panel runModal] == NSModalResponseOK) {
        [_fileRootField setStringValue:
            [[[panel URLs] objectAtIndex:0] path]];
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
    if ([panel runModal] == NSModalResponseOK) {
        [_bannerFileField setStringValue:
            [[[panel URLs] objectAtIndex:0] path]];
    }
}

- (void)chooseWizardFileRoot:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select files directory"];
    if ([panel runModal] == NSModalResponseOK) {
        [_wizardFileRootField setStringValue:
            [[[panel URLs] objectAtIndex:0] path]];
    }
}

- (void)chooseWizardBannerFile:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select banner image (JPG or GIF)"];
    if ([panel runModal] == NSModalResponseOK) {
        [_wizardBannerField setStringValue:
            [[[panel URLs] objectAtIndex:0] path]];
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
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable)
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
        [_wizardBonjourCheckbox setButtonType:NSButtonTypeSwitch];
        [_wizardBonjourCheckbox setTitle:@"Enable Bonjour"];

        _wizardTrackerCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_wizardTrackerCheckbox setButtonType:NSButtonTypeSwitch];
        [_wizardTrackerCheckbox setTitle:@"Enable Tracker Registration"];

        _wizardPreserveForkCheckbox = [[NSButton alloc] initWithFrame:NSZeroRect];
        [_wizardPreserveForkCheckbox setButtonType:NSButtonTypeSwitch];
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
        (_bonjourCheckbox && [_bonjourCheckbox state] == NSControlStateValueOff) ? NSControlStateValueOff : NSControlStateValueOn];
    [_wizardTrackerCheckbox setState:
        (_trackerCheckbox && [_trackerCheckbox state] == NSControlStateValueOn) ? NSControlStateValueOn : NSControlStateValueOff];
    [_wizardPreserveForkCheckbox setState:
        (_preserveForkCheckbox && [_preserveForkCheckbox state] == NSControlStateValueOn) ? NSControlStateValueOn : NSControlStateValueOff];
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
        [nameLabel setAlignment:NSTextAlignmentRight];
        [nameLabel setFrame:NSMakeRect(10, 224, 110, 20)];
        [_wizardStepContainer addSubview:nameLabel];
        [nameLabel release];
        [_wizardNameField setFrame:NSMakeRect(126, 222, 436, 22)];
        [_wizardStepContainer addSubview:_wizardNameField];

        NSTextField *descLabel = makeLabel(@"Description:", 11.0, NO);
        [descLabel setAlignment:NSTextAlignmentRight];
        [descLabel setFrame:NSMakeRect(10, 190, 110, 20)];
        [_wizardStepContainer addSubview:descLabel];
        [descLabel release];
        [_wizardDescriptionField setFrame:NSMakeRect(126, 188, 436, 22)];
        [_wizardStepContainer addSubview:_wizardDescriptionField];

        NSTextField *portLabel = makeLabel(@"Hotline Port:", 11.0, NO);
        [portLabel setAlignment:NSTextAlignmentRight];
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
        [rootLabel setAlignment:NSTextAlignmentRight];
        [rootLabel setFrame:NSMakeRect(10, 224, 110, 20)];
        [_wizardStepContainer addSubview:rootLabel];
        [rootLabel release];
        [_wizardFileRootField setFrame:NSMakeRect(126, 222, 348, 22)];
        [_wizardStepContainer addSubview:_wizardFileRootField];
        NSButton *rootBrowse = makeButton(@"Browse...", self,
                                          @selector(chooseWizardFileRoot:));
        [rootBrowse setFrame:NSMakeRect(482, 220, 80, 24)];
        [_wizardStepContainer addSubview:rootBrowse];
        [rootBrowse release];

        NSTextField *bannerLabel = makeLabel(@"Banner File:", 11.0, NO);
        [bannerLabel setAlignment:NSTextAlignmentRight];
        [bannerLabel setFrame:NSMakeRect(10, 190, 110, 20)];
        [_wizardStepContainer addSubview:bannerLabel];
        [bannerLabel release];
        [_wizardBannerField setFrame:NSMakeRect(126, 188, 348, 22)];
        [_wizardStepContainer addSubview:_wizardBannerField];
        NSButton *bannerBrowse = makeButton(@"Browse...", self,
                                            @selector(chooseWizardBannerFile:));
        [bannerBrowse setFrame:NSMakeRect(482, 186, 80, 24)];
        [_wizardStepContainer addSubview:bannerBrowse];
        [bannerBrowse release];

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
        [maxDL setAlignment:NSTextAlignmentRight];
        [maxDL setFrame:NSMakeRect(10, 190, 110, 20)];
        [_wizardStepContainer addSubview:maxDL];
        [maxDL release];
        [_wizardMaxDownloadsField setFrame:NSMakeRect(126, 188, 86, 22)];
        [_wizardStepContainer addSubview:_wizardMaxDownloadsField];

        NSTextField *maxPer = makeLabel(@"Max DL/Client:", 11.0, NO);
        [maxPer setAlignment:NSTextAlignmentRight];
        [maxPer setFrame:NSMakeRect(10, 156, 110, 20)];
        [_wizardStepContainer addSubview:maxPer];
        [maxPer release];
        [_wizardMaxDLPerClientField setFrame:NSMakeRect(126, 154, 86, 22)];
        [_wizardStepContainer addSubview:_wizardMaxDLPerClientField];

        NSTextField *maxIP = makeLabel(@"Max Conn/IP:", 11.0, NO);
        [maxIP setAlignment:NSTextAlignmentRight];
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
            ([_wizardBonjourCheckbox state] == NSControlStateValueOn ? @"Enabled" : @"Disabled"),
            ([_wizardTrackerCheckbox state] == NSControlStateValueOn ? @"Enabled" : @"Disabled"),
            ([_wizardPreserveForkCheckbox state] == NSControlStateValueOn ? @"Enabled" : @"Disabled"),
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
                { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Missing Server Name"]; [a setInformativeText:@"Enter a server name before continuing."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
            }
            return NO;
        }

        int port = [_wizardPortField intValue];
        if (port < 1 || port > 65535) {
            if (showAlert) {
                { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Invalid Port"]; [a setInformativeText:@"Hotline port must be between 1 and 65535."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
            }
            return NO;
        }
    } else if (_wizardStepIndex == 1) {
        NSString *root = trimmedString([_wizardFileRootField stringValue]);
        if ([root length] == 0) {
            if (showAlert) {
                { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Missing File Root"]; [a setInformativeText:@"Enter a file root path before continuing."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
            }
            return NO;
        }
    } else if (_wizardStepIndex == 2) {
        if ([_wizardMaxDownloadsField intValue] < 0 ||
            [_wizardMaxDLPerClientField intValue] < 0 ||
            [_wizardMaxConnPerIPField intValue] < 0) {
            if (showAlert) {
                { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Invalid Limits"]; [a setInformativeText:@"Limit values cannot be negative."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
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
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable)
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
        { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Invalid URL"]; [a setInformativeText:@"Could not open requested link."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
        return;
    }

    if (![[NSWorkspace sharedWorkspace] openURL:url]) {
        { NSAlert *a = [[NSAlert alloc] init]; [a setMessageText:@"Open Failed"]; [a setInformativeText:@"Unable to open link in the default browser."]; [a addButtonWithTitle:@"OK"]; [a runModal]; [a release]; }
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
