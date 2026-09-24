# Prevent cross-process plugin storage collisions
**Severity:** CRITICAL  
**Source:** Codex #2; `libs/draxul-host/src/plugin_storage.cpp:467`.

Separate clients saving shared plugin/pane state can select the same `.tmp-N` filename, truncate each other’s writes, or modify an already-published inode.

**Investigation**

- [ ] Exercise concurrent writers against the same storage key and inspect native creation/replacement behavior on both platforms.

**Fix strategy**

- [ ] Exclusively create unique temporary files per writer and publish only completed writes.
- [ ] Ensure failure cleanup removes only the calling writer’s temporary file.

**Acceptance criteria**

- [ ] Concurrent saves always leave one complete valid document, never mixed or truncated content.
- [ ] Cover replacement failures and abandoned temporary files; run core/plugin storage aggregate coverage and same-cache smoke.
- [ ] Coordinate with `05 plugin-quiescence-final-storage-saves -bug.md`.
