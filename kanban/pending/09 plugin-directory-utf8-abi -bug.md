# Publish plugin resource directories as explicit UTF-8
**Severity:** HIGH  
**Source:** Codex #10; `libs/draxul-host/src/plugin_host.cpp:117`.

The host publishes `.string()` bytes through `plugin_directory_utf8`; plugins decode them as UTF-8, breaking non-ASCII Windows staging paths.

**Investigation**

- [ ] Trace the ABI directory field and representative plugin resource consumers.

**Fix strategy**

- [ ] Populate the field using explicit UTF-8 conversion with lifetime extending through instance creation.

**Acceptance criteria**

- [ ] Plugins load assets from non-ASCII Windows directories under non-UTF-8 ANSI settings.
- [ ] Run shared plugin aggregate coverage and same-cache smoke; verify macOS path behavior remains correct.
