# Preserve explicitly disabled default keybindings
**Severity:** HIGH  
**Source:** Codex #11; `libs/draxul-config/src/app_config_io.cpp:376`.

Removed bindings are omitted during serialization, so shutdown saving erases explicit disables and defaults return at restart.

**Investigation**

- [ ] Trace empty binding parsing, serialization, document merging, and shutdown persistence.

**Fix strategy**

- [ ] Serialize explicit empty entries for disabled default actions while preserving configured chords and unrelated settings.

**Acceptance criteria**

- [ ] `copy = ""` remains disabled after serialization, document merge, and restart.
- [ ] Verify enabled bindings still round-trip; run core aggregate tests and same-cache smoke.
