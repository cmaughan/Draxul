# Normalize Windows OSC 7 paths before reuse

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-core/src/terminal_core_csi.cpp:689` retains the URI’s leading slash for Windows drive paths. Metadata is reused as a working directory, while `app/app.cpp:3177` recognizes only `/` when naming tabs. Windows OSC 7 paths can therefore produce invalid launch directories or incorrect names.

**Investigation**

- [ ] Trace drive, UNC, percent-encoded, and backslash-containing paths through metadata, naming, and pane launch.

**Fix strategy**

- [ ] Convert file URIs to platform-appropriate filesystem paths.
- [ ] Extract display basenames using platform-aware rules while preserving POSIX behavior.

**Acceptance criteria**

- [ ] Windows drive and supported UNC paths round-trip into valid working directories.
- [ ] Tab names show the intended basename.
- [ ] POSIX paths remain unchanged semantically.
