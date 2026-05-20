# WI 20 — ChromeHost UTF-8 tab-name width and pane-status layout tests

**Type:** test  
**Source:** review-bugs-latest.gpt.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Lock down correct UTF-8 measurement, truncation, and hit-testing in the `ChromeHost` tab bar and pane status strip. This test covers the behaviour fixed by WI 13 (chromehost-utf8-byte-counting).

---

## What to test

**Tab bar:**
- [ ] A tab named with 4-byte emoji (e.g. `"🔥 main"`) is measured as 7 display columns, not 11 bytes.
- [ ] Truncation of a long CJK tab name at a codepoint boundary produces valid UTF-8 (no truncated multibyte sequences).
- [ ] Hit-testing (click-to-activate tab) lands on the correct tab when names include wide characters.

**Pane status strip:**
- [ ] A pane path with multibyte codepoints (e.g. `/Users/André/code`) truncates correctly at a codepoint boundary.
- [ ] The rendered glyph sequence from the warming loop (line 1018) does not include invalid one-byte fragments.

**Rename input:**
- [ ] A tab renamed to a UTF-8 string is subsequently displayed with correct width (consistent with rename-input behaviour which is already UTF-8 aware).

---

## Implementation notes

- These tests can target the `ChromeHost` measurement utilities directly if they are extractable, or drive through a fake `ChromeHost` render pass.
- For hit-testing: construct a synthetic tab bar layout, fire a fake mouse click, and assert the correct tab index is selected.
- Do not require GPU rendering — use fake renderer.
- Place in `tests/chrome_host_test.cpp`.

---

## Interdependencies

- **Requires WI 13 (chromehost UTF-8 byte counting fix) to pass**.
- WI 25 (centralised test fixtures) provides fake renderer / window helpers.
- The rename input path (already UTF-8 aware) should be tested alongside these for consistency.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
