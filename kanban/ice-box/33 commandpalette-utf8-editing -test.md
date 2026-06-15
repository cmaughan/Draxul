# WI 33 — commandpalette-utf8-editing

**Type:** test  
**Priority:** Medium  
**Source:** review-consensus.md §5c — GPT  
**Produced by:** claude-sonnet-4-6

---

## Problem

The command palette input field accepts text and supports backspace, but it has no test coverage for multibyte UTF-8 input. Specific gaps:

1. Backspace over a multibyte sequence (e.g., a 3-byte CJK character) should remove one codepoint, not one byte.
2. Pasting a non-ASCII string into the palette input should not corrupt the search query.
3. Cursor positioning within a multibyte cluster should not produce split sequences.
4. Fuzzy-match scoring should operate on codepoints (or at least produce correct results) when the query contains non-ASCII.

This is separate from WI 20 (ChromeHost UTF-8 layout test) and WI 13 (UTF-8 byte counting), which test the tab/pane label rendering path. The command palette has its own input buffer and rendering path.

---

## Investigation

- [ ] Read `app/command_palette_host.cpp` — locate the input buffer type, the backspace handler, the paste handler, and how the query string is passed to the fuzzy matcher.
- [ ] Check if the input buffer stores raw bytes or decoded codepoints.
- [ ] Check if backspace is byte-based (`pop_back()` on a `std::string`) or codepoint-based.
- [ ] Identify the test file for command palette (likely `tests/command_palette_test.cpp` or similar).

---

## Test Cases to Implement

### Case 1: Type multibyte character, then backspace
- Simulate SDL text-input events delivering a 3-byte UTF-8 sequence (e.g., U+4E2D, `中`).
- Send backspace.
- Assert input buffer is empty (one codepoint removed, not one byte).

### Case 2: Type multibyte then ASCII, then backspace twice
- Simulate `中A` input.
- Send backspace twice.
- Assert buffer is empty.

### Case 3: Paste non-ASCII string
- Simulate a paste event containing `"søk: 中文"`.
- Assert the full string appears correctly in the palette query (no truncation, no corruption).
- Assert fuzzy match runs without crash.

### Case 4: Filter with non-ASCII query
- Load a fake command list including a command with a non-ASCII description.
- Type a non-ASCII query.
- Assert no crash and results are returned (even if matching is approximate).

### Case 5: Emoji input (4-byte sequence)
- Simulate input of a 4-byte emoji (e.g., U+1F600 😀).
- Assert it is accepted as a single unit (not rendered as replacement characters).
- Backspace removes it in one keypress.

---

## Implementation Notes

- Look at how existing palette tests drive input — replicate the same pattern.
- If the input buffer is a bare `std::string` with `pop_back()` for backspace, the fix is to use a UTF-8 aware "previous codepoint start" helper before erasing; this WI should also file a bug (or just fix it in-place if trivial).
- Tests should live in an existing or new `tests/command_palette_utf8_test.cpp`.

---

## Acceptance Criteria

- [ ] All 5 cases pass under `ctest`.
- [ ] Run under ASan — no memory errors.
- [ ] If a backspace bug is found, fix it and add a regression assertion.
- [ ] Smoke test passes.
