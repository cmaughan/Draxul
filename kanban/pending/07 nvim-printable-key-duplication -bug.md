# Deliver Space and Backslash to Neovim once
**Severity:** HIGH  
**Source:** Codex #8; `libs/draxul-nvim/src/input.cpp:122`.

Ordinary Space and Backslash produce both named key input and unsuppressed text input, duplicating characters in insert mode.

**Investigation**

- [x] Trace ordinary and modified key/text event pairs through dispatcher and Neovim input handling.

**Fix strategy**

- [x] Give printable input one delivery path while preserving modified-key notation and text composition behavior.

**Acceptance criteria**

- [x] Automated paired-event checks deliver one Space or Backslash and preserve modified combinations.
- [x] Run the Windows core/product aggregate and a same-cache startup smoke with a sufficient bound.
- [ ] Verify actual Space, Backslash, Shift, and modified-chord typing in Neovim on Windows and macOS.

**Implementation notes:** SDL keydown and text input both reach `NvimInput` through
`InputDispatcher`/`NvimHost`. Space, Backslash, and Less were translated into
named keys on keydown, then sent again from text input. Unmodified/Shift-only
printable keys now use the composed text event; Ctrl/Alt combinations retain
Neovim's named-key notation and suppress their paired text. `tests/input_tests.cpp`
covers paired events, Shift layout output, `<lt>` escaping, and modified chords.

**Validation (Windows, 2026-09-25):** `py do.py test debug --products`
passed 49/49 CTest cases in 238.19s; the Unicode render snapshot passed.
The standard same-cache smoke exceeded its fixed 30s limit while restoring the
existing nine-pane session. The identical wrapper environment passed with a
90s bound, completing startup in approximately 45s. Real Neovim typing on
Windows and macOS remains unverified.
