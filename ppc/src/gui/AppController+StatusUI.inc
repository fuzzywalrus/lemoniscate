- (void)updateFooterStats
{
    if (!_footerConnectedLabel || !_footerPeakLabel ||
        !_footerDLLabel || !_footerULLabel) {
        return;
    }

    NSUInteger connected = [_onlineUsersByID count];
    [_footerConnectedLabel setStringValue:
        [NSString stringWithFormat:@"Connected: %u", (unsigned)connected]];
    [_footerPeakLabel setStringValue:
        [NSString stringWithFormat:@"Peak: %d", _onlinePeakConnections]];
    [_footerDLLabel setStringValue:
        [NSString stringWithFormat:@"DL: %@", humanFileSize(_footerDownloadBytes)]];
    [_footerULLabel setStringValue:
        [NSString stringWithFormat:@"UL: %@", humanFileSize(_footerUploadBytes)]];

    [_footerConnectedLabel sizeToFit];
    [_footerPeakLabel sizeToFit];
    [_footerDLLabel sizeToFit];
    [_footerULLabel sizeToFit];

    NSView *footer = [_footerConnectedLabel superview];
    if (!footer) return;

    float right = [footer bounds].size.width - 10.0f;
    NSRect f;

    f = [_footerULLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerULLabel setFrame:f];
    right = f.origin.x - 14.0f;

    f = [_footerDLLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerDLLabel setFrame:f];
    right = f.origin.x - 14.0f;

    f = [_footerPeakLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerPeakLabel setFrame:f];
    right = f.origin.x - 14.0f;

    f = [_footerConnectedLabel frame];
    f.origin.x = right - f.size.width;
    f.origin.y = 6.0f;
    [_footerConnectedLabel setFrame:f];
}

- (void)updateServerUI
{
    BOOL running = [_processManager isRunning];
    ServerStatus st = [_processManager status];

    if (!running && [_onlineUsersByID count] > 0) {
        [_onlineUsersByID removeAllObjects];
        [self updateOnlineUI];
    }

    switch (st) {
        case ServerStatusStopped:
            [_statusLabel setStringValue:@"Server is stopped"];
            [_portInfoLabel setStringValue:@""];
            break;
        case ServerStatusStarting:
            [_statusLabel setStringValue:@"Server is starting..."];
            break;
        case ServerStatusRunning:
            [_statusLabel setStringValue:@"Server is running"];
            [_portInfoLabel setStringValue:
                [NSString stringWithFormat:@"Hotline port %d", _serverPort]];
            break;
        case ServerStatusError:
            [_statusLabel setStringValue:
                [NSString stringWithFormat:@"Error: %@",
                 [_processManager errorMessage]]];
            [_portInfoLabel setStringValue:@""];
            break;
    }

    NSColor *dotColor;
    switch (st) {
        case ServerStatusRunning:
            dotColor = [NSColor colorWithCalibratedRed:0.2 green:0.8
                                                 blue:0.2 alpha:1.0];
            break;
        case ServerStatusStarting:
            dotColor = [NSColor colorWithCalibratedRed:0.9 green:0.8
                                                 blue:0.0 alpha:1.0];
            break;
        case ServerStatusError:
            dotColor = [NSColor redColor]; break;
        default:
            dotColor = [NSColor grayColor]; break;
    }
    [_statusIndicator setDotColor:dotColor];
    [_footerStatusDot setDotColor:dotColor];

    [_startButton setEnabled:!running];
    [_stopButton setEnabled:running];
    [_restartButton setEnabled:[_processManager hasBinary]];
    if (_reloadButton) [_reloadButton setEnabled:running];

    [_footerStatusLabel setStringValue:[_processManager statusLabel]];
    [_footerStatusLabel sizeToFit];
    if (running) {
        [_footerPortLabel setStringValue:
            [NSString stringWithFormat:@"port %d", _serverPort]];
        NSRect slF = [_footerStatusLabel frame];
        [_footerPortLabel setFrameOrigin:
            NSMakePoint(slF.origin.x + slF.size.width + 8, 6)];
    } else {
        [_footerPortLabel setStringValue:@""];
    }
    [self updateFooterStats];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    SEL action = [item action];
    BOOL running = [_processManager isRunning];
    if (action == @selector(startServer:)) return !running;
    if (action == @selector(stopServer:)) return running;
    if (action == @selector(restartServer:)) return [_processManager hasBinary];
    if (action == @selector(reloadServerConfig:)) return running;
    return YES;
}

