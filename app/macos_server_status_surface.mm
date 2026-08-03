#include "macos_server_status_surface.h"

#import <Cocoa/Cocoa.h>

@interface DraxulServerApplicationDelegate : NSObject <NSApplicationDelegate>
{
@public
    void (*reopen_callback_)(void*);
    void (*quit_callback_)(void*);
    void* userdata_;
}
@end

@implementation DraxulServerApplicationDelegate

- (BOOL)applicationShouldHandleReopen:(NSApplication*)application
                    hasVisibleWindows:(BOOL)hasVisibleWindows
{
    (void)application;
    (void)hasVisibleWindows;
    if (reopen_callback_)
        reopen_callback_(userdata_);
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)application
{
    (void)application;
    if (quit_callback_)
        quit_callback_(userdata_);
    // Quitting the headless server can destroy live terminals. Keep the
    // server alive while its existing guarded stop dialog asks the user.
    return NSTerminateCancel;
}

- (void)handleReopenEvent:(NSAppleEventDescriptor*)event
            withReplyEvent:(NSAppleEventDescriptor*)replyEvent
{
    (void)event;
    (void)replyEvent;
    if (reopen_callback_)
        reopen_callback_(userdata_);
}

- (void)handleQuitEvent:(NSAppleEventDescriptor*)event
          withReplyEvent:(NSAppleEventDescriptor*)replyEvent
{
    (void)event;
    (void)replyEvent;
    if (quit_callback_)
        quit_callback_(userdata_);
}

@end

namespace draxul
{

bool configure_macos_server_status_application(
    MacosServerApplicationCallback reopen_callback,
    MacosServerApplicationCallback quit_callback,
    void* userdata, std::string& error)
{
    @autoreleasepool
    {
        NSApplication* application = [NSApplication sharedApplication];
        if (![application setActivationPolicy:NSApplicationActivationPolicyAccessory])
        {
            error = "macOS could not configure the server as a menu-bar application.";
            return false;
        }
        // SDL leaves an existing delegate in place. The server does not need
        // SDL's window/open-file delegate, but it does need to distinguish a
        // Finder reopen from a request to show a nonexistent server window.
        // Keep this object alive for the lifetime of the server process because
        // NSApplication's delegate property is not owning.
        static DraxulServerApplicationDelegate* server_delegate
            = [[DraxulServerApplicationDelegate alloc] init];
        server_delegate->reopen_callback_ = reopen_callback;
        server_delegate->quit_callback_ = quit_callback;
        server_delegate->userdata_ = userdata;
        [application setDelegate:server_delegate];
        [[NSAppleEventManager sharedAppleEventManager]
            setEventHandler:server_delegate
                 andSelector:@selector(handleReopenEvent:withReplyEvent:)
               forEventClass:kCoreEventClass
                  andEventID:kAEReopenApplication];
        [[NSAppleEventManager sharedAppleEventManager]
            setEventHandler:server_delegate
                 andSelector:@selector(handleQuitEvent:withReplyEvent:)
               forEventClass:kCoreEventClass
                  andEventID:kAEQuitApplication];
    }
    return true;
}

} // namespace draxul
