#include "macos_menu.h"
#include "gui_action_handler.h"
#import <Cocoa/Cocoa.h>

@interface DraxulMenuAction : NSObject
@property (nonatomic, copy) void (^action)(void);
- (void)perform:(id)sender;
@end

@implementation DraxulMenuAction
- (void)perform:(id)sender
{
    self.action();
}
@end

// Keep action objects alive for the lifetime of the menu.
static NSMutableArray* gActions = nil;

static NSMenuItem* make_item(
    NSString* title, NSString* key, NSEventModifierFlags mods, void (^block)(void))
{
    if (!gActions)
        gActions = [NSMutableArray array];
    DraxulMenuAction* act = [[DraxulMenuAction alloc] init];
    act.action = block;
    [gActions addObject:act];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                  action:@selector(perform:)
                                           keyEquivalent:key];
    item.keyEquivalentModifierMask = mods;
    item.target = act;
    return item;
}

namespace draxul
{

void install_macos_menu(GuiActionHandler& handler)
{
    NSMenu* menubar = [NSMenu new];
    [NSApp setMainMenu:menubar];

    // App menu
    NSMenuItem* appItem = [NSMenuItem new];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Draxul"];
    [appMenu addItemWithTitle:@"About Draxul"
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit Draxul"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [menubar addItem:appItem];

    // File menu
    NSMenuItem* fileItem = [NSMenuItem new];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    [fileMenu addItem:make_item(@"Open File...", @"o", NSEventModifierFlagCommand, ^{
        handler.execute("open_file_dialog");
    })];
    fileItem.submenu = fileMenu;
    [menubar addItem:fileItem];

    // Edit menu
    NSMenuItem* editItem = [NSMenuItem new];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItem:make_item(@"Copy", @"c", NSEventModifierFlagCommand, ^{
        handler.execute("copy");
    })];
    [editMenu addItem:make_item(@"Paste", @"v", NSEventModifierFlagCommand, ^{
        handler.execute("paste");
    })];
    editItem.submenu = editMenu;
    [menubar addItem:editItem];

    // View menu
    NSMenuItem* viewItem = [NSMenuItem new];
    NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
    [viewMenu addItem:make_item(
                          @"Toggle Diagnostics", @"d", NSEventModifierFlagCommand, ^{
                              handler.execute("toggle_diagnostics");
                          })];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:make_item(@"Split Vertical", @"|",
                          NSEventModifierFlagCommand | NSEventModifierFlagShift, ^{
                              handler.execute("split_vertical");
                          })];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:make_item(
                          @"Increase Font Size", @"+", NSEventModifierFlagCommand, ^{
                              handler.execute("font_increase");
                          })];
    [viewMenu addItem:make_item(
                          @"Decrease Font Size", @"-", NSEventModifierFlagCommand, ^{
                              handler.execute("font_decrease");
                          })];
    [viewMenu addItem:make_item(@"Reset Font Size", @"0", NSEventModifierFlagCommand, ^{
        handler.execute("font_reset");
    })];
    viewItem.submenu = viewMenu;
    [menubar addItem:viewItem];
}

} // namespace draxul
