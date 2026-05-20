# Feature: OSC 133 Shell Integration (Prompt Marks + Command Navigation)

**Type:** feature
**Priority:** 24
**Source:** Gemini review

## Overview

OSC 133 (also known as "semantic prompts") is a widely supported shell integration protocol. Shells (bash, zsh, fish) can be configured to emit markers at:

- `ESC ] 133 ; A ST` — prompt start
- `ESC ] 133 ; B ST` — prompt end / command start
- `ESC ] 133 ; C ST` — command output start
- `ESC ] 133 ; D ; exit_code ST` — command output end

By recording these markers in the scrollback, Draxul can enable:

1. **Jump to previous/next command** — navigate between shell commands via keybinding.
2. **Select command output** — quickly select the output of the last (or any) command.
3. **Visual gutter markers** — subtle indicator lines in the scrollback showing command boundaries.

This is high-value for shell-heavy workflows and is a differentiator from terminals that only offer cursor-position-based scrollback.

## Implementation plan

### Phase 1: Parse OSC 133 sequences

- [ ] Read the OSC dispatch in `libs/draxul-host/src/terminal_host_base_osc.cpp` (or equivalent).
- [ ] Add a handler for OSC sequence `133` (params: `A`, `B`, `C`, `D[;exit_code]`).
- [ ] On each marker, record a `ShellMark` in the scrollback:
  ```cpp
  enum class ShellMarkType { kPromptStart, kCommandStart, kOutputStart, kOutputEnd };
  struct ShellMark {
      ShellMarkType type;
      int scrollback_row;
      int exit_code; // for kOutputEnd only
  };
  ```
- [ ] Store marks in `ScrollbackBuffer` alongside rows, or in a parallel list indexed by scrollback row.

### Phase 2: Command navigation keybindings

- [ ] Register `prev_command` and `next_command` GUI actions.
- [ ] Default keybindings: `ctrl+shift+up` / `ctrl+shift+down` (user-configurable).
- [ ] On `prev_command`: scan backwards through shell marks to find the previous `kPromptStart`; scroll viewport to that row.
- [ ] On `next_command`: scan forwards.

### Phase 3: Select command output

- [ ] Register `select_command_output` GUI action.
- [ ] Find the nearest `kOutputStart` mark above the cursor, and `kOutputEnd` below it.
- [ ] Set `SelectionManager` selection to that range.

### Phase 4: Visual gutter markers

- [ ] In the grid renderer or an ImGui overlay, draw a thin coloured line in the left gutter at each `kPromptStart` row in the current viewport.
- [ ] Optionally: colour the gutter marker by exit code (green = 0, red = non-zero).
- [ ] Add `enable_shell_integration_marks = true` to `config.toml`.

### Phase 5: Shell setup documentation

- [ ] Document the required shell config snippets (e.g. for zsh, bash, fish) in `docs/features.md` or a new `docs/shell-integration.md`.

## Acceptance criteria

- [ ] A zsh session with the OSC 133 prompt snippet configured emits markers that Draxul records.
- [ ] `prev_command`/`next_command` keybindings scroll the viewport to the correct command boundaries.
- [ ] `select_command_output` selects the last command's output.
- [ ] Gutter marks are visible in the scrollback viewport.
- [ ] No crash when OSC 133 markers arrive before the scrollback is initialised.

## Interdependencies

- **`20 osc8-hyperlink-support -feature`**: shares OSC dispatch infrastructure; implement OSC 8 first to validate the pattern.
- **Icebox `20 searchable-scrollback -feature`**: search benefits from knowing command boundaries; coordinate.
- **`06 scrollback-ring-wrap -test`**: shell marks associated with evicted rows must be cleaned up; add a test.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
