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
- [ ] **Acceptance:** Injected replacement failures preserve original bytes and provide a useful error; successful installation remains functional.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; exercise Windows sharing failures and POSIX replacement behavior.

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
- Validation is owned by the root agent; Windows aggregate/smoke and macOS native
  execution remain gates until their actual results are recorded.
