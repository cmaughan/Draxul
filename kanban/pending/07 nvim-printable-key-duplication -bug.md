# Deliver Space and Backslash to Neovim once
**Severity:** HIGH  
**Source:** Codex #8; `libs/draxul-nvim/src/input.cpp:122`.

Ordinary Space and Backslash produce both named key input and unsuppressed text input, duplicating characters in insert mode.

**Investigation**

- [ ] Trace ordinary and modified key/text event pairs through dispatcher and Neovim input handling.

**Fix strategy**

- [ ] Give printable input one delivery path while preserving modified-key notation and text composition behavior.

**Acceptance criteria**

- [ ] A keydown/text pair inserts one Space or Backslash; modified combinations retain their intended behavior.
- [ ] Run core aggregate tests and same-cache smoke; verify actual typing on Windows and macOS.
