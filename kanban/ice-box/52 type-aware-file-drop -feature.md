# Type-aware file-drop routing

**Type:** feature
**Priority:** 52
**Raised by:** GPT/Codex, Claude

## User need

Dropping a file should open it in an appropriate Draxul host instead of blindly sending `open_file:` to whichever pane happens to be focused.

## Implementation plan

- [ ] Introduce a pure `DropRouter` below `App` that classifies normalized paths as directory, Markdown, MusicXML/`.mxl`, text/code, or unknown without opening untrusted content.
- [ ] Drive host availability from provider metadata (pending 12), including supported extensions/drop kinds and a default open action.
- [ ] Route Markdown to a Markdown pane, `.musicxml`/`.mxl` to ScoreView, code/text to Nvim, and directories to a shell pane with that working directory.
- [ ] For a focused shell, offer/define a safe “insert path” behavior using platform/shell-aware quoting and bracketed paste; never execute the path automatically.
- [ ] Define modifier behavior (for example, hold a modifier to force focused-host delivery) and a chooser for ambiguous/unknown types.
- [ ] Use `TabController`/PaneManager APIs to split or reuse panes according to one documented policy; do not add host-specific branches to `InputDispatcher`.
- [ ] Preserve existing `open_file:` encoding for direct host delivery and handle multiple dropped paths in stable order with a reasonable count limit.

## Tests and acceptance

- [ ] Table-test extensions, case, directories, Unicode, spaces, quotes, shell metacharacters, Windows drive/UNC paths, missing paths, and multiple drops.
- [ ] Test optional ScoreView/Markdown disabled builds and chooser fallback.
- [ ] Prove shell insertion round-trips as one literal path for Bash, Zsh, PowerShell, and Windows command quoting policy.
- [ ] Host routing is deterministic, never executes a dropped path, and does not lose the existing focused-host override.

## Dependencies and parallelism

Depends on pending 12/22 and hostile `.mxl` item 18. Coordinate with split-with-host (69) so both use one provider picker/factory path.

<model>GPT-5 Codex</model>
