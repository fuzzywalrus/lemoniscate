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

