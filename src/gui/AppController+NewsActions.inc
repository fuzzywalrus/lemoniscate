- (void)newsSegmentChanged:(id)sender
{
    (void)sender;
    BOOL showMessageBoard = ([_newsSegmentedControl selectedSegment] == 0);
    [_newsMessageBoardView setHidden:!showMessageBoard];
    [_newsThreadedView setHidden:showMessageBoard];
}

- (void)saveMessageBoard:(id)sender
{
    (void)sender;
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:@"MessageBoard.txt"];
    NSString *text = [_messageBoardTextView string];
    if (![text writeToFile:path atomically:YES
                  encoding:NSUTF8StringEncoding error:nil]) {
        NSRunAlertPanel(@"Unable to Save", @"Could not save MessageBoard.txt.",
                        @"OK", nil, nil);
        return;
    }
    [self setMessageBoardDirty:NO];
}

- (void)refreshThreadedNews:(id)sender
{
    (void)sender;
    [self loadThreadedNewsCategories];
}

- (void)addNewsCategory:(id)sender
{
    (void)sender;
    NSString *name = trimmedString([_newsNewCategoryField stringValue]);
    if ([name length] == 0) return;

    if ([_newsCategoriesByKey objectForKey:name]) {
        NSRunAlertPanel(@"Category Exists",
                        @"A category named \"%@\" already exists.",
                        @"OK", nil, nil, name);
        return;
    }

    NSMutableDictionary *cat = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        name, @"name",
        @"bundle", @"type",
        [NSMutableDictionary dictionary], @"articles",
        nil];
    [_newsCategoriesByKey setObject:cat forKey:name];
    [_newsNewCategoryField setStringValue:@""];
    [_newsSelectedCategoryKey release];
    _newsSelectedCategoryKey = [name retain];
    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
}

- (void)deleteNewsCategory:(id)sender
{
    (void)sender;
    int row = _newsCategoriesTableView ? [_newsCategoriesTableView selectedRow] : -1;
    if (row < 0 || row >= (int)[_newsCategoryItems count]) return;

    NSString *key = [_newsCategoryItems objectAtIndex:(unsigned)row];
    if (![_newsCategoriesByKey objectForKey:key]) return;

    int rc = NSRunAlertPanel(@"Delete Category",
                             @"Delete \"%@\" and all listed articles?",
                             @"Delete", @"Cancel", nil, key);
    if (rc != NSAlertDefaultReturn) return;

    [_newsCategoriesByKey removeObjectForKey:key];
    [_newsSelectedCategoryKey release];
    _newsSelectedCategoryKey = nil;
    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
}

- (NSString *)nextThreadedNewsArticleIDForCategory:(NSDictionary *)category
{
    NSDictionary *articles = category ? [category objectForKey:@"articles"] : nil;
    NSArray *ids = articles ? [articles allKeys] : [NSArray array];
    int maxID = 0;
    unsigned i;
    for (i = 0; i < [ids count]; i++) {
        int v = [[ids objectAtIndex:i] intValue];
        if (v > maxID) maxID = v;
    }
    if (maxID > 0) {
        return [NSString stringWithFormat:@"%d", maxID + 1];
    }
    return [NSString stringWithFormat:@"%u", (unsigned)[[NSDate date] timeIntervalSince1970]];
}

- (void)openNewsArticleEditorForNew:(BOOL)isNew
{
    if (!_newsSelectedCategoryKey || [_newsSelectedCategoryKey length] == 0) {
        NSRunAlertPanel(@"Select Category",
                        @"Select a category before editing articles.",
                        @"OK", nil, nil);
        return;
    }

    if (_newsArticlesTableView && !isNew) {
        int row = [_newsArticlesTableView selectedRow];
        if (row < 0 || row >= (int)[_newsArticleItems count]) return;
    }

    if (!_newsArticleEditorWindow) {
        _newsArticleEditorWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 640, 430)
                      styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                                 NSMiniaturizableWindowMask | NSResizableWindowMask)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [_newsArticleEditorWindow setReleasedWhenClosed:NO];
        [_newsArticleEditorWindow setDelegate:(id)self];

        NSView *content = [_newsArticleEditorWindow contentView];

        NSTextField *tl = makeLabel(@"Title:", 11.0, NO);
        [tl setAlignment:NSRightTextAlignment];
        [tl setFrame:NSMakeRect(8, 396, 70, 17)];
        [content addSubview:tl];
        [tl release];
        _newsArticleTitleField = makeEditField(548);
        [_newsArticleTitleField setFrame:NSMakeRect(84, 392, 548, 22)];
        [content addSubview:_newsArticleTitleField];

        NSTextField *pl = makeLabel(@"Poster:", 11.0, NO);
        [pl setAlignment:NSRightTextAlignment];
        [pl setFrame:NSMakeRect(8, 364, 70, 17)];
        [content addSubview:pl];
        [pl release];
        _newsArticlePosterField = makeEditField(208);
        [_newsArticlePosterField setFrame:NSMakeRect(84, 360, 208, 22)];
        [content addSubview:_newsArticlePosterField];

        NSTextField *dl = makeLabel(@"Date:", 11.0, NO);
        [dl setAlignment:NSRightTextAlignment];
        [dl setFrame:NSMakeRect(308, 364, 60, 17)];
        [content addSubview:dl];
        [dl release];
        _newsArticleDateField = makeEditField(264);
        [_newsArticleDateField setFrame:NSMakeRect(370, 360, 262, 22)];
        [content addSubview:_newsArticleDateField];

        NSScrollView *bodySV = [[NSScrollView alloc]
            initWithFrame:NSMakeRect(8, 40, 624, 312)];
        [bodySV setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [bodySV setHasVerticalScroller:YES];
        [bodySV setBorderType:NSBezelBorder];
        _newsArticleBodyTextView = [[NSTextView alloc]
            initWithFrame:NSMakeRect(0, 0, 624, 312)];
        [_newsArticleBodyTextView setMinSize:NSMakeSize(0, 312)];
        [_newsArticleBodyTextView setMaxSize:NSMakeSize(1e7, 1e7)];
        [_newsArticleBodyTextView setVerticallyResizable:YES];
        [_newsArticleBodyTextView setHorizontallyResizable:NO];
        [_newsArticleBodyTextView setAutoresizingMask:NSViewWidthSizable];
        [[_newsArticleBodyTextView textContainer]
            setContainerSize:NSMakeSize(624, 1e7)];
        [[_newsArticleBodyTextView textContainer] setWidthTracksTextView:YES];
        [_newsArticleBodyTextView setFont:[NSFont fontWithName:@"Monaco" size:10.0]];
        [bodySV setDocumentView:_newsArticleBodyTextView];
        [content addSubview:bodySV];
        [bodySV release];

        NSButton *cancelBtn = makeButton(@"Cancel", self,
                                         @selector(cancelNewsArticleEditor:));
        [cancelBtn setFrame:NSMakeRect(464, 8, 80, 24)];
        [cancelBtn setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
        [content addSubview:cancelBtn];
        [cancelBtn release];

        NSButton *saveBtn = makeButton(@"Save", self,
                                       @selector(saveNewsArticleEditor:));
        [saveBtn setFrame:NSMakeRect(552, 8, 80, 24)];
        [saveBtn setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
        [content addSubview:saveBtn];
        [saveBtn release];
    }

    [_newsEditingCategoryKey release];
    _newsEditingCategoryKey = [_newsSelectedCategoryKey retain];
    [_newsEditingArticleID release];
    _newsEditingArticleID = nil;

    if (isNew) {
        [_newsArticleTitleField setStringValue:@""];
        [_newsArticlePosterField setStringValue:@"admin"];
        NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
        [fmt setDateFormat:@"yyyy-MM-dd HH:mm"];
        [_newsArticleDateField setStringValue:[fmt stringFromDate:[NSDate date]]];
        [fmt release];
        [_newsArticleBodyTextView setString:@""];
        [_newsArticleEditorWindow setTitle:@"Add News Article"];
    } else {
        int row = [_newsArticlesTableView selectedRow];
        if (row < 0 || row >= (int)[_newsArticleItems count]) return;
        NSDictionary *item = [_newsArticleItems objectAtIndex:(unsigned)row];
        NSString *articleID = [item objectForKey:@"id"];
        if (articleID) _newsEditingArticleID = [articleID retain];
        [_newsArticleTitleField setStringValue:[item objectForKey:@"title"] ? [item objectForKey:@"title"] : @""];
        [_newsArticlePosterField setStringValue:[item objectForKey:@"poster"] ? [item objectForKey:@"poster"] : @""];
        [_newsArticleDateField setStringValue:[item objectForKey:@"date"] ? [item objectForKey:@"date"] : @""];
        [_newsArticleBodyTextView setString:[item objectForKey:@"body"] ? [item objectForKey:@"body"] : @""];
        [_newsArticleEditorWindow setTitle:@"Edit News Article"];
    }

    [_newsArticleEditorWindow center];
    [_newsArticleEditorWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)addNewsArticle:(id)sender
{
    (void)sender;
    [self openNewsArticleEditorForNew:YES];
}

- (void)editNewsArticle:(id)sender
{
    (void)sender;
    [self openNewsArticleEditorForNew:NO];
}

- (void)deleteNewsArticle:(id)sender
{
    (void)sender;
    if (!_newsSelectedCategoryKey || !_newsSelectedArticleID) return;

    int rc = NSRunAlertPanel(@"Delete Article",
                             @"Delete selected article from \"%@\"?",
                             @"Delete", @"Cancel", nil, _newsSelectedCategoryKey);
    if (rc != NSAlertDefaultReturn) return;

    NSMutableDictionary *cat = [_newsCategoriesByKey objectForKey:_newsSelectedCategoryKey];
    NSMutableDictionary *articles = [cat objectForKey:@"articles"];
    if (!articles) return;
    [articles removeObjectForKey:_newsSelectedArticleID];
    [_newsSelectedArticleID release];
    _newsSelectedArticleID = nil;
    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
}

- (void)cancelNewsArticleEditor:(id)sender
{
    (void)sender;
    if (_newsArticleEditorWindow) [_newsArticleEditorWindow orderOut:nil];
}

- (void)saveNewsArticleEditor:(id)sender
{
    (void)sender;
    if (!_newsEditingCategoryKey || [_newsEditingCategoryKey length] == 0) return;

    NSString *title = trimmedString([_newsArticleTitleField stringValue]);
    NSString *poster = trimmedString([_newsArticlePosterField stringValue]);
    NSString *date = trimmedString([_newsArticleDateField stringValue]);
    NSString *body = [_newsArticleBodyTextView string];
    if ([title length] == 0) {
        NSRunAlertPanel(@"Missing Title", @"Article title is required.", @"OK", nil, nil);
        return;
    }
    if ([poster length] == 0) poster = @"admin";
    if ([date length] == 0) date = @"";
    if (!body) body = @"";

    NSMutableDictionary *cat = [_newsCategoriesByKey objectForKey:_newsEditingCategoryKey];
    if (!cat) return;
    NSMutableDictionary *articles = [cat objectForKey:@"articles"];
    if (!articles) {
        articles = [NSMutableDictionary dictionary];
        [cat setObject:articles forKey:@"articles"];
    }

    NSString *articleID = _newsEditingArticleID;
    if (!articleID || [articleID length] == 0) {
        articleID = [self nextThreadedNewsArticleIDForCategory:cat];
    }

    NSString *safeTitle = [title stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    NSString *safePoster = [poster stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    NSString *safeDate = [date stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    NSString *safeBody = [body stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];

    NSMutableDictionary *article = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        articleID, @"id",
        safeTitle, @"title",
        safePoster, @"poster",
        safeDate, @"date",
        safeBody, @"body",
        nil];
    [articles setObject:article forKey:articleID];

    [_newsSelectedCategoryKey release];
    _newsSelectedCategoryKey = [_newsEditingCategoryKey retain];
    [_newsSelectedArticleID release];
    _newsSelectedArticleID = [articleID retain];

    [self writeThreadedNewsCategories];
    [self loadThreadedNewsCategories];
    [self cancelNewsArticleEditor:nil];
}

