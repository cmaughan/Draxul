# Restore keyboard input when ScoreView becomes visible

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:341` clears the input rig when hidden. The show branch at `:347` excludes keyboard input while resuming playback, so returning to a keyboard-driven Roll/Gate pane rejects keys and can record missed notes.

**Investigation**

- [ ] Hide and show keyboard-driven Roll and Gate modes with background playback disabled.

**Fix strategy**

- [ ] Restore the requested input rig for every applicable non-Clock mode before resuming.
- [ ] Preserve input lease and background-playback semantics.

**Acceptance criteria**

- [ ] Keyboard notes work immediately after showing the pane.
- [ ] Visibility transitions do not create artificial missed-note progress.
- [ ] Other input modes still reacquire correctly.
