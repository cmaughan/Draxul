# Preserve settings when hook replacement fails
**Summary:** Preserve existing settings when replacement fails so installing conversation hooks cannot erase them.

**Priority:** 84  
**Severity:** CRITICAL  
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`  
**Reported by:** Claude H5; consensus F03.

**Evidence and trigger:** Lines 194 and 202 delete the original and staged files after two rename failures. Windows file sharing can prevent replacement while allowing original deletion.

**Related:** `kanban/pending/11 atomic-file-replacement -refactor.md`.

- [x] **Investigate:** Trace every hook/settings caller and the replacement, cleanup, permission, and recovery contracts.
- [x] **Fix:** Replace files without deleting the destination as a fallback.
- [x] **Fix:** Preserve recoverable staged contents when publication fails.
- [x] **Acceptance:** Injected replacement failures preserve original bytes and provide a useful error; successful installation remains functional.
- [x] **Validation:** Run core aggregate tests and same-cache smoke; exercise Windows sharing failures and POSIX replacement behavior.

## Implementation notes (2026-10-02)

- Hook scripts, Codex/Claude JSON registration (including uninstall), and Codex
  feature configuration all publish through the same repaired `write_atomic`.
- Stage with exclusive creation and a distinct process/time/sequence filename;
  a subsequent attempt cannot truncate a previous recovery file. Windows uses
  `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` and POSIX uses same-directory
  rename. No path deletes the destination to permit replacement.
- Completed staging survives permission/publication failure. The error includes
  the native failure and full recovery path. Partial write/close failures clean
  up only the incomplete staging file. POSIX settings retain existing permissions;
  new staging files start private (0600).
- Added integration regressions for attempted publication over an empty directory,
  repeated failure preserving both recovery copies, Windows sharing denial for
  each hook/config writer, Claude uninstall, and POSIX private permissions. Existing idempotent
  install/uninstall coverage continues to exercise successful replacement.
- Validation is owned by the root agent. The Windows aggregate exercised and
  passed all hook/config integration cases, including native sharing-denial
  regressions and successful install/uninstall. Same-cache smoke and POSIX
  replacement behavior remain pending until their actual results are recorded.

## Windows validation evidence (2026-10-02)

- `py do.py test debug --products`: all agent-integration cases passed. The wider
  run completed with 79/84 CTest entries passing; unrelated failures are recorded
  by the root agent. Windows tests verify unchanged original bytes, retained
  recovery copies and diagnostic paths, safe retry, and successful publication.
- The private-permissions test is POSIX-only and was not exercised by this Windows
  run. No native macOS gate is added to this card beyond its existing POSIX gate.

## POSIX validation evidence (2026-10-02)

- A standalone WSL Ubuntu 22.04 public-API harness compiled the actual
  `agent_integration.cpp` using the existing JSON/TOML headers, without another
  CMake cache or fetched dependencies. Successful existing-file replacement,
  idempotence, unrelated setting preservation and existing private permissions
  all passed. Failed publication preserved destination bytes, retained both
  recovery files across retries, and reported recovery paths.
- The first Linux run exposed that `ifstream` can open a directory and throw
  during byte iteration when the result status inspects a failed destination.
  `read_optional_document` now checks for a regular file and returns its existing
  typed read error. The same production-source harness passed after this fix.
- Harness/executable are under `/tmp/draxul-hooks-validation-20261002/` in WSL;
  source copy is in the Windows temporary folder as
  `draxul-posix-hooks-harness-20261002.cpp`. POSIX replacement gate is satisfied;
  root owns final post-fix core aggregate and same-cache smoke evidence.

## Final handoff evidence

- After the POSIX typed-read correction, `py do.py test debug` passed every
  agent-integration regression and 53/56 CTest entries overall in 81.75s. The
  remaining failures are separately tracked in `kanban/pending/63 joined-family-emoji-fallback -bug.md`,
  `kanban/pending/64 windows-test-scope-selection -bug.md`, and
  `kanban/pending/65 windows-validation-timing -test.md`.
- Final same-cache Debug `py do.py smoke --skip-build` exited 0 with a short
  isolated APPDATA profile (9.72s including owned-server cleanup). Final Release
  Neovim startup exited 0, spawned Neovim and created the Vulkan swapchain. No user
  server or UI was stopped. An earlier Release relink encountered an executable
  sharing lock, which cleared before retry; a preceding isolated smoke exited 1
  without usable endpoint metadata. The successful checks do not resolve those
  independent environment/startup observations.
- Validation cost: existing Debug cache, 19 incremental build/staging steps in
  32.28s; final core aggregate 81.75s. This was a necessary additional aggregate
  after the real POSIX harness found the inspection exception. Earlier core/product
  aggregate ran 84 entries in 372.89s; native shader/atlas snapshots were not
  repeated for this settings-only correction. No remote CI was run.
