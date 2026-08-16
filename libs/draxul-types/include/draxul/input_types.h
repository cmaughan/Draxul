#pragma once

#include <cstdint>

namespace draxul
{

// Platform-neutral modifier bitmask used throughout the event and config layer.
// Bit values are chosen to match SDL3's combined-modifier constants (SDL_KMOD_SHIFT,
// SDL_KMOD_CTRL, SDL_KMOD_ALT, SDL_KMOD_GUI) so that no conversion is required when
// constructing events from SDL input — only a widening cast from uint16_t to uint32_t.
// If the windowing backend is ever replaced, translate from the new backend's modifier
// values to these constants at the new boundary.
using ModifierFlags = uint32_t;

inline constexpr ModifierFlags kModNone = 0x0000;
inline constexpr ModifierFlags kModShift = 0x0003; // left+right shift
inline constexpr ModifierFlags kModCtrl = 0x00C0; // left+right ctrl
inline constexpr ModifierFlags kModAlt = 0x0300; // left+right alt
inline constexpr ModifierFlags kModSuper = 0x0C00; // left+right super/GUI
inline constexpr ModifierFlags kModCaps = 0x2000; // caps lock
inline constexpr ModifierFlags kModNum = 0x1000; // num lock
inline constexpr ModifierFlags kModScroll = 0x8000; // scroll lock

// Mask of all modifiers recognised for GUI keybinding matching.
inline constexpr ModifierFlags kGuiModifierMask = kModShift | kModCtrl | kModAlt | kModSuper;

// Latched lock keys that must never block a keybinding match.
inline constexpr ModifierFlags kLockModifierMask = kModCaps | kModNum | kModScroll;

// Reduces a raw modifier state to the four GUI-relevant modifier groups,
// widening a one-sided modifier (e.g. left ctrl) to its full group and
// dropping lock keys and any unrecognised bits.
constexpr ModifierFlags normalize_modifiers(ModifierFlags mod)
{
    ModifierFlags result = kModNone;
    if (mod & kModShift)
        result |= kModShift;
    if (mod & kModCtrl)
        result |= kModCtrl;
    if (mod & kModAlt)
        result |= kModAlt;
    if (mod & kModSuper)
        result |= kModSuper;
    return result;
}

// True when exactly the `expected` GUI modifiers are held. Caps/Num/Scroll
// lock are ignored (a latched lock key must not disable shortcuts), while any
// other unrecognised modifier bit (e.g. AltGr/mode) blocks the match.
constexpr bool has_only_modifiers(ModifierFlags actual, ModifierFlags expected)
{
    constexpr ModifierFlags kAcceptedModifiers = kGuiModifierMask | kLockModifierMask;
    return (actual & ~kAcceptedModifiers) == 0 && normalize_modifiers(actual) == expected;
}

} // namespace draxul
