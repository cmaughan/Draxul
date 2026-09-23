# Bring Linux up as a tested platform

**Type:** feature
**Priority:** 124

## Current state

Linux is an intended Draxul target, but the repository does not currently have a
native Linux build, test, render, and smoke gate that can support platform claims.
POSIX runtime paths, Unix PTY/process handling, Neovim spawning, a file-monitor
backend, and the non-Apple Vulkan path already provide useful foundations. Treat
them as unvalidated until this project exercises the complete product on Linux.

This card is the single home for Linux validation. Other cards may preserve or
extend Linux-specific code, but Linux must not be used as their completion gate
or recorded as validated until this project establishes repeatable evidence.

## Work

- [ ] Define the initial supported distributions, compiler versions, window
      system, Vulkan requirements, and packaging contract.
- [ ] Inventory and repair CMake, dependency, shader, asset, plugin, and install
      paths until the complete desktop product configures and builds natively.
- [ ] Bring up SDL windowing, Vulkan rendering, fonts, clipboard, file monitoring,
      process/PTY handling, Neovim integration, sessions, and plugin discovery.
- [ ] Run the full unit and integration suite on native Linux and fix platform
      gaps without weakening the macOS or Windows contracts.
- [ ] Add deterministic Vulkan render comparisons and a real same-cache desktop
      smoke gate.
- [ ] Add a native Linux CI lane that builds and runs the agreed test, render,
      and smoke gates on every relevant change.
- [ ] Document the supported Linux contract, prerequisites, known limitations,
      and local validation commands in canonical repository guidance.
- [ ] Keep Windows and macOS behavior unchanged.

## Acceptance criteria

- [ ] A clean supported Linux environment configures and builds the complete app.
- [ ] The aggregate tests, Vulkan comparisons, and same-cache smoke pass in CI.
- [ ] Platform-specific behavior is covered or explicitly documented; tests are
      not silently skipped to obtain a green lane.
- [ ] Shared POSIX code remains usable and unsupported configurations fail with
      precise guidance.
- [ ] Linux validation claims in other cards link to reproducible evidence from
      the established native gate.
