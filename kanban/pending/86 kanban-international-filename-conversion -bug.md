# Preserve international Kanban names safely
**Summary:** Preserve card names in their original characters so opening a board cannot crash on international names.

**Priority:** 86  
**Severity:** CRITICAL  
**Source:** `modules/kanban/draxul-kanban/src/kanban_store.cpp`  
**Reported by:** Claude C6; consensus F06.

**Evidence and trigger:** Lines 578, 616, and 658 use throwing narrow filename conversions. On Windows, scanning an emoji or Asian card name outside the active code page can escape board loading.

**Related:** `kanban/pending/75 windows-markdown-source-paths -bug.md` owns the related Markdown path boundary.

- [ ] **Investigate:** Trace repository, lane, card, title, preview, and error-text conversions while preserving native paths.
- [ ] **Fix:** Convert displayed names explicitly to UTF-8 and contain scan/conversion failures at the board boundary.
- [ ] **Acceptance:** International repository/lane/card names load, display, reload, and select correctly on Windows with a non-UTF-8 code page.
- [ ] **Acceptance:** A failed scan leaves usable state and reports an error without terminating the application.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; verify Windows filename behavior and existing POSIX behavior.
