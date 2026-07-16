# Platform window callback lifetime safety

**Type:** bug
**Priority:** 10
**Raised by:** Claude, with Gemini's broader lifetime concern

## Problem

Windows tray-icon and macOS Dock-reopen glue retain raw pointers to callback members owned by one `SdlWindow`. Those pointers can outlive the window and implicitly cap the design at one live window. `InputDispatcher::connect()` also installs window callbacks that capture the dispatcher as a raw `this`, but the dispatcher has no symmetric disconnect path; current shutdown ordering narrows the window, but a late event or future ownership reorder can call a destroyed dispatcher.

## Implementation plan

- [ ] Inventory the install/uninstall order for tray, menu, Apple Event, and SDL window callbacks.
- [ ] Give `IWindow` a symmetric callback-clearing/connection-token contract and make `InputDispatcher` release its registrations before destruction or reconnection.
- [ ] Clear App-owned `on_close_requested`, `on_quit_requested`, `on_dock_reopen`, resize, drop, text, key, and mouse callbacks before their captured owners begin teardown.
- [ ] On Windows bind the owning instance through the native window user-data/context mechanism and clear it before destruction.
- [ ] On macOS pass an owned context through `SRefCon` or an Objective-C handler object; remove the redundant dead handler installation.
- [ ] Add RAII registration tokens whose destructors unregister before callback storage is destroyed.
- [ ] Ignore late callbacks after teardown through an explicit lifetime token, not a dangling raw pointer.
- [ ] Make the design instance-aware so item 43 can support multiple windows later.

## Tests

- [ ] Add platform tests for install, callback, window destruction, and a simulated late event.
- [ ] Create/destroy multiple windows sequentially and verify the current instance receives the callback.
- [ ] Verify shutdown ordering with macOS menus and Windows tray state.
- [ ] Destroy an `InputDispatcher` while a fake window remains alive, fire every stored callback, and prove none reaches the destroyed object.

## Acceptance criteria

- [ ] No platform-global pointer refers to a `SdlWindow` member.
- [ ] Callback registration has a symmetric, deterministic unregister path.
- [ ] No `IWindow` callback captures a dead `InputDispatcher` or `App` during teardown.
- [ ] Window tests and smoke pass on both platforms.

## Dependencies and parallelism

Independent bug and prerequisite for detachable multi-window work (item 43).

<model>GPT-5 Codex</model>
