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

