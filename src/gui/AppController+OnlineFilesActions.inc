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

