/*
 * AppController.m - Lemoniscate Server Admin GUI
 *
 * Maps to: MobiusAdmin AppState + ContentView + ServerControlView +
 *          SettingsFormView + LogView
 *
 * Tiger Obj-C 1.0. All UI built programmatically.
 * Layout: HSplitView with scrollable settings left, tab view right.
 */

#import "AppController.h"

/* ===== FlippedView =====
 * NSView subclass with flipped coords so (0,0) is top-left. */

@interface FlippedView : NSView
@end

@implementation FlippedView
- (BOOL)isFlipped { return YES; }
@end

/* ===== Layout constants ===== */
#define LEFT_PANEL_WIDTH  560
#define SECTION_MARGIN    12
#define LABEL_WIDTH       100
#define FIELD_X           (SECTION_MARGIN + LABEL_WIDTH + 6)
#define ROW_RIGHT_PAD     8
#define ROW_HEIGHT        28

/* ===== Helpers ===== */

static NSTextField *makeLabel(NSString *text, float fontSize, BOOL bold)
{
    NSTextField *label = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [label setStringValue:text];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setFont:bold ? [NSFont boldSystemFontOfSize:fontSize]
                        : [NSFont systemFontOfSize:fontSize]];
    [label sizeToFit];
    return label;
}

static NSTextField *makeEditField(float width)
{
    NSTextField *field = [[NSTextField alloc]
        initWithFrame:NSMakeRect(0, 0, width, 22)];
    [field setFont:[NSFont systemFontOfSize:12.0]];
    return field;
}

static NSButton *makeButton(NSString *title, id target, SEL action)
{
    NSButton *btn = [[NSButton alloc]
        initWithFrame:NSMakeRect(0, 0, 80, 28)];
    [btn setTitle:title];
    [btn setBezelStyle:NSRoundedBezelStyle];
    [btn setTarget:target];
    [btn setAction:action];
    [btn sizeToFit];
    NSRect f = [btn frame];
    f.size.width += 20;
    [btn setFrame:f];
    return btn;
}

/* Add a right-aligned label + text field row inside an NSBox content view.
 * Returns next y position. */
static float addRow(NSView *parent, NSString *labelText,
                    NSTextField *field, float y, float fieldWidth)
{
    NSTextField *label = makeLabel(labelText, 11.0, NO);
    [label setAlignment:NSRightTextAlignment];
    [label setFrame:NSMakeRect(6, y + 3, LABEL_WIDTH - 12, 17)];
    [parent addSubview:label];
    [label release];

    [field setFrame:NSMakeRect(LABEL_WIDTH - 4, y,
                               fieldWidth - ROW_RIGHT_PAD, 22)];
    [parent addSubview:field];
    return y + ROW_HEIGHT;
}

/* Add a right-aligned label + text field + browse button row.
 * Returns next y position. */
static float addRowWithButton(NSView *parent, NSString *labelText,
                               NSTextField *field, NSButton *btn,
                               float y, float fieldWidth)
{
    NSTextField *label = makeLabel(labelText, 11.0, NO);
    [label setAlignment:NSRightTextAlignment];
    [label setFrame:NSMakeRect(6, y + 3, LABEL_WIDTH - 12, 17)];
    [parent addSubview:label];
    [label release];

    [btn setFont:[NSFont systemFontOfSize:11.0]];
    [btn sizeToFit];
    NSRect bf = [btn frame];
    float btnWidth = bf.size.width + 12.0f;
    if (btnWidth < 70.0f) btnWidth = 70.0f;
    if (btnWidth > 110.0f) btnWidth = 110.0f;

    float rightPad = ROW_RIGHT_PAD;
    float gap = 4.0f;
    [field setFrame:NSMakeRect(LABEL_WIDTH - 4, y,
                               fieldWidth - btnWidth - gap - rightPad, 22)];
    [parent addSubview:field];

    [btn setFrame:NSMakeRect(LABEL_WIDTH - 4 + fieldWidth - btnWidth - rightPad, y - 1,
                             btnWidth, 24)];
    [parent addSubview:btn];
    return y + ROW_HEIGHT;
}

/* Add a checkbox row. Returns next y. */
static float addCheckbox(NSView *parent, NSButton *checkbox,
                          float y)
{
    [checkbox setFrame:NSMakeRect(LABEL_WIDTH - 4, y, 200, 18)];
    [checkbox setFont:[NSFont systemFontOfSize:11.0]];
    [parent addSubview:checkbox];
    return y + 24;
}

/* Create a section box. */
static NSBox *makeSection(NSString *title, float x, float y,
                           float w, float h)
{
    NSBox *box = [[NSBox alloc]
        initWithFrame:NSMakeRect(x, y, w, h)];
    [box setTitle:title];
    [box setTitleFont:[NSFont boldSystemFontOfSize:11.0]];
    return box;
}

static NSString *trimmedString(NSString *s)
{
    if (!s) return @"";
    return [s stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static NSString *yamlUnquote(NSString *value)
{
    NSString *v = trimmedString(value);
    if ([v length] >= 2 &&
        (([v characterAtIndex:0] == '"' && [v characterAtIndex:[v length] - 1] == '"') ||
         ([v characterAtIndex:0] == '\'' && [v characterAtIndex:[v length] - 1] == '\''))) {
        return [v substringWithRange:NSMakeRange(1, [v length] - 2)];
    }
    return v;
}

static BOOL yamlBoolValue(NSString *value)
{
    NSString *v = [[trimmedString(value) lowercaseString] stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return [v isEqualToString:@"true"] || [v isEqualToString:@"yes"] || [v isEqualToString:@"1"];
}

static int leadingSpaces(NSString *s)
{
    unsigned i;
    int count = 0;
    for (i = 0; i < [s length]; i++) {
        unichar ch = [s characterAtIndex:i];
        if (ch == ' ') count++;
        else break;
    }
    return count;
}

static NSString *yamlQuoted(NSString *s)
{
    NSString *v = s ? s : @"";
    NSMutableString *m = [NSMutableString stringWithString:v];
    [m replaceOccurrencesOfString:@"\\"
                       withString:@"\\\\"
                          options:0
                            range:NSMakeRange(0, [m length])];
    [m replaceOccurrencesOfString:@"\""
                       withString:@"\\\""
                          options:0
                            range:NSMakeRange(0, [m length])];
    return [NSString stringWithFormat:@"\"%@\"", m];
}

static void parseInlineYAMLArray(NSString *value, NSMutableArray *outItems)
{
    if (!value || !outItems) return;
    NSString *v = trimmedString(value);
    if (![v hasPrefix:@"["] || ![v hasSuffix:@"]"]) return;
    if ([v length] < 2) return;

    NSString *inner = trimmedString([v substringWithRange:
        NSMakeRange(1, [v length] - 2)]);
    if ([inner length] == 0) return;

    NSArray *parts = [inner componentsSeparatedByString:@","];
    unsigned i;
    for (i = 0; i < [parts count]; i++) {
        NSString *item = yamlUnquote(trimmedString([parts objectAtIndex:i]));
        if ([item length] == 0) continue;
        if (![outItems containsObject:item]) [outItems addObject:item];
    }
}

static NSString *humanFileSize(unsigned long long size)
{
    double value = (double)size;
    if (value < 1024.0) return [NSString stringWithFormat:@"%llu B", size];
    value /= 1024.0;
    if (value < 1024.0) return [NSString stringWithFormat:@"%.1f KB", value];
    value /= 1024.0;
    if (value < 1024.0) return [NSString stringWithFormat:@"%.1f MB", value];
    value /= 1024.0;
    return [NSString stringWithFormat:@"%.1f GB", value];
}

static unsigned long long parseLogByteCount(NSString *text)
{
    if (!text || [text length] == 0) return 0;

    NSRange bytesRange = [text rangeOfString:@" bytes"];
    if (bytesRange.location == NSNotFound) return 0;

    NSRange openParen = [text rangeOfString:@"("
                                   options:NSBackwardsSearch
                                     range:NSMakeRange(0, bytesRange.location)];
    if (openParen.location == NSNotFound) return 0;

    NSString *inside = [text substringWithRange:
        NSMakeRange(openParen.location + 1,
                    bytesRange.location - openParen.location - 1)];
    inside = trimmedString(inside);
    if ([inside length] == 0) return 0;

    unsigned long long value = 0;
    unsigned i;
    for (i = 0; i < [inside length]; i++) {
        unichar ch = [inside characterAtIndex:i];
        if (ch < '0' || ch > '9') break;
        value = value * 10ULL + (unsigned long long)(ch - '0');
    }
    return value;
}

typedef struct {
    const char *group;
    const char *label;
    const char *key;
} AccountPermissionDef;

static const AccountPermissionDef kAccountPermissionDefs[] = {
    {"Files", "Download Files", "DownloadFile"},
    {"Files", "Download Folders", "DownloadFolder"},
    {"Files", "Upload Files", "UploadFile"},
    {"Files", "Upload Folders", "UploadFolder"},
    {"Files", "Upload Anywhere", "UploadAnywhere"},
    {"Files", "Delete Files", "DeleteFile"},
    {"Files", "Delete Folders", "DeleteFolder"},
    {"Files", "Rename Files", "RenameFile"},
    {"Files", "Rename Folders", "RenameFolder"},
    {"Files", "Move Files", "MoveFile"},
    {"Files", "Move Folders", "MoveFolder"},
    {"Files", "Create Folders", "CreateFolder"},
    {"Files", "Set File Comments", "SetFileComment"},
    {"Files", "Set Folder Comments", "SetFolderComment"},
    {"Files", "View Drop Boxes", "ViewDropBoxes"},
    {"Files", "Make Aliases", "MakeAlias"},

    {"Chat", "Read Chat", "ReadChat"},
    {"Chat", "Send Chat", "SendChat"},
    {"Chat", "Open Private Chat", "OpenChat"},
    {"Chat", "Close Chat", "CloseChat"},

    {"Users", "Create Accounts", "CreateUser"},
    {"Users", "Delete Accounts", "DeleteUser"},
    {"Users", "Read Accounts", "OpenUser"},
    {"Users", "Modify Accounts", "ModifyUser"},
    {"Users", "Change Own Password", "ChangeOwnPass"},
    {"Users", "Disconnect Users", "DisconnectUser"},
    {"Users", "Cannot Be Disconnected", "CannotBeDisconnected"},
    {"Users", "Get Client Info", "GetClientInfo"},
    {"Users", "Show In List", "ShowInList"},

    {"News", "Read Articles", "NewsReadArt"},
    {"News", "Post Articles", "NewsPostArt"},
    {"News", "Delete Articles", "NewsDeleteArt"},
    {"News", "Create Categories", "NewsCreateCat"},
    {"News", "Delete Categories", "NewsDeleteCat"},
    {"News", "Create News Bundles", "NewsCreateFldr"},
    {"News", "Delete News Bundles", "NewsDeleteFldr"},

    {"Messaging", "Send Private Messages", "SendPrivMsg"},
    {"Messaging", "Broadcast", "Broadcast"},

    {"Misc", "Use Any Name", "AnyName"},
    {"Misc", "No Agreement Required", "NoAgreement"}
};

static const unsigned kAccountPermissionDefCount =
    sizeof(kAccountPermissionDefs) / sizeof(kAccountPermissionDefs[0]);

@interface AppController ()
- (NSView *)createSettingsPanel;
- (NSView *)createRightPanel;
- (NSView *)createServerTabView;
- (NSView *)createLogsTabView;
- (NSView *)createAccountsTabView;
- (NSView *)createOnlineTabView;
- (NSView *)createFilesTabView;
- (NSView *)createNewsTabView;
- (void)layoutRightPanel;
- (void)createMainMenu;
- (void)createMainWindow;
- (void)updateServerUI;
- (void)updateFooterStats;
- (void)refreshLogTextView;
- (void)loadSettings;
- (void)loadAccountsListData;
- (void)loadBanListData;
- (void)writeBanListData;
- (NSDictionary *)loadAccountDataForLogin:(NSString *)login;
- (void)populateAccountEditorFromData:(NSDictionary *)acct;
- (void)populateAccountEditorForNewAccount;
- (void)rebuildAccountPermissionUI;
- (void)syncPermissionCheckboxesFromAccess;
- (void)updateAccountPasswordStatus;
- (void)updateAccountTemplateFromAccessKeys;
- (NSMutableSet *)guestAccessTemplate;
- (NSMutableSet *)adminAccessTemplate;
- (void)processOnlineLogLine:(NSString *)text;
- (void)updateOnlineUI;
- (NSString *)resolvedFileRootPath;
- (void)loadFilesAtPath:(NSString *)path;
- (void)loadMessageBoardText;
- (void)loadThreadedNewsCategories;
- (void)writeThreadedNewsCategories;
- (void)refreshThreadedNewsArticles;
- (NSString *)nextThreadedNewsArticleIDForCategory:(NSDictionary *)category;
- (void)openNewsArticleEditorForNew:(BOOL)isNew;
- (void)setMessageBoardDirty:(BOOL)dirty;
- (void)openTextConfigFileNamed:(NSString *)filename title:(NSString *)title;
- (void)saveTextEditor:(id)sender;
- (void)closeTextEditor:(id)sender;
- (void)wizardBack:(id)sender;
- (void)wizardNext:(id)sender;
- (void)wizardFinish:(id)sender;
- (void)wizardFinishAndStart:(id)sender;
- (void)closeWizard:(id)sender;
- (void)rebuildWizardStepUI;
- (BOOL)validateWizardStep:(BOOL)showAlert;
- (void)applyWizardValuesToSettings;
- (void)openProjectURL:(NSString *)urlString;
- (void)openServerRepository:(id)sender;
- (void)openGUIRepository:(id)sender;
- (void)openProjectHomepage:(id)sender;
- (void)openDownloadPage:(id)sender;
- (void)loadConfigFromDisk;
- (void)writeConfigToDisk;
- (void)ensureConfigScaffolding;
@end

@implementation AppController

#import "AppController+LifecycleConfig.inc"


#import "AppController+LayoutAndTabs.inc"


#import "AppController+GeneralActions.inc"

#import "AppController+AccountsActions.inc"

#import "AppController+OnlineFilesActions.inc"

#import "AppController+NewsActions.inc"

#import "AppController+LogActions.inc"


#import "AppController+TextEditorActions.inc"

#import "AppController+AccountsData.inc"

#import "AppController+OnlineFilesData.inc"

#import "AppController+NewsData.inc"

#import "AppController+TableNotifications.inc"

#import "AppController+StatusUI.inc"

@end
