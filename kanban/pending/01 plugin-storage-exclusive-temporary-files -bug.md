# Prevent cross-process plugin storage collisions
**Severity:** CRITICAL  
**Source:** Codex #2; `libs/draxul-host/src/plugin_storage.cpp:467`.

Separate clients saving shared plugin/pane state can select the same `.tmp-N` filename, truncate each other’s writes, or modify an already-published inode.

**Investigation**

- [x] Exercise concurrent writers against the same storage key and inspect native creation/replacement behavior on both platforms.

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

[Build workflow run 35989867854](https://github.com/cmaughan/Draxul/actions/runs/35989867854)
stopped in both hosted jobs during checkout because the workflow token cannot
read the private `draxul-pcbview` submodule. macOS storage execution remains open.

2026-09-25 follow-up: The current Windows Debug core + all-products aggregate
passed 49/49 CTest entries in 238.19 seconds. The standard 30-second same-cache
smoke again timed out while restoring the existing nine-pane Session; the
identical Debug executable and wrapper environment passed under a 90-second
bound in roughly 45 seconds. No macOS storage execution has occurred yet.

2026-09-25 native contention follow-up: Repeated the focused Windows Debug
`PluginStorage concurrent writers publish complete documents` case 100 times
against the existing build, with 8 independent storage instances and 16 saves
per instance to the same key on each run. All 12,800 native saves completed
without a test failure in 32.38 seconds; each final document matched one complete
writer payload. This is a same-process threaded stress test, not a separate-
process or macOS execution, so the cross-platform investigation and macOS
acceptance boxes remain open.

2026-09-25 separate-process follow-up: Added a Windows integration case that
launches eight child `draxul-test-app` processes, holds them at a shared start
barrier, and has each make 16 native storage saves to the same key. It checks
each child exit and requires the final JSON to equal one complete writer
document. The focused `draxul-test-app` `[plugin][storage][process]` selection
passed (1 case, 32 assertions, 0.42 seconds). This covers separate-client
contention on Windows; the macOS-native half of the investigation and the
macOS acceptance gate remain open.

**Platform scope (2026-09-25):** This card intentionally remains pending
because publication uses different native creation/replacement APIs on Windows
and POSIX. The Windows separate-process case is complete; macOS must execute
the `O_EXCL` and replacement path before the investigation and acceptance gate
can be checked.

**macOS native gate (2026-09-26):** Extended the separate-process storage test
to macOS using `posix_spawn`. Eight child processes each made 16 saves to one
storage key; all exited successfully and the final JSON matched one complete
writer document. The case passed 100 repeated runs (12,800 native saves).
The core aggregate then passed 25/25 CTest entries and the same-cache Debug
startup smoke passed. This closes the cross-platform investigation. The
checkbox explicitly requiring hosted macOS CI remains open: the latest
[Build workflow](https://github.com/cmaughan/Draxul/actions/runs/36156449265)
still fails at checkout because its token cannot read the private PCBView
submodule, before any macOS test runs.

**Hosted CI recheck (2026-09-26):** The latest
[Build workflow](https://github.com/cmaughan/Draxul/actions/runs/36228857760)
also failed in both jobs at `actions/checkout` before build or tests. The
previous run's macOS log identifies the private PCBView submodule access failure.
The hosted macOS checkbox remains open until CI can clone that submodule and
run the separate-process case.
