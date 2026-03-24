#include "macos_menu.h"
#include "gui_action_handler.h"
#import <Cocoa/Cocoa.h>

#include <string>
#include <unordered_map>

// Objective-C action target: each instance holds a block that is invoked
// when the corresponding NSMenuItem is activated.
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

namespace draxul
{

struct MacOsMenu::Impl
{
    // Retains all DraxulMenuAction instances so they stay alive while the menu exists.
    NSMutableArray* action_targets = nil;
    // The menu bar itself, so we can clear it on destruction.
    NSMenu* menubar = nil;

    // Creates an NSMenuItem backed by a DraxulMenuAction and registers the target.
    NSMenuItem* make_item(NSString* title, NSString* key,
        NSEventModifierFlags mods, void (^block)(void))
    {
        DraxulMenuAction* act = [[DraxulMenuAction alloc] init];
        act.action = block;
        [action_targets addObject:act];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                      action:@selector(perform:)
                                               keyEquivalent:key];
        item.keyEquivalentModifierMask = mods;
        item.target = act;
        return item;
    }
};

std::vector<MenuItemDescriptor> MacOsMenu::default_menu_items()
{
    // NSEventModifierFlagCommand = 1 << 20 = 0x100000
    // NSEventModifierFlagShift   = 1 << 17 = 0x020000
    constexpr unsigned long kCmd = NSEventModifierFlagCommand;
    constexpr unsigned long kCmdShift = NSEventModifierFlagCommand | NSEventModifierFlagShift;

    return {
        // File menu
        { "Open File...", "open_file_dialog", "o", kCmd, "File" },

        // Edit menu
        { "Copy", "copy", "c", kCmd, "Edit" },
        { "Paste", "paste", "v", kCmd, "Edit" },

        // View menu
        { "Toggle Diagnostics", "toggle_diagnostics", "d", kCmd, "View" },
        { nullptr, nullptr, nullptr, 0, "View" }, // separator
        { "Split Vertical", "split_vertical", "|", kCmdShift, "View" },
        { "Split Horizontal", "split_horizontal", "_", kCmdShift, "View" },
        { nullptr, nullptr, nullptr, 0, "View" }, // separator
        { "Increase Font Size", "font_increase", "+", kCmd, "View" },
        { "Decrease Font Size", "font_decrease", "-", kCmd, "View" },
        { "Reset Font Size", "font_reset", "0", kCmd, "View" },
    };
}

MacOsMenu::MacOsMenu(GuiActionHandler& handler)
    : impl_(std::make_unique<Impl>())
{
    impl_->action_targets = [NSMutableArray array];

    NSMenu* menubar = [NSMenu new];
    impl_->menubar = menubar;
    [NSApp setMainMenu:menubar];

    // --- App menu (standard items, not driven by GuiActionHandler) ---
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

    // --- Data-driven menus from descriptors ---
    const auto items = default_menu_items();

    // Build submenus grouped by menu_group, preserving order.
    std::vector<std::string> group_order;
    std::unordered_map<std::string, NSMenu*> group_menus;

    for (const auto& desc : items)
    {
        std::string group = desc.menu_group;
        if (group_menus.find(group) == group_menus.end())
        {
            group_order.push_back(group);
            group_menus[group] =
                [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:group.c_str()]];
        }

        NSMenu* menu = group_menus[group];

        // A descriptor with nullptr title is a separator.
        if (!desc.title)
        {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }

        NSString* title = [NSString stringWithUTF8String:desc.title];
        NSString* key = [NSString stringWithUTF8String:desc.key_equivalent];
        NSEventModifierFlags mods = static_cast<NSEventModifierFlags>(desc.modifiers);

        // Capture the action name by value for the block.
        std::string action_name = desc.action_name;
        [menu addItem:impl_->make_item(title, key, mods, ^{
            handler.execute(action_name);
        })];
    }

    // Add group menus to the menu bar in order.
    for (const auto& group : group_order)
    {
        NSMenuItem* groupItem = [NSMenuItem new];
        groupItem.submenu = group_menus[group];
        [menubar addItem:groupItem];
    }
}

MacOsMenu::~MacOsMenu()
{
    if (impl_ && impl_->menubar)
    {
        // Remove all items from the menu bar so no stale references remain.
        [impl_->menubar removeAllItems];
        [NSApp setMainMenu:nil];
    }
    // impl_->action_targets is released when Impl is destroyed (ARC handles the NSMutableArray).
}

} // namespace draxul
