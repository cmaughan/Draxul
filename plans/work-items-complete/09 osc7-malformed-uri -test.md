# 09 osc7-malformed-uri -test

**Type:** test
**Priority:** 9
**Source:** Claude review (review-latest.claude.md)

## Problem

`TerminalHostBase::handle_osc()` processes OSC 7 (`\e]7;<uri>\a`) to update the current working directory. A shell emitting a malformed URI like `\e]7;not-a-url\a` should be silently ignored (or produce a `WARN` log) rather than causing a parse error, crash, or silent state corruption. There is no test for this path.

## Acceptance Criteria

- [x] Locate `handle_osc()` in `libs/draxul-host/src/terminal_host_base_csi.cpp`.
- [x] Add tests using direct sequence injection for:
  - [x] `\e]7;not-a-url\a` — malformed URI, produces a WARN log and is ignored.
  - [x] `\e]7;\a` — empty URI, produces a WARN log and is handled gracefully.
  - [x] `\e]7;file:///valid/path\a` — valid URI, updates CWD correctly (pre-existing test).
  - [x] `\e]7;file://host/path\a` — remote host URI extracts path and updates title.
- [x] Verify no crash under test run.
- [x] Run under `ctest`.

## Implementation Notes

- Use `ScopedLogCapture` if the project has one, to assert on the WARN output for malformed URIs.
- If the handler does not yet gracefully handle malformed URIs, add the guard as part of this item.

## Interdependencies

- No blockers. Independent test item.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
