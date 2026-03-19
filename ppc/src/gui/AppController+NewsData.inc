- (void)loadMessageBoardText
{
    if (!_messageBoardTextView) return;
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:@"MessageBoard.txt"];
    NSString *text = [NSString stringWithContentsOfFile:path
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (!text) text = @"";
    [_messageBoardTextView setString:text];
    [self setMessageBoardDirty:NO];
}

- (void)loadThreadedNewsCategories
{
    [self ensureConfigScaffolding];
    [_newsCategoryItems removeAllObjects];
    [_newsArticleItems removeAllObjects];
    [_newsCategoriesByKey removeAllObjects];

    NSString *path = [_configDir stringByAppendingPathComponent:@"ThreadedNews.yaml"];
    NSString *yaml = [NSString stringWithContentsOfFile:path
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (yaml) {
        NSArray *lines = [yaml componentsSeparatedByCharactersInSet:
            [NSCharacterSet newlineCharacterSet]];
        NSString *currentCategoryKey = nil;
        NSMutableDictionary *currentCategory = nil;
        NSMutableDictionary *currentArticles = nil;
        NSMutableDictionary *currentArticle = nil;
        unsigned i;
        for (i = 0; i < [lines count]; i++) {
            NSString *raw = [lines objectAtIndex:i];
            NSString *line = trimmedString(raw);
            int indent = leadingSpaces(raw);
            if ([line length] == 0 || [line hasPrefix:@"#"]) continue;
            if (indent == 0) continue;

            if (indent == 2 && [line hasSuffix:@":"]) {
                NSString *key = trimmedString([line substringToIndex:[line length] - 1]);
                key = yamlUnquote(key);
                if ([key length] == 0) continue;
                currentCategoryKey = key;
                currentCategory = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    key, @"name",
                    @"bundle", @"type",
                    [NSMutableDictionary dictionary], @"articles",
                    nil];
                [_newsCategoriesByKey setObject:currentCategory forKey:key];
                currentArticles = [currentCategory objectForKey:@"articles"];
                currentArticle = nil;
                continue;
            }

            if (!currentCategory) continue;

            NSRange sep = [line rangeOfString:@":"];
            if (sep.location == NSNotFound) continue;
            NSString *key = trimmedString([line substringToIndex:sep.location]);
            NSString *val = yamlUnquote([line substringFromIndex:sep.location + 1]);

            if (indent == 4) {
                if ([key isEqualToString:@"Name"] && [val length] > 0) {
                    [currentCategory setObject:val forKey:@"name"];
                } else if ([key isEqualToString:@"Type"] && [val length] > 0) {
                    [currentCategory setObject:val forKey:@"type"];
                } else if ([key isEqualToString:@"Articles"]) {
                    currentArticle = nil;
                }
                continue;
            }

            if (indent == 6 && [line hasSuffix:@":"]) {
                NSString *articleID = trimmedString([line substringToIndex:[line length] - 1]);
                articleID = yamlUnquote(articleID);
                if ([articleID length] == 0) continue;
                currentArticle = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    articleID, @"id",
                    @"", @"title",
                    @"", @"poster",
                    @"", @"date",
                    @"", @"body",
                    nil];
                if (!currentArticles) {
                    currentArticles = [NSMutableDictionary dictionary];
                    [currentCategory setObject:currentArticles forKey:@"articles"];
                }
                [currentArticles setObject:currentArticle forKey:articleID];
                continue;
            }

            if (indent >= 8 && currentArticle) {
                NSString *decodedVal = [val stringByReplacingOccurrencesOfString:@"\\n"
                                                                       withString:@"\n"];
                if ([key isEqualToString:@"Title"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"title"];
                } else if ([key isEqualToString:@"Poster"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"poster"];
                } else if ([key isEqualToString:@"Date"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"date"];
                } else if ([key isEqualToString:@"Body"]) {
                    [currentArticle setObject:(decodedVal ? decodedVal : @"")
                                      forKey:@"body"];
                }
            }
        }
        (void)currentCategoryKey;
    }

    NSArray *keys = [[_newsCategoriesByKey allKeys] sortedArrayUsingSelector:
        @selector(caseInsensitiveCompare:)];
    [_newsCategoryItems addObjectsFromArray:keys];

    if (_newsCategoriesTableView) {
        [_newsCategoriesTableView reloadData];
        if ([_newsCategoryItems count] > 0) {
            NSUInteger idx = NSNotFound;
            if (_newsSelectedCategoryKey) {
                idx = [_newsCategoryItems indexOfObject:_newsSelectedCategoryKey];
            }
            if (idx == NSNotFound) idx = 0;
            [_newsCategoriesTableView selectRow:(int)idx byExtendingSelection:NO];
            [_newsSelectedCategoryKey release];
            _newsSelectedCategoryKey = [[_newsCategoryItems objectAtIndex:idx] retain];
        } else {
            [_newsSelectedCategoryKey release];
            _newsSelectedCategoryKey = nil;
        }
    }
    [self refreshThreadedNewsArticles];
}

- (void)writeThreadedNewsCategories
{
    [self ensureConfigScaffolding];
    NSString *path = [_configDir stringByAppendingPathComponent:@"ThreadedNews.yaml"];

    if ([_newsCategoriesByKey count] == 0) {
        [@"Categories: {}\n" writeToFile:path atomically:YES
                                encoding:NSUTF8StringEncoding error:nil];
        return;
    }

    NSMutableString *yaml = [NSMutableString stringWithString:@"Categories:\n"];
    NSArray *keys = [[_newsCategoriesByKey allKeys] sortedArrayUsingSelector:
        @selector(caseInsensitiveCompare:)];
    unsigned i;
    for (i = 0; i < [keys count]; i++) {
        NSString *key = [keys objectAtIndex:i];
        NSDictionary *cat = [_newsCategoriesByKey objectForKey:key];
        NSString *name = [cat objectForKey:@"name"];
        NSString *type = [cat objectForKey:@"type"];
        NSDictionary *articles = [cat objectForKey:@"articles"];
        if (!name) name = key;
        if (!type || [type length] == 0) type = @"bundle";

        [yaml appendFormat:@"  %@:\n", yamlQuoted(key)];
        [yaml appendFormat:@"    Name: %@\n", yamlQuoted(name)];
        [yaml appendFormat:@"    Type: %@\n", yamlQuoted(type)];

        if (!articles || [articles count] == 0) {
            [yaml appendString:@"    Articles: {}\n"];
            continue;
        }

        [yaml appendString:@"    Articles:\n"];
        NSArray *articleIDs = [[articles allKeys] sortedArrayUsingSelector:
            @selector(caseInsensitiveCompare:)];
        unsigned j;
        for (j = 0; j < [articleIDs count]; j++) {
            NSString *articleID = [articleIDs objectAtIndex:j];
            NSDictionary *art = [articles objectForKey:articleID];
            NSString *title = [art objectForKey:@"title"];
            NSString *poster = [art objectForKey:@"poster"];
            NSString *date = [art objectForKey:@"date"];
            NSString *body = [art objectForKey:@"body"];
            if (title) title = [title stringByReplacingOccurrencesOfString:@"\n"
                                                                 withString:@"\\n"];
            if (poster) poster = [poster stringByReplacingOccurrencesOfString:@"\n"
                                                                     withString:@"\\n"];
            if (date) date = [date stringByReplacingOccurrencesOfString:@"\n"
                                                               withString:@"\\n"];
            if (body) body = [body stringByReplacingOccurrencesOfString:@"\n"
                                                               withString:@"\\n"];

            [yaml appendFormat:@"      %@:\n", yamlQuoted(articleID)];
            [yaml appendFormat:@"        Title: %@\n", yamlQuoted(title ? title : @"")];
            [yaml appendFormat:@"        Poster: %@\n", yamlQuoted(poster ? poster : @"")];
            [yaml appendFormat:@"        Date: %@\n", yamlQuoted(date ? date : @"")];
            [yaml appendFormat:@"        Body: %@\n", yamlQuoted(body ? body : @"")];
        }
    }

    [yaml writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

- (void)refreshThreadedNewsArticles
{
    [_newsArticleItems removeAllObjects];
    NSString *selected = _newsSelectedCategoryKey;
    if (!selected && _newsCategoriesTableView) {
        int row = [_newsCategoriesTableView selectedRow];
        if (row >= 0 && row < (int)[_newsCategoryItems count]) {
            selected = [_newsCategoryItems objectAtIndex:(unsigned)row];
        }
    }

    if (selected) {
        NSDictionary *cat = [_newsCategoriesByKey objectForKey:selected];
        NSDictionary *articles = [cat objectForKey:@"articles"];
        NSArray *articleIDs = [[articles allKeys] sortedArrayUsingSelector:
            @selector(caseInsensitiveCompare:)];
        unsigned i;
        for (i = 0; i < [articleIDs count]; i++) {
            NSString *articleID = [articleIDs objectAtIndex:i];
            NSDictionary *art = [articles objectForKey:articleID];
            NSMutableDictionary *item = [NSMutableDictionary dictionary];
            [item setObject:articleID forKey:@"id"];
            [item setObject:([art objectForKey:@"title"] ? [art objectForKey:@"title"] : @"")
                     forKey:@"title"];
            [item setObject:([art objectForKey:@"poster"] ? [art objectForKey:@"poster"] : @"")
                     forKey:@"poster"];
            [item setObject:([art objectForKey:@"date"] ? [art objectForKey:@"date"] : @"")
                     forKey:@"date"];
            [item setObject:([art objectForKey:@"body"] ? [art objectForKey:@"body"] : @"")
                     forKey:@"body"];
            [_newsArticleItems addObject:item];
        }
    }

    if (_newsArticlesTableView) {
        [_newsArticlesTableView reloadData];
        int targetRow = -1;
        if (_newsSelectedArticleID && [selected length] > 0) {
            unsigned i;
            for (i = 0; i < [_newsArticleItems count]; i++) {
                NSDictionary *item = [_newsArticleItems objectAtIndex:i];
                if ([[_newsSelectedArticleID description] isEqualToString:
                    [[item objectForKey:@"id"] description]]) {
                    targetRow = (int)i;
                    break;
                }
            }
        }
        if (targetRow >= 0) {
            [_newsArticlesTableView selectRow:targetRow byExtendingSelection:NO];
        } else {
            [_newsArticlesTableView deselectAll:nil];
            [_newsSelectedArticleID release];
            _newsSelectedArticleID = nil;
        }
    }
    if (_newsDeleteCategoryButton) {
        [_newsDeleteCategoryButton setEnabled:(selected != nil)];
    }
    if (_newsAddArticleButton) {
        [_newsAddArticleButton setEnabled:(selected != nil)];
    }
    if (_newsEditArticleButton) {
        [_newsEditArticleButton setEnabled:(_newsSelectedArticleID != nil)];
    }
    if (_newsDeleteArticleButton) {
        [_newsDeleteArticleButton setEnabled:(_newsSelectedArticleID != nil)];
    }
}

- (void)setMessageBoardDirty:(BOOL)dirty
{
    _messageBoardDirty = dirty;
    if (_messageBoardDirtyLabel) {
        [_messageBoardDirtyLabel setHidden:!dirty];
    }
}

/* ===== NSTableView datasource ===== */

