# 133 xterm-256color-compatibility -feature

**Type:** Feature  
**Status:** In Progress  
**Priority:** Medium  
**Owner:** Unassigned

## Summary

Draxul's Unix PTY shell hosts now advertise `TERM=xterm-256color` by default
and also set `COLORTERM=truecolor` / `TERM_PROGRAM=draxul`. This item tracks
the compatibility work needed to make that identity defensible for common
shell-launched applications rather than merely flipping the environment.

This item tracks the work required to raise that terminal identity safely,
without regressing shell-launched tools such as `nvim`, `vim`, `less`, `tmux`,
`fzf`, `htop`, and ncurses-based applications.

## Problem

Advertising `TERM=xterm-256color` is more than "enable 256 colors". It also
signals that applications may rely on xterm-style queries, private modes,
charset behavior, and terminfo-defined key/control capabilities. If Draxul
claims that identity before implementing those behaviors, terminal programs may:

- enter code paths that assume unsupported control/query responses
- mis-detect capabilities and render incorrectly
- break startup or repaint behavior in shell-launched full-screen apps
- diverge from user expectations for ncurses- and terminfo-driven tooling

We already support many important pieces:

- ANSI + 256-color SGR handling
- alt screen
- scroll regions
- mouse 1000/1002/1003/1006
- focus reporting (`CSI ? 1004 h/l` -> `CSI I` / `CSI O`)
- bracketed paste
- cursor-shape control
- DEC special graphics / ACS line drawing (`ESC ( 0`, `ESC ) 0`, `SO`, `SI`)
- OSC 7 / OSC 52
- synchronized output

## Goal

Support `TERM=xterm-256color` as the default Unix PTY shell identity with good
practical compatibility for common terminal software, backed by explicit tests
and capture-based regressions.

## Scope

- audit the xterm/terminfo surface that common apps actually depend on
- implement missing high-value terminal behaviors or queries
- add regression tests for shell-launched full-screen tools
- only switch the default TERM once the compatibility bar is defensible

## Non-Goals

- shipping a custom terminfo entry in this item
- making Draxul perfectly emulate every historical xterm edge case
- changing embedded `nvim --embed` TERM handling; that path should remain
  non-terminal (`TERM=dumb`)

## Candidate Gaps To Audit

- alternate character set / line-drawing charset handling (`ESC ( 0` and friends)
- richer terminal query/response coverage beyond the current DA/DSR subset
- focus reporting and other xterm private modes commonly probed by apps
- broader key-sequence compatibility tied to xterm terminfo expectations
- additional OSC/DCS behaviors that terminfo- or app-driven probes may expect
- full-screen curses/ncurses repaint behavior under shell startup and teardown

## Implementation Sketch

- [ ] Capture and catalog startup/probe sequences from representative apps:
  - `nvim`
  - `vim`
  - `less`
  - `tmux`
  - `htop` / `btop`
  - `fzf`
  - a small ncurses probe app if needed
- [ ] Compare those captures against Draxul's implemented VT/xterm behavior.
- [x] Prioritize missing pieces by compatibility impact, not spec completeness.
- [x] Add focused parser/terminal-state tests for each newly implemented behavior.
- [ ] Add at least one PTY capture replay test for shell -> full-screen-app ->
  shell return transitions.
- [x] Re-evaluate the default shell TERM once the regression set is green.

## Implemented In This Pass

- [x] Default Unix PTY shell environment now sets `TERM=xterm-256color`,
  `COLORTERM=truecolor`, and `TERM_PROGRAM=draxul`.
- [x] Added DEC special graphics / ACS support for line-drawing charsets.
- [x] Added xterm focus reporting support (`DECSET ? 1004`).
- [x] Added regression tests for ACS, focus reporting, and PTY environment propagation.
- [ ] Follow up with real-app startup captures and PTY replay coverage before
  considering this item fully complete.

## Known Unimplemented Xterm Gaps

These are not just "not yet checked" items; they are currently absent or only
partially implemented in the tree and should be treated as follow-up fixes if
the close-out validation shows they matter in practice.

- Richer OSC coverage beyond OSC `0`/`2` (title), `7` (cwd), and `52`
  (clipboard). Notably absent today:
  - OSC `8` hyperlinks
  - palette / color OSCs such as `4`, `10`, `11`, `12`
  - broader xterm / iTerm-style OSC extensions
- Richer query/response support beyond:
  - DSR `5` / `6`
  - DA1 / DA2
  Missing examples include:
  - `DECRQSS`
  - `XTGETTCAP`
  - xterm window ops / `XTWINOPS`
  - other DCS-based xterm probes
- Broader xterm private-mode coverage beyond the currently implemented set:
  - `1`, `6`, `7`, `25`, `47`, `1047`, `1049`
  - `1000`, `1002`, `1003`, `1004`, `1006`
  - `2004`, `2026`
  Missing examples include `1005` and `1015`.
- Keyboard protocol coverage is still basic xterm-compatible rather than
  full-featured xterm:
  - no `modifyOtherKeys`
  - no CSI-u / kitty keyboard-style encoding
  - modified function-key coverage is incomplete
  - `F13`-`F24` are still unmapped
  - `F1`-`F12` modifier combinations currently collapse to the base sequence

The intent of this item is still "good practical xterm-256color
compatibility", not perfect xterm emulation. If validation shows that some of
the above are harmless for the target app set, they do not necessarily need to
block closing this item. But if any of them are what breaks `tmux`, `vim`,
`less`, `fzf`, `btop`, or a curses probe app, they should be promoted into the
implementation checklist below.

## Validation

- Common shell-launched full-screen tools start cleanly under `TERM=xterm-256color`.
- Returning from a full-screen app leaves the shell prompt correctly repainted.
- No new regressions in existing terminal VT, mouse, and cursor-refresh tests.
- PTY capture replays cover at least one real-world full-screen startup/exit flow.

## Practical Compatibility Checklist

Treat this as the minimum bar before advertising an xterm-family TERM
intentionally.

- [x] `vim` starts, redraws, resizes, and exits cleanly from a shell pane
- [x] `nvim` starts, redraws, resizes, and exits cleanly from a shell pane
- [x] `less` renders and exits cleanly without leaving stale screen state
- [x] `fzf` renders interactively and returns to the shell with a clean prompt
- [x] `htop` or `btop` renders and exits cleanly
- [x] `tmux` starts inside the shell pane without obvious capability mismatch
- [ ] A small ncurses test app renders borders/line drawing correctly
- [x] Alternate character set / line-drawing mode behaves correctly
- [ ] Alt-screen enter/exit preserves and restores the shell prompt correctly
- [x] Window resize during a full-screen app leaves the app and shell in a valid state
- [ ] Function keys and modified keys match the expected xterm behavior for common apps
- [ ] Common terminal capability probes do not push apps into visibly broken code paths
- [ ] PTY capture replay exists for at least one shell -> full-screen app -> shell return flow

## Close-Out Test Plan

Use this section as the explicit sign-off plan for the remaining unchecked
items above.

### 1. Record real startup/probe captures

- [ ] Launch Draxul with PTY capture enabled:

  ```bash
  DRAXUL_CAPTURE_PTY_FILE=/tmp/draxul-xterm-capture.log \
  ./build/draxul.app/Contents/MacOS/draxul --host zsh --log-file /tmp/draxul-xterm.log --log-level trace
  ```

- [ ] In the shell pane, run each representative app and quit it cleanly:
  - `vim`
  - `nvim`
  - `less README.md`
  - `fzf`
  - `tmux`
  - `btop`
  - the small ncurses probe app from step 4 below
- [ ] Keep one capture per app if needed for easier replay/debugging.

Pass bar:

- startup and exit are visually clean
- no app lands in an obviously broken fallback path
- captures exist for at least the highest-value full-screen flows

### 2. Compare captures against implemented behavior

- [ ] Grep the trace log for warnings or unhandled terminal traffic:

  ```bash
  rg -n "unhandled|OSC|CSI|ESC|WARN" /tmp/draxul-xterm.log
  ```

- [ ] Compare the observed probes with the handlers in:
  - `libs/draxul-host/src/terminal_host_base.cpp`
  - `libs/draxul-host/src/terminal_host_base_csi.cpp`
- [ ] For any unsupported probe seen during app startup, decide whether it is:
  - harmless and documentable
  - worth implementing now
  - evidence that `xterm-256color` is still too aggressive

Pass bar:

- unsupported probes do not cause visible repaint or interaction breakage
- any probe that does break an app is turned into an implementation task

### 3. Add one real PTY capture replay test

- [ ] Reuse the existing capture harness in `tests/terminal_capture_tests.cpp`.
- [ ] Pick one real flow, ideally:
  - shell -> `less` -> shell
  - shell -> `fzf` -> shell
  - shell -> `btop` -> shell
- [ ] Replay the captured chunks into `TestVtTerminalHost`.
- [ ] Assert at minimum:
  - final prompt text is correct
  - final cursor row/col are correct
  - the shell state after app exit is sane

Pass bar:

- at least one real full-screen app startup/exit flow is replayed in tests

### 4. Add and run a small ncurses border app

- [ ] Build or check in a tiny ncurses probe app that:
  - draws an outer border with `box()` / ACS chars
  - draws one inner horizontal rule
  - draws one inner vertical rule
  - optionally handles one resize before exit
- [ ] Run it under Draxul with `TERM=xterm-256color`.

Pass bar:

- no tofu
- corners, verticals, and horizontals render correctly
- resize does not corrupt the border

### 5. Validate alt-screen shell restore manually

- [ ] Use an obvious prompt, for example:

  ```bash
  printf 'BEFORE-ALTSCREEN\n'
  PS1='PROMPT-XTERM> '
  ```

- [ ] Run and quit:
  - `less README.md`
  - `vim`
  - `fzf` (cancel)
- [ ] Repeat at least one run with a resize while inside the full-screen app.

Pass bar:

- shell prompt is restored exactly once
- no stale app rows remain on screen
- cursor lands at the correct prompt position

### 6. Validate function keys and modified keys

- [ ] Use a raw key probe in the shell pane (small script/app that prints the
  received bytes).
- [ ] Test:
  - arrows
  - `Home` / `End`
  - `PgUp` / `PgDn`
  - `F1`-`F12`
  - modified arrows
  - modified function keys
- [ ] Repeat spot checks inside:
  - `vim`
  - `less`
  - `tmux`

Pass bar:

- common keys produce the expected xterm-family behavior for the target app set
- if modified key behavior is lacking, convert that into a follow-up
  implementation task instead of silently checking this off

### 7. Validate capability probes do not trigger broken code paths

- [ ] Use the startup captures and trace logging from steps 1-2.
- [ ] Focus especially on:
  - `tmux`
  - `vim`
  - `less`
  - `fzf`
- [ ] Look for repeated unanswered queries, odd fallback behavior, repaint
  churn, or startup loops.

Pass bar:

- common probes do not push apps into visibly broken behavior
- if they do, add the required protocol support before closing the item

## Likely Follow-Up Fixes If Validation Fails

If any close-out test above fails, the most likely implementation follow-ups
are:

- [ ] Add richer OSC support if a target app relies on it:
  - OSC `8` hyperlinks
  - palette / color OSCs (`4`, `10`, `11`, `12`)
- [ ] Add richer xterm query/response support if a target app probes it:
  - `DECRQSS`
  - `XTGETTCAP`
  - xterm window ops / `XTWINOPS`
- [ ] Add broader xterm private-mode coverage if a target app still negotiates
  older mouse/probe modes:
  - `1005`
  - `1015`
- [ ] Improve keyboard encoding if common apps need richer key reporting:
  - modified function-key sequences
  - `F13`-`F24`
  - `modifyOtherKeys` or equivalent richer key protocol support

## Notes

- Passing the checklist is more important than claiming spec completeness.
- If the checklist reveals that `xterm` is supportable but `xterm-256color` is
  still too aggressive, it is acceptable to split this item later into:
  - safe `xterm` support
  - later `xterm-256color` promotion

## Coordination

- Related to `plans/work-items-icebox/21 per-pane-env-overrides -feature.md`
  because that work would let users override TERM/COLORTERM explicitly.
- Related to the existing terminal VT compatibility work already completed in
  `plans/work-items-complete/64 terminal vt compatibility hardening -feature.md`.
