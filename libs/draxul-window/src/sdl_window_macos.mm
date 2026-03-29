#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <draxul/perf_timing.h>
#include <draxul/types.h>

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

} // namespace draxul
