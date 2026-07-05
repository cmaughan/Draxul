# Platform window callback lifetime safety

**Type:** bug
**Priority:** 10
**Raised by:** Claude, with Gemini's broader lifetime concern

## Problem

Windows tray-icon and macOS Dock-reopen glue retain raw pointers to callback members owned by one `SdlWindow`. Those pointers can outlive the window and implicitly cap the design at one live window.

## Implementation plan

- [ ] Inventory the install/uninstall order for tray, menu, Apple Event, and SDL window callbacks.
- [ ] On Windows bind the owning instance through the native window user-data/context mechanism and clear it before destruction.
- [ ] On macOS pass an owned context through `SRefCon` or an Objective-C handler object; remove the redundant dead handler installation.
- [ ] Add RAII registration tokens whose destructors unregister before callback storage is destroyed.
- [ ] Ignore late callbacks after teardown through an explicit lifetime token, not a dangling raw pointer.
- [ ] Make the design instance-aware so item 43 can support multiple windows later.

## Tests

- [ ] Add platform tests for install, callback, window destruction, and a simulated late event.
- [ ] Create/destroy multiple windows sequentially and verify the current instance receives the callback.
- [ ] Verify shutdown ordering with macOS menus and Windows tray state.

## Acceptance criteria

- [ ] No platform-global pointer refers to a `SdlWindow` member.
- [ ] Callback registration has a symmetric, deterministic unregister path.
- [ ] Window tests and smoke pass on both platforms.

## Dependencies and parallelism

Independent bug and prerequisite for detachable multi-window work (item 43).

<model>GPT-5 Codex</model>
