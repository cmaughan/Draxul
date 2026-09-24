# Prevent cross-process plugin storage collisions
**Severity:** CRITICAL  
**Source:** Codex #2; `libs/draxul-host/src/plugin_storage.cpp:467`.

Separate clients saving shared plugin/pane state can select the same `.tmp-N` filename, truncate each other’s writes, or modify an already-published inode.

**Investigation**

- [ ] Exercise concurrent writers against the same storage key and inspect native creation/replacement behavior on both platforms.

**Fix strategy**

- [x] Exclusively create unique temporary files per writer and publish only completed writes.
- [x] Ensure failure cleanup removes only the calling writer’s temporary file.

**Acceptance criteria**

- [x] Windows concurrent saves leave one complete valid document, never mixed or truncated content.
- [ ] Validate concurrent saves on macOS CI.
- [x] Cover replacement failures and abandoned temporary files; run core/plugin storage aggregate coverage and a same-cache smoke.
- [x] Coordinate with `05 plugin-quiescence-final-storage-saves -bug.md`.

**Implementation and validation (2026-09-24, Windows):** Native output creation now uses
`CREATE_NEW` on Windows and `O_CREAT | O_EXCL` on POSIX, with a process/time/serial
temporary name and collision retry. The writer flushes and closes its own temporary
file before replacement; only a temporary file successfully opened by that writer
is eligible for cleanup. Windows replacement retries transient access/sharing/lock
errors from simultaneous publishers. The storage API and reload overlay path are
unchanged, so the final-save work in `05 plugin-quiescence-final-storage-saves -bug.md`
uses the same publication path.

The focused `draxul-test-app` `[plugin][storage]` selection passed: 6 cases,
121 assertions. It covers native refusal to truncate an abandoned file, collision
retry, replacement-failure cleanup, and 8 concurrent writers making 16 saves each
to one key, with the final JSON matching one complete writer document. The first
stress run found 38 transient Windows replacement failures; the bounded retry
resolved them in the rerun. POSIX behavior has been implemented and inspected, but macOS execution
is still needed before the cross-platform investigation and macOS acceptance boxes can
be checked.

The shared Windows Debug core aggregate passed 25/25 CTest entries. The
same-cache single-pane `--smoke-test --host nvim` passed in 8.6 seconds, and
the rebuilt Release app passed `--smoke-test`. The default `py do.py smoke
--skip-build` exceeded its 30-second guard while loading the existing
nine-pane shared Session; the same Debug binary completed that startup with
a 90-second bound.
