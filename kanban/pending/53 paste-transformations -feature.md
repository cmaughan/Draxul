# Paste transformations

**Type:** feature
**Priority:** 53
**Raised by:** GPT/Codex

## User need

Offer normal paste, single-line paste, and shell-escaped paste so multi-line clipboard content and paths can be inserted safely for the current task.

## Implementation plan

- [ ] Define a platform-neutral `PasteTransform` enum and pure transformation helpers in `draxul-host` for unchanged, newline-to-space single-line, and shell-literal modes.
- [ ] Make shell escaping host-aware: POSIX shell quoting for Bash/Zsh, PowerShell literal rules for PowerShell, and the existing Windows command quoting helper where relevant.
- [ ] Extend terminal host actions to accept a transform, then pass transformed text through the existing large-paste confirmation and bracketed-paste path.
- [ ] Keep Nvim normal paste unchanged; expose only transformations whose semantics are clear for that host.
- [ ] Add palette entries/subcommands and include a preview (line/byte count and transformed excerpt) in the confirmation overlay for risky multi-line content.
- [ ] Enforce clipboard and transformed-output size limits and avoid logging clipboard contents.

## Tests and acceptance

- [ ] Table-test empty, Unicode, CRLF/LF, tabs, quotes, dollars, backticks, backslashes, newlines, trailing newline, and very large input for every shell policy.
- [ ] Confirm large-paste replacement/cancel behavior remains correct for each transform.
- [ ] No transform executes text, loses embedded Unicode, bypasses bracketed paste, or leaks clipboard content to logs.
- [ ] Update action registry/keybinding docs and run terminal/Nvim paste tests plus smoke.

## Dependencies and parallelism

Independent host-layer feature. Reuse the safe quoting primitives required by item 52; one owner should define quoting contracts to avoid divergent shell policies.

<model>GPT-5 Codex</model>
