# Configurable Scrollback Capacity

**Type:** feature
**Priority:** 34
**Raised by:** Claude

## Summary

`kScrollbackCapacity = 2000` in `libs/draxul-host/src/terminal_host_base.h` is hardcoded. Expose it as `[terminal] scrollback_lines` in `config.toml` so users can adjust the scrollback buffer size to match their workflow (power users may want 10,000+ lines; memory-constrained systems may want fewer).

## Background

2,000 lines is a reasonable default but is not appropriate for all users. Someone reviewing build output or log files in a terminal may need thousands of lines of scrollback. Someone running Draxul on a memory-constrained device may want to reduce it. Hardcoding this value forces all users to accept the default. The implementation is straightforward: read the value from config at startup and pass it to `TerminalHostBase`.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.h` — change `kScrollbackCapacity` from a compile-time constant to a runtime parameter (constructor argument or `set_scrollback_capacity()`)
- `libs/draxul-host/src/terminal_host_base.cpp` — use the runtime parameter instead of the constant
- `libs/draxul-app-support/` — extend `AppConfig` with `scrollback_lines` field (default 2000)
- Config parsing — parse `[terminal] scrollback_lines` from `config.toml`
- `app/app.cpp` — pass `config.scrollback_lines` to `TerminalHostBase` constructor

### Steps
- [ ] Add `int scrollback_lines = 2000` to `AppConfig`
- [ ] Parse `[terminal] scrollback_lines` from config; clamp to a sane range (e.g., 100–100000) and log a warning if out of range
- [ ] Change `TerminalHostBase` to accept scrollback capacity as a constructor parameter
- [ ] Update all `TerminalHostBase` construction sites to pass the config value
- [ ] Update `kScrollbackCapacity` usages in `.cpp` to use the instance field
- [ ] Update work item `14` tests to pass a small capacity for efficient boundary testing
- [ ] Document the config option

## Depends On
- None

## Blocks
- None

## Notes
Consider whether the scrollback capacity can be changed at runtime (after construction) or only at startup. Changing it at runtime requires resizing or recreating the ring buffer, which is complex. Startup-only is simpler and sufficient for most users.

> Work item produced by: claude-sonnet-4-6
