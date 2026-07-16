#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <draxul/perf_timing.h>
#include <draxul/types.h>
#include <functional>
#include <memory>

@interface DraxulDockReopenHandler : NSObject
@property(nonatomic, copy) void (^callback)(void);
- (void)handleAppleEvent:(NSAppleEventDescriptor*)event
          withReplyEvent:(NSAppleEventDescriptor*)replyEvent;
@end

@implementation DraxulDockReopenHandler
- (void)handleAppleEvent:(NSAppleEventDescriptor*)event
          withReplyEvent:(NSAppleEventDescriptor*)replyEvent
{
    (void)event;
    (void)replyEvent;
    if (self.callback)
        self.callback();
}
@end

namespace draxul
{

void disable_press_and_hold_macos()
{
    // SDL3's Cocoa event handler registers ApplePressAndHoldEnabled = YES as a
    // default, which enables macOS's accent-picker popup when holding a key and
    // suppresses OS key-repeat events.  Terminal emulators need the opposite:
    // actual key repeat.  Explicitly setting the key (not just registering a
    // default) overrides SDL's registration and restores repeat behaviour.
    [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"ApplePressAndHoldEnabled"];
}

void apply_title_bar_color_macos(SDL_Window* window, Color color)
{
    PERF_MEASURE();
    NSWindow* ns_window = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!ns_window)
        return;

    ns_window.titlebarAppearsTransparent = YES;
    ns_window.backgroundColor = [NSColor colorWithRed:color.r green:color.g blue:color.b alpha:1.0];
}

// ---------------------------------------------------------------------------
// Dock reopen handler (Ghostty-style: click dock icon → show window)
// ---------------------------------------------------------------------------
// SDL3 installs its own NSApplicationDelegate, so we can't directly implement
// applicationShouldHandleReopen:hasVisibleWindows: without subclassing or
// swizzling SDL's delegate. Instead, an instance-owned NSAppleEventManager
// handler receives the "reopen" Apple Event (kAEReopenApplication), which the
// system sends when the user clicks the Dock icon while no windows are visible.

namespace
{
struct DockReopenContext
{
    DraxulDockReopenHandler* handler = nil;
};
}

void* install_dock_reopen_handler(std::function<void()> callback)
{
    auto context = std::make_unique<DockReopenContext>();
    context->handler = [[DraxulDockReopenHandler alloc] init];
    auto owned_callback = std::make_shared<std::function<void()>>(std::move(callback));
    context->handler.callback = ^{
        if (*owned_callback)
            (*owned_callback)();
    };
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:context->handler
            andSelector:@selector(handleAppleEvent:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEReopenApplication];
    return context.release();
}

void uninstall_dock_reopen_handler(void* raw_context)
{
    if (!raw_context)
        return;
    auto context = std::unique_ptr<DockReopenContext>(
        static_cast<DockReopenContext*>(raw_context));
    [[NSAppleEventManager sharedAppleEventManager]
        removeEventHandlerForEventClass:kCoreEventClass
                              andEventID:kAEReopenApplication];
    context->handler.callback = nil;
    context->handler = nil;
}

} // namespace draxul
