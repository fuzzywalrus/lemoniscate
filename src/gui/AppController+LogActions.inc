- (void)clearLogs:(id)sender
{
    (void)sender;
    [_logEntries removeAllObjects];
    [_logTextView setString:@""];
    if (_onlineLogTextView) [_onlineLogTextView setString:@""];
}

- (void)toggleAutoScroll:(id)sender
{
    (void)sender;
    _autoScroll = ([_autoScrollCheckbox state] == NSControlStateValueOn);
}

- (void)logFilterChanged:(id)sender
{
    (void)sender;
    [self refreshLogTextView];
}

- (void)toggleStdoutVisibility:(id)sender
{
    (void)sender;
    _showStdout = ([_showStdoutCheckbox state] == NSControlStateValueOn);
    [self refreshLogTextView];
}

- (void)toggleStderrVisibility:(id)sender
{
    (void)sender;
    _showStderr = ([_showStderrCheckbox state] == NSControlStateValueOn);
    [self refreshLogTextView];
}

- (void)textDidChange:(NSNotification *)note
{
    if ([note object] == _messageBoardTextView) {
        [self setMessageBoardDirty:YES];
    } else if ([note object] == _logFilterField) {
        [self logFilterChanged:nil];
    }
}

/* ===== Tab data helpers ===== */

