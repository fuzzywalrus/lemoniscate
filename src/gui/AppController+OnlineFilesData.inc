- (void)processOnlineLogLine:(NSString *)text
{
    if (!text || [text length] == 0) return;

    if ([text rangeOfString:@"Server started"].location != NSNotFound) {
        [_onlineUsersByID removeAllObjects];
        _onlinePeakConnections = 0;
        _footerDownloadBytes = 0;
        _footerUploadBytes = 0;
        [self updateOnlineUI];
        return;
    }
    if ([text rangeOfString:@"Server shutting down"].location != NSNotFound) {
        [_onlineUsersByID removeAllObjects];
        [self updateOnlineUI];
        return;
    }

    unsigned long long bytes = parseLogByteCount(text);
    if (bytes > 0) {
        if ([text hasPrefix:@"File sent:"] || [text hasPrefix:@"Banner sent"]) {
            _footerDownloadBytes += bytes;
            [self updateFooterStats];
        } else if ([text hasPrefix:@"File received:"] ||
                   [text hasPrefix:@"Folder upload: received"]) {
            _footerUploadBytes += bytes;
            [self updateFooterStats];
        }
    }

    NSRange loginRange = [text rangeOfString:@"Login: "];
    if (loginRange.location != NSNotFound) {
        NSUInteger remoteStart = loginRange.location + [@"Login: " length];
        NSRange asRange = [text rangeOfString:@" as \""
                                      options:0
                                        range:NSMakeRange(remoteStart,
                                            [text length] - remoteStart)];
        if (asRange.location != NSNotFound) {
            NSString *remote = [text substringWithRange:
                NSMakeRange(remoteStart, asRange.location - remoteStart)];

            NSUInteger nameStart = asRange.location + [@" as \"" length];
            NSRange nameEnd = [text rangeOfString:@"\" (id="
                                             options:0
                                               range:NSMakeRange(nameStart,
                                                   [text length] - nameStart)];
            if (nameEnd.location != NSNotFound) {
                NSString *login = [text substringWithRange:
                    NSMakeRange(nameStart, nameEnd.location - nameStart)];
                NSUInteger idStart = nameEnd.location + [@"\" (id=" length];
                NSRange idEnd = [text rangeOfString:@")"
                                           options:0
                                             range:NSMakeRange(idStart,
                                                 [text length] - idStart)];
                if (idEnd.location != NSNotFound) {
                    NSString *idStr = [text substringWithRange:
                        NSMakeRange(idStart, idEnd.location - idStart)];
                    int userID = [idStr intValue];
                    if (userID > 0) {
                        NSString *ip = remote;
                        NSRange portSep = [remote rangeOfString:@":"
                                                         options:NSBackwardsSearch];
                        if (portSep.location != NSNotFound) {
                            ip = [remote substringToIndex:portSep.location];
                        }

                        NSMutableDictionary *user =
                            [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                [NSNumber numberWithInt:userID], @"id",
                                (login ? login : @""), @"login",
                                (ip ? ip : @""), @"ip",
                                [NSDate date], @"lastSeen",
                                nil];
                        [_onlineUsersByID setObject:user
                                            forKey:[NSString stringWithFormat:@"%d", userID]];
                        if ((int)[_onlineUsersByID count] > _onlinePeakConnections) {
                            _onlinePeakConnections = (int)[_onlineUsersByID count];
                        }
                        [self updateOnlineUI];
                        return;
                    }
                }
            }
        }
    }

    NSRange disRange = [text rangeOfString:@"Client disconnected: "];
    if (disRange.location != NSNotFound) {
        NSUInteger idStart = NSNotFound;
        NSRange idMarker = [text rangeOfString:@"(id="];
        if (idMarker.location != NSNotFound) {
            idStart = idMarker.location + [@"(id=" length];
        }
        if (idStart != NSNotFound) {
            NSRange idEnd = [text rangeOfString:@")"
                                       options:0
                                         range:NSMakeRange(idStart,
                                             [text length] - idStart)];
            if (idEnd.location != NSNotFound) {
                NSString *idStr = [text substringWithRange:
                    NSMakeRange(idStart, idEnd.location - idStart)];
                int userID = [idStr intValue];
                if (userID > 0) {
                    [_onlineUsersByID removeObjectForKey:
                        [NSString stringWithFormat:@"%d", userID]];
                    [self updateOnlineUI];
                }
            }
        }
    }
}

- (void)updateOnlineUI
{
    if (!_onlineTableView) {
        [self updateFooterStats];
        return;
    }

    [_onlineUsersItems removeAllObjects];
    NSMutableArray *sorted = [NSMutableArray arrayWithArray:
        [_onlineUsersByID allValues]];
    int i;
    for (i = 1; i < (int)[sorted count]; i++) {
        id item = [sorted objectAtIndex:(unsigned)i];
        int itemID = [[item objectForKey:@"id"] intValue];
        int j = i - 1;
        while (j >= 0) {
            id prev = [sorted objectAtIndex:(unsigned)j];
            int prevID = [[prev objectForKey:@"id"] intValue];
            if (prevID <= itemID) break;
            [sorted replaceObjectAtIndex:(unsigned)(j + 1) withObject:prev];
            j--;
        }
        [sorted replaceObjectAtIndex:(unsigned)(j + 1) withObject:item];
    }
    [_onlineUsersItems addObjectsFromArray:sorted];

    [_onlineTableView reloadData];

    NSString *status = [NSString stringWithFormat:
        @"Connected: %u  Peak: %d",
        (unsigned)[_onlineUsersItems count], _onlinePeakConnections];
    [_onlineStatusLabel setStringValue:status];
    [self updateFooterStats];

    int sel = [_onlineTableView selectedRow];
    [_onlineBanButton setEnabled:(sel >= 0 && sel < (int)[_onlineUsersItems count])];
}

- (NSString *)resolvedFileRootPath
{
    NSString *root = @"";
    if (_fileRootField) root = trimmedString([_fileRootField stringValue]);
    if (!root || [root length] == 0) root = @"Files";
    if ([root hasPrefix:@"/"]) return root;
    return [_configDir stringByAppendingPathComponent:root];
}

- (void)loadFilesAtPath:(NSString *)path
{
    if (!path || [path length] == 0) path = [self resolvedFileRootPath];

    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:path isDirectory:&isDir] || !isDir) {
        [_filesItems removeAllObjects];
        if (_filesTableView) [_filesTableView reloadData];
        if (_filesPathLabel) [_filesPathLabel setStringValue:@"File root not found"];
        return;
    }

    [path retain];
    [_filesCurrentPath release];
    _filesCurrentPath = path;

    if (_filesPathLabel) [_filesPathLabel setStringValue:_filesCurrentPath];

    [_filesItems removeAllObjects];
    NSArray *contents = [fm directoryContentsAtPath:_filesCurrentPath];
    NSArray *sorted = contents ? [contents sortedArrayUsingSelector:
        @selector(caseInsensitiveCompare:)] : [NSArray array];
    NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
    [fmt setDateFormat:@"yyyy-MM-dd HH:mm"];

    unsigned i;
    for (i = 0; i < [sorted count]; i++) {
        NSString *name = [sorted objectAtIndex:i];
        if ([name hasPrefix:@"."]) continue;

        NSString *fullPath = [_filesCurrentPath stringByAppendingPathComponent:name];
        BOOL entryIsDir = NO;
        if (![fm fileExistsAtPath:fullPath isDirectory:&entryIsDir]) continue;

        NSDictionary *attrs = [fm fileAttributesAtPath:fullPath traverseLink:YES];
        NSString *sizeText = entryIsDir ? @"—" : @"";
        NSString *dateText = @"";

        if (!entryIsDir && attrs) {
            NSNumber *sizeNum = [attrs objectForKey:NSFileSize];
            if (sizeNum) sizeText = humanFileSize([sizeNum unsignedLongLongValue]);
        }
        if (attrs) {
            NSDate *modDate = [attrs objectForKey:NSFileModificationDate];
            if (modDate) dateText = [fmt stringFromDate:modDate];
        }

        NSDictionary *item = [NSDictionary dictionaryWithObjectsAndKeys:
            name, @"name",
            fullPath, @"path",
            [NSNumber numberWithBool:entryIsDir], @"isDir",
            sizeText, @"size",
            dateText, @"modified",
            nil];
        [_filesItems addObject:item];
    }
    [fmt release];

    if (_filesTableView) [_filesTableView reloadData];
}

