# Preserve settings when hook replacement fails
**Summary:** Preserve existing settings when replacement fails so installing conversation hooks cannot erase them.

**Priority:** 84  
**Severity:** CRITICAL  
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`  
**Reported by:** Claude H5; consensus F03.

**Evidence and trigger:** Lines 194 and 202 delete the original and staged files after two rename failures. Windows file sharing can prevent replacement while allowing original deletion.

**Related:** `kanban/pending/11 atomic-file-replacement -refactor.md`.

- [ ] **Investigate:** Trace every hook/settings caller and the replacement, cleanup, permission, and recovery contracts.
- [ ] **Fix:** Replace files without deleting the destination as a fallback.
- [ ] **Fix:** Preserve recoverable staged contents when publication fails.
- [ ] **Acceptance:** Injected replacement failures preserve original bytes and provide a useful error; successful installation remains functional.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; exercise Windows sharing failures and POSIX replacement behavior.
