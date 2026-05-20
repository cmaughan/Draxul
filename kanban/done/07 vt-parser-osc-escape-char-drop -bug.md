# WI 07 — vt-parser-osc-escape-char-drop

**Type:** bug  
**Priority:** MEDIUM (terminal control sequence silently lost; visible as missing cursor motion or title update)  
**Platform:** all  
**Source:** review-bugs-consensus.md — BUG-08 (Claude)

---

## Problem

`VtParser` in `libs/draxul-host/src/vt_parser.cpp` (lines 166–170) handles `State::OscEsc` — the state entered after consuming an `ESC` byte that might be the start of a String Terminator (`ESC \\`). If the next character is not `\\`, the parser transitions to Ground but silently discards the character. If that character was `[` (CSI introducer) or `]` (OSC start) or another escape-sequence character, the entire following terminal sequence is lost.

**Concrete example:** `\x1B]0;title\x1B[A` (OSC 0 title followed immediately by a cursor-up CSI with no explicit ST) — the `[A` CSI is swallowed and the cursor does not move up.

---

## Investigation

- [x] Read `libs/draxul-host/src/vt_parser.cpp` lines 155–175 to confirm the `OscEsc` state body.
- [x] Check what other states transition into `OscEsc` (should be only from `Osc` on `ESC` byte).
- [x] Look at VT parser test fixtures in `tests/` to understand how to write a regression test.

---

## Fix Strategy

- [x] In `State::OscEsc`, re-route the non-`\\` character instead of discarding it:
  ```cpp
  case State::OscEsc:
      if (ch == '\\') {
          cbs_.on_osc(osc_buffer_);
      } else {
          // ESC was consumed as potential ST; re-dispatch ch.
          if (ch == '[') { csi_buffer_.clear(); state_ = State::Csi; return; }
          else if (ch == ']') { osc_buffer_.clear(); state_ = State::Osc; return; }
          else if (cbs_.on_esc) cbs_.on_esc(ch);
      }
      state_ = State::Ground;
      break;
  ```
- [x] Add a VT parser unit test covering `\x1B]0;title\x1B[A` that verifies both the OSC and CSI are processed (or at minimum that the non-ST path does not drop sequences).

---

## Acceptance Criteria

- [x] A non-`\\` character in `OscEsc` state is re-dispatched, not dropped.
- [x] Unit test covers the `ESC ] ... ESC [ A` sequence and asserts the CSI is delivered.
- [x] Existing VT parser tests still pass: `ctest --test-dir build -R draxul-tests`.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
