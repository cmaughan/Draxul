#include "macos_server_status_surface.h"

#import <Cocoa/Cocoa.h>

namespace draxul
{

bool configure_macos_server_status_application(std::string& error)
{
    @autoreleasepool
    {
        NSApplication* application = [NSApplication sharedApplication];
        if (![application setActivationPolicy:NSApplicationActivationPolicyAccessory])
        {
            error = "macOS could not configure the server as a menu-bar application.";
            return false;
        }
    }
    return true;
}

} // namespace draxul
