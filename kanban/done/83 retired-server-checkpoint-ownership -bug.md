# Prevent retired servers from publishing checkpoints
**Summary:** Stop retired servers from replacing newer saved layouts so restarting preserves the replacement server’s changes.

**Priority:** 83  
**Severity:** CRITICAL  
**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Reported by:** Claude C2; consensus F02.

**Evidence and trigger:** Eviction exits at line 414 but continues into queued requests and checkpoint scheduling at lines 651 and 664. Detached writers retain shared destinations. A successor checkpoint can be overwritten by stale state.

**Related:** `kanban/pending/09 server-checkpoint-state -refactor.md`; `kanban/pending/10 session-model-store -refactor.md`.

- [x] **Investigate:** Trace endpoint ownership, eviction, final saves, already-running writers, and temporary-file publication.
- [x] **Fix:** Distinguish eviction from normal shutdown and suppress post-eviction mutations and saves.
- [x] **Fix:** Prevent outstanding retired-owner writes from publishing over successor state.
- [x] **Acceptance:** A successor’s saved layout survives old-server retirement, including a controlled in-flight old writer.
- [x] **Acceptance:** Ordinary graceful shutdown still saves the latest accepted state.
- [x] **Validation:** Run core aggregate tests and same-cache smoke; exercise ownership replacement on supported platform paths.

## 2026-10-02 implementation

Checkpoint workers now save to unique private staging paths. Only the state thread
publishes a finished stage, after checking PID, process-start token and server epoch.
Retirement skips final queued requests and checkpoint scheduling, and drops task
publication rights. Workers detached after the shutdown budget can only finish their
private files; task destruction cleans those files without touching the destination.
Ordinary shutdown still collects periodic writes and saves the final accepted revision.
Publication reuses the Session model's native atomic replacement and directory flush.

Added a controlled blocked-writer regression that replaces just the endpoint epoch,
waits for retirement, saves successor state, then releases the old writer and verifies
the successor and metadata survive. Existing failure coverage now targets the private
staging path; bounded-shutdown coverage expects no late publication. The periodic
acceptance test now waits for the requested rename to be durable, rather than
accepting an earlier successful checkpoint of the initial topology.

## Validation evidence

- Focused Debug persistence coverage: 13 cases, 273 assertions passed in 9.64s.
- Final `py do.py test debug --products`: both server shards and both detached-server
  integration shards passed, including controlled epoch replacement, blocked old
  writers, bounded shutdown, failure preservation, and graceful final persistence.
  The full selection passed 79/84 CTest entries in 372.89s. Its failures were the
  separate joined-emoji issue (`kanban/done/63 joined-family-emoji-fallback -bug.md`),
  scope membership issue (`kanban/done/64 windows-test-scope-selection -bug.md`),
  remote initial-state timeout (`kanban/pending/65 windows-validation-timing -test.md`),
  SatView catalog-idle deadline, and PCBView render timeout; none are persistence tests.
- Same-cache Debug `py do.py smoke --skip-build` passed with a fresh temporary
  APPDATA profile. The default-profile smoke had timed out loading the live user's
  plugin session; the fresh-profile success is not a claim that this separate
  startup problem is fixed. No user server was stopped.
- Five core Vulkan snapshots passed (basic, cmdline, unicode, panel, nanovg; 26.13s
  combined). Final `py do.py run release --console -- --smoke-test --host nvim`
  passed with an isolated profile; Release build took 27.03s and reused its cache.
- Windows ownership and atomic publication were exercised natively. POSIX ownership,
  rename, and directory-flush paths were inspected; native macOS CI remains the usual
  cross-platform check.

Validation cost: initial focused build regenerated the cache (~86s) and built 17
steps; the first full validation required a repeated build after an executable
sharing conflict, then ran 77 CTest entries in 309.66s. Its two new regression-test
failures were corrected before the final 84-entry aggregate (incremental build
22.75s). Render snapshots were not repeated because subsequent edits only changed
test synchronization and test backend selection. No remote CI was run in this turn.

Final settings-only follow-up also passed both server and detached-server integration
shards in the 53/56-entry core aggregate (81.75s), followed by a passing same-cache
Debug smoke and Release startup. Failure owners for the wider product run are now
`plugins/satview/kanban/pending/15 catalog-seeder-test-deadline -bug.md` and
`plugins/pcbview/kanban/pending/02 pcbview-cancellable-autoroute -perf.md`.
