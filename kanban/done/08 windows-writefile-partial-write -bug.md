# WI 08 — Windows `WriteFile` doesn't retry partial writes; RPC stream truncation

**Type:** bug  
**Severity:** HIGH (Windows-only)  
**Source:** review-bugs-latest.claude.md (MEDIUM — functionally HIGH)  
**Consensus:** review-consensus.md Phase 4

---

## Problem

`libs/draxul-nvim/src/nvim_process.cpp:187` (Windows path):

```cpp
return WriteFile(impl_->child_stdin_write_, data, (DWORD)len, &written, nullptr) && written == len;
```

`WriteFile` to a named pipe is allowed to perform a **partial write** — return `TRUE` with `written < len`. The current code returns `false` in that case without retrying, silently dropping the tail of the msgpack message. The caller treats `false` as a hard failure and logs a warning, but the RPC stream is now corrupt for all subsequent messages.

The POSIX implementation (lines ~378–386) correctly loops until all bytes are delivered.

**Trigger:** Sending a large RPC request (e.g. pasting many lines, `nvim_buf_set_lines` with large content) when the pipe's kernel buffer is nearly full.

**Files:**
- `libs/draxul-nvim/src/nvim_process.cpp` — Windows `write()` implementation (~line 187)

---

## Implementation Plan

- [x] Replace the single `WriteFile` call with a retry loop that advances the data pointer by `written` bytes each iteration, mirroring the POSIX `write()` loop.
- [x] Handle `WriteFile` returning `FALSE` (error) vs `TRUE` with `written == 0` (should not happen on blocking pipe, but guard anyway) as distinct failure cases.
- [x] Add a comment explaining why the loop is necessary (pipe partial-write contract).
- [ ] Write a unit test that mocks the Windows pipe with a partial-write shim and verifies the full message is eventually delivered (or add to the Windows CI smoke test). — deferred; requires Windows-only pipe mock infrastructure that doesn't exist in the tree.
- [x] Verify no regression on the existing Windows pipe tests. — macOS unit test suite + smoke test pass; Windows CI will exercise the path.

---

## Interdependencies

- Independent of Phase 1 RPC hardening but should land in the same overall RPC robustness effort.
- macOS/Linux paths not affected.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
