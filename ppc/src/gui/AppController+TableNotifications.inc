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
        } else if ([cid isEqualToString:@"online_encrypted"]) {
            NSNumber *enc = [item objectForKey:@"encrypted"];
            return (enc && [enc boolValue]) ? @"Yes" : @"No";
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

/* ===== Tab view delegate ===== */

- (void)refreshNewsTimer:(id)sender
{
    (void)sender;
    [self loadMessageBoardText];
    [self refreshThreadedNews:nil];
}

- (void)tabView:(NSTabView *)tv didSelectTabViewItem:(NSTabViewItem *)item
{
    (void)tv;
    NSString *ident = [item identifier];
    NSTimeInterval interval = _pollingInterval > 0 ? _pollingInterval : 10.0;

    /* Start news polling when News tab is selected */
    if ([ident isEqualToString:@"news"]) {
        [self loadMessageBoardText];
        [self refreshThreadedNews:nil];
        if (!_newsRefreshTimer) {
            _newsRefreshTimer = [[NSTimer
                scheduledTimerWithTimeInterval:interval
                                        target:self
                                      selector:@selector(refreshNewsTimer:)
                                      userInfo:nil
                                       repeats:YES] retain];
        }
    } else {
        /* Stop news polling when leaving News tab */
        if (_newsRefreshTimer) {
            [_newsRefreshTimer invalidate];
            [_newsRefreshTimer release];
            _newsRefreshTimer = nil;
        }
    }
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

/* Bold "Yes" in the encrypted column of online users */
- (void)tableView:(NSTableView *)tableView willDisplayCell:(id)cell
   forTableColumn:(NSTableColumn *)tableColumn row:(int)row
{
    if (tableView == _onlineTableView &&
        [[tableColumn identifier] isEqualToString:@"online_encrypted"]) {
        if (row >= 0 && row < (int)[_onlineUsersItems count]) {
            NSDictionary *item = [_onlineUsersItems objectAtIndex:(unsigned)row];
            NSNumber *enc = [item objectForKey:@"encrypted"];
            if (enc && [enc boolValue]) {
                [cell setFont:[NSFont boldSystemFontOfSize:11.0]];
            } else {
                [cell setFont:[NSFont systemFontOfSize:11.0]];
            }
        }
    }
}

