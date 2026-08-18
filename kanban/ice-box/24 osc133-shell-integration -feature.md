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

- [x] Parse OSC 133 in `libs/draxul-terminal-core/src/terminal_core_csi.cpp`.
- [x] Handle `A`, `B`, `C`, and `D[;exit_code]` markers.
- [x] Record bounded, resize-safe shell marks in terminal state and protocol snapshots.
  ```cpp
  enum class ShellMarkType { kPromptStart, kCommandStart, kOutputStart, kOutputEnd };
  struct ShellMark {
      ShellMarkType type;
      int scrollback_row;
      int exit_code; // for kOutputEnd only
  };
  ```
- [x] Keep marks coherent with scrollback eviction and terminal snapshots.

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
- [x] Add `enable_shell_integration_marks = true` to the declarative configuration schema.

### Phase 5: Shell setup documentation

- [ ] Document the required shell config snippets (e.g. for zsh, bash, fish) in `docs/features.md` or a new `docs/shell-integration.md`.

## Acceptance criteria

- [ ] A zsh session with the OSC 133 prompt snippet configured emits markers that Draxul records.
- [ ] `prev_command`/`next_command` keybindings scroll the viewport to the correct command boundaries.
- [ ] `select_command_output` selects the last command's output.
- [ ] Gutter marks are visible in the scrollback viewport.
- [x] No crash when OSC 133 markers arrive before scrollback is initialized; terminal,
      replay, protocol, and server tests cover parsing and transport.

## Interdependencies

- `kanban/ice-box/20 searchable-scrollback -feature.md` can use the delivered mark model.
- Remaining work is client navigation, selection, gutter presentation, and shell setup documentation.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
