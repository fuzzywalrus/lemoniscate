- (void)loadAccountsListData
{
    [self ensureConfigScaffolding];
    [_accountsItems removeAllObjects];

    NSString *usersDir = [_configDir stringByAppendingPathComponent:@"Users"];
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray *contents = [fm contentsOfDirectoryAtPath:usersDir error:NULL];
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
            [_accountsTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)idx] byExtendingSelection:NO];
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
    BOOL requireEncryption = NO;
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
            } else if ([key isEqualToString:@"RequireEncryption"]) {
                requireEncryption = ([val isEqualToString:@"true"] || [val isEqualToString:@"1"]);
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
        [NSNumber numberWithBool:requireEncryption], @"requireEncryption",
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

    float contentW = 366.0f;
    if (_accountPermissionsScrollView) {
        NSSize cs = [_accountPermissionsScrollView contentSize];
        if (cs.width > 120.0f) contentW = cs.width;
    }
    float leftPad = 8.0f;
    float rightPad = 8.0f;
    float colGap = 8.0f;
    float colWidth = (contentW - leftPad - rightPad - colGap) / 2.0f;
    if (colWidth < 148.0f) colWidth = 148.0f;

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
            [groupLabel setFrame:NSMakeRect(leftPad, y, contentW - leftPad - rightPad, 16)];
            [_accountPermissionsContentView addSubview:groupLabel];
            [groupLabel release];
            y += 18.0f;
            lastGroup = group;
        }

        NSButton *cb = [[NSButton alloc] initWithFrame:NSMakeRect(
            leftPad + (float)col * (colWidth + colGap), y, colWidth, 18.0f)];
        [cb setButtonType:NSButtonTypeSwitch];
        [cb setTitle:label];
        [cb setFont:[NSFont systemFontOfSize:10.0]];
        [cb setState:[_accountAccessKeys containsObject:key] ? NSControlStateValueOn : NSControlStateValueOff];
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
    [_accountPermissionsContentView setFrame:NSMakeRect(0, 0, contentW, y + 8.0f)];
}

- (void)syncPermissionCheckboxesFromAccess
{
    NSArray *keys = [_accountPermissionCheckboxes allKeys];
    unsigned i;
    for (i = 0; i < [keys count]; i++) {
        NSString *key = [keys objectAtIndex:i];
        NSButton *cb = [_accountPermissionCheckboxes objectForKey:key];
        [cb setState:[_accountAccessKeys containsObject:key] ? NSControlStateValueOn : NSControlStateValueOff];
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

    NSNumber *reqEnc = [acct objectForKey:@"requireEncryption"];
    [_acctRequireEncryptionCheckbox setState:(reqEnc && [reqEnc boolValue]) ? NSControlStateValueOn : NSControlStateValueOff];

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
    [_acctRequireEncryptionCheckbox setState:NSControlStateValueOff];

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
