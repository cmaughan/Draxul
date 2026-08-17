# Decide and validate the Linux support matrix

**Type:** refactor
**Priority:** 124

## Current state

The earlier "dead Linux code" premise is no longer true. POSIX runtime paths,
Unix PTY/process handling, Neovim process spawning, non-Apple sanitizers, and the
non-Apple Vulkan build path are active shared infrastructure. Removing Linux guards
or adding a platform fatal error would regress supported headless/POSIX components.

The real gap is that Linux GUI support is not documented or continuously built as
a first-class platform.

## Work

- [ ] Inventory which headless libraries, tests, and Vulkan/window paths currently
      compile and run on Linux.
- [ ] Decide whether the supported contract is headless-only or includes the GUI.
- [ ] Document that contract in the README and canonical platform guidance.
- [ ] Add the smallest CI compile/test lane that protects the supported subset, or
      explicitly gate only components that cannot satisfy the documented contract.
- [ ] Keep Windows and macOS behavior unchanged.

## Acceptance criteria

- [ ] Linux support claims match an exercised build/test configuration.
- [ ] Shared POSIX code is not deleted merely because the full desktop product lacks CI.
- [ ] Unsupported components fail configuration with a precise explanation.
