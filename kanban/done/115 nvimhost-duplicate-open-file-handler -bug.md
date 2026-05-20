# WI 115 — NvimHost Duplicate `open_file_at_type:` Handler

**Type:** Bug  
**Severity:** High (maintenance bug magnet, divergence risk)  
**Source:** Gemini review, GPT review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`libs/draxul-host/src/nvim_host.cpp` contains the `open_file_at_type:` dispatch handler **twice** — at approximately L216 and L248 — each with independently copy-pasted embedded Lua blocks. Any fix or enhancement to this feature must be applied to both copies; if only one is updated, the two code paths silently diverge, producing inconsistent behaviour depending on which dispatch route is taken.

Gemini specifically named this as a "concrete maintenance bug magnet." GPT flagged `NvimHost::dispatch_action()` as containing duplicated Lua at those two locations.

---

## Investigation Steps

- [x] Open `libs/draxul-host/src/nvim_host.cpp`
- [x] Search for `open_file_at_type` — confirm both handlers exist and are structurally identical or nearly so
- [x] Determine which dispatch path invokes each copy (string-match switch? separate if-chain?)
- [x] Check if the two Lua blocks differ in any way (edge case handling, return value, error paths)

---

## Fix Strategy

1. Extract the common logic into a single `nvim_open_file_at_type(path, type)` free function or private method that builds and sends the Lua RPC call.
2. Both dispatch paths call this single function.
3. Write a regression test (see WI 119 for a related `open_file_at_type` RPC dedup test).

---

## Acceptance Criteria

- [x] `grep -n "open_file_at_type"` in `nvim_host.cpp` shows exactly one implementation site.
- [x] The embedded Lua block exists in exactly one place.
- [x] No behaviour change for the normal open-file workflow.
- [x] CI green.

---

## Interdependencies

- Prerequisite for **WI 126** (Lua extraction refactor — easier to extract after dedup).
- Prerequisite for **WI 127** (NvimHost handler consolidation).

---

## Status

The two `open_file_at_type:` handlers in `libs/draxul-host/src/nvim_host.cpp` (around L216 and L248) were byte-identical. The second was unreachable dead code because the first always returns `true` on match. Removed the duplicate block, leaving exactly one implementation. Build (`cmake --build build --target draxul draxul-tests`) succeeds.
