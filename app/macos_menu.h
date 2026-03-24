#pragma once
#ifdef __APPLE__

#include <memory>
#include <vector>

namespace draxul
{

class GuiActionHandler;

// Describes a single menu item for the macOS menu bar.
struct MenuItemDescriptor
{
    const char* title; // Display title (e.g. "Copy")
    const char* action_name; // GuiActionHandler action (e.g. "copy"), or nullptr for standard items
    const char* key_equivalent; // macOS key equivalent (e.g. "c"), empty string for none
    unsigned long modifiers; // NSEventModifierFlags combination
    const char* menu_group; // Which menu this item belongs to (e.g. "Edit", "View", "File")
};

// RAII wrapper around the macOS NSMenu bar. Owns all Objective-C action
// target objects and clears the menu on destruction.
class MacOsMenu
{
public:
    explicit MacOsMenu(GuiActionHandler& handler);
    ~MacOsMenu();

    MacOsMenu(const MacOsMenu&) = delete;
    MacOsMenu& operator=(const MacOsMenu&) = delete;

    // Returns the default menu item descriptors for the Draxul app.
    static std::vector<MenuItemDescriptor> default_menu_items();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul

#endif
