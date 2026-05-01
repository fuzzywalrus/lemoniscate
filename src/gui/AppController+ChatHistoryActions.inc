/*
 * AppController+ChatHistoryActions.inc — actions for the Chat History
 * settings section: master-toggle gating, key-file picker, one-click
 * key generation, Advanced disclosure fold.
 *
 * Method bodies are inlined into AppController's @implementation via
 * #import in AppController.m, matching the convention of the other
 * +Actions.inc files.
 */

/* Single source of truth for which chat-history widgets are gated by the
 * master Enable checkbox. Mirrors the HOPE pattern: visible-but-disabled
 * when the feature is off so operators can still see the available knobs. */
- (void)updateChatHistoryWidgetEnablement
{
    BOOL on = (_chatHistoryEnableCheckbox &&
               [_chatHistoryEnableCheckbox state] == NSControlStateValueOn);

    [_chatHistoryMaxMsgsField              setEnabled:on];
    [_chatHistoryMaxDaysField              setEnabled:on];
    [_chatHistoryLegacyBroadcastCheckbox   setEnabled:on];
    [_chatHistoryLegacyCountField          setEnabled:on];
    [_chatHistoryLogJoinsCheckbox          setEnabled:on];
    [_chatHistoryKeyPathField              setEnabled:on];
    [_chooseChatHistoryKeyButton           setEnabled:on];
    [_generateChatHistoryKeyButton         setEnabled:on];
    [_chatHistoryAdvancedDisclosure        setEnabled:on];
    [_chatHistoryRateCapacityField         setEnabled:on];
    [_chatHistoryRateRefillField           setEnabled:on];
}

- (void)chatHistoryToggled:(id)sender
{
    (void)sender;
    [self updateChatHistoryWidgetEnablement];
    [self saveSettings:nil];
}

- (void)chooseChatHistoryKey:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select Chat History Encryption Key"];
    if ([panel runModal] == NSModalResponseOK) {
        NSString *path = [[[panel URLs] objectAtIndex:0] path];
        [_chatHistoryKeyPathField setStringValue:path];
        [self saveSettings:nil];
    }
}

/* Write 32 random bytes to <configdir>/chat_history.key with 0600 perms.
 * If the path already has a key, refuse to overwrite without confirmation —
 * an overwrite would silently render every existing encrypted message
 * unreadable. */
- (void)generateChatHistoryKey:(id)sender
{
    (void)sender;
    NSString *defaultPath = [_configDir
        stringByAppendingPathComponent:@"chat_history.key"];

    NSFileManager *fm = [NSFileManager defaultManager];
    if ([fm fileExistsAtPath:defaultPath]) {
        NSAlert *a = [[NSAlert alloc] init];
        [a setMessageText:@"Key file already exists"];
        [a setInformativeText:@"A key file already exists at this path. "
            @"Generating a new key will make all existing encrypted "
            @"messages unreadable. Choose a different filename, or "
            @"cancel and remove the existing key first."];
        [a addButtonWithTitle:@"Cancel"];
        [a addButtonWithTitle:@"Choose Different Filename\u2026"];
        NSModalResponse r = [a runModal];
        [a release];
        if (r == NSAlertSecondButtonReturn) {
            NSSavePanel *sp = [NSSavePanel savePanel];
            [sp setNameFieldStringValue:@"chat_history.key"];
            [sp setDirectoryURL:[NSURL fileURLWithPath:_configDir isDirectory:YES]];
            if ([sp runModal] != NSModalResponseOK) return;
            defaultPath = [[sp URL] path];
            if ([fm fileExistsAtPath:defaultPath]) {
                NSAlert *aa = [[NSAlert alloc] init];
                [aa setMessageText:@"That file also exists"];
                [aa setInformativeText:@"Pick a path that does not exist yet, "
                    @"or remove the existing file first."];
                [aa addButtonWithTitle:@"OK"];
                [aa runModal];
                [aa release];
                return;
            }
        } else {
            return;
        }
    }

    uint8_t key[32];
    arc4random_buf(key, sizeof(key));
    NSData *data = [NSData dataWithBytes:key length:sizeof(key)];
    memset(key, 0, sizeof(key));

    NSError *err = nil;
    if (![data writeToFile:defaultPath options:NSDataWritingAtomic error:&err]) {
        NSAlert *a = [[NSAlert alloc] init];
        [a setMessageText:@"Failed to write key file"];
        [a setInformativeText:[err localizedDescription] ?: @"Unknown error."];
        [a addButtonWithTitle:@"OK"];
        [a runModal];
        [a release];
        return;
    }
    NSDictionary *attrs = [NSDictionary dictionaryWithObject:
                          [NSNumber numberWithUnsignedLong:0600]
                          forKey:NSFilePosixPermissions];
    [fm setAttributes:attrs ofItemAtPath:defaultPath error:NULL];

    [_chatHistoryKeyPathField setStringValue:defaultPath];
    [self saveSettings:nil];
}

- (void)toggleChatHistoryAdvanced:(id)sender
{
    (void)sender;
    if (!_chatHistoryAdvancedView) return;
    BOOL hidden = [_chatHistoryAdvancedView isHidden];
    [_chatHistoryAdvancedView setHidden:!hidden];
    [_chatHistoryAdvancedDisclosure setTitle:
        hidden ? @"Advanced \u25BC" : @"Advanced \u25B6"];
}
