# Restore keyboard input when ScoreView becomes visible

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:341` clears the input rig when hidden. The show branch at `:347` excludes keyboard input while resuming playback, so returning to a keyboard-driven Roll/Gate pane rejects keys and can record missed notes.

**Investigation**

- [x] Hide and show a keyboard-driven gated flow with background playback disabled.

**Fix strategy**

- [x] Restore the requested input rig for every applicable non-Clock mode before resuming.
- [x] Preserve input lease and background-playback semantics.

**Acceptance criteria**

- [x] Keyboard notes work immediately after showing the pane.
- [x] Visibility transitions do not create artificial missed-note progress.
- [x] Other input modes still reacquire correctly.
